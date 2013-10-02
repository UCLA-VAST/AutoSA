#include <assert.h>

#include <isl/ilp.h>

#include "gpu_array_tile.h"
#include "gpu_group.h"
#include "schedule.h"

/* Print the name of the local copy of a given group of array references.
 */
__isl_give isl_printer *gpu_array_ref_group_print_name(
	struct gpu_array_ref_group *group, __isl_take isl_printer *p)
{
	int global = 0;

	if (group->private_tile)
		p = isl_printer_print_str(p, "private_");
	else if (group->shared_tile)
		p = isl_printer_print_str(p, "shared_");
	else
		global = 1;
	p = isl_printer_print_str(p, group->array->name);
	if (!global && group->array->n_group > 1) {
		p = isl_printer_print_str(p, "_");
		p = isl_printer_print_int(p, group->nr);
	}

	return p;
}

/* Return the union of all read (read = 1) and/or write (write = 1)
 * access relations in the group.
 */
__isl_give isl_union_map *gpu_array_ref_group_access_relation(
	struct gpu_array_ref_group *group, int read, int write)
{
	int i;
	isl_union_map *access;

	access = isl_union_map_empty(isl_map_get_space(group->access));
	for (i = 0; i < group->n_ref; ++i) {
		isl_map *map_i;

		if (!((read && group->refs[i]->read) ||
		     (write && group->refs[i]->write)))
			continue;
		map_i = isl_map_copy(group->refs[i]->access);
		access = isl_union_map_union(access,
					    isl_union_map_from_map(map_i));
	}

	return access;
}

/* Given a constraint
 *
 *		a(p,i) + j = g f(e)
 *
 * or -a(p,i) - j = g f(e) if sign < 0,
 * store a(p,i) in bound->shift and g (stride) in bound->stride.
 * a(p,i) is assumed to be an expression in only the parameters
 * and the input dimensions.
 */
static void extract_stride(__isl_keep isl_constraint *c,
	struct gpu_array_bound *bound, __isl_keep isl_val *stride, int sign)
{
	int i;
	isl_val *v;
	isl_space *space;
	unsigned nparam;
	unsigned nvar;
	isl_aff *aff;

	isl_val_free(bound->stride);
	bound->stride = isl_val_copy(stride);

	space = isl_constraint_get_space(c);
	space = isl_space_domain(space);

	nparam = isl_space_dim(space, isl_dim_param);
	nvar = isl_space_dim(space, isl_dim_set);

	v = isl_constraint_get_constant_val(c);
	if (sign < 0)
		v = isl_val_neg(v);
	aff = isl_aff_zero_on_domain(isl_local_space_from_space(space));
	aff = isl_aff_set_constant_val(aff, v);

	for (i = 0; i < nparam; ++i) {
		if (!isl_constraint_involves_dims(c, isl_dim_param, i, 1))
			continue;
		v = isl_constraint_get_coefficient_val(c, isl_dim_param, i);
		if (sign < 0)
			v = isl_val_neg(v);
		aff = isl_aff_add_coefficient_val(aff, isl_dim_param, i, v);
	}

	for (i = 0; i < nvar; ++i) {
		if (!isl_constraint_involves_dims(c, isl_dim_in, i, 1))
			continue;
		v = isl_constraint_get_coefficient_val(c, isl_dim_in, i);
		if (sign < 0)
			v = isl_val_neg(v);
		aff = isl_aff_add_coefficient_val(aff, isl_dim_in, i, v);
	}

	bound->shift = aff;
}

/* Given an equality constraint of a map with a single output dimension j,
 * check if the constraint is of the form
 *
 *		a(p,i) + j = g f(e)
 *
 * with a(p,i) an expression in the parameters and input dimensions
 * and f(e) an expression in the existentially quantified variables.
 * If so, and if g is larger than any such g from a previously considered
 * constraint, then call extract_stride to record the stride information
 * in bound.
 */
static int check_stride_constraint(__isl_take isl_constraint *c, void *user)
{
	int i;
	isl_ctx *ctx;
	isl_val *v;
	unsigned n_div;
	struct gpu_array_bound *bound = user;

	ctx = isl_constraint_get_ctx(c);
	n_div = isl_constraint_dim(c, isl_dim_div);
	v = isl_constraint_get_coefficient_val(c, isl_dim_out, 0);

	if (n_div && (isl_val_is_one(v) || isl_val_is_negone(v))) {
		int s = isl_val_sgn(v);
		isl_val *stride = isl_val_zero(ctx);

		isl_val_free(v);
		for (i = 0; i < n_div; ++i) {
			v = isl_constraint_get_coefficient_val(c,
								isl_dim_div, i);
			stride = isl_val_gcd(stride, v);
		}
		if (!isl_val_is_zero(stride) &&
		    isl_val_gt(stride, bound->stride))
			extract_stride(c, bound, stride, s);

		isl_val_free(stride);
	} else
		isl_val_free(v);

	isl_constraint_free(c);
	return 0;
}

/* Given contraints on an array index i, check if we can find
 * a shift a(p) and a stride g such that
 *
 *	a(p) + i = 0 mod g
 *
 * If so, record the information in bound and apply the mapping
 * i -> (i + a(p))/g to the array index in bounds and return
 * the new constraints.
 * If not, simply return the original constraints.
 *
 * If bounds is a subset of the space
 *
 *	D -> i
 *
 * then the bound recorded in bound->shift is of the form
 *
 *	D -> s(D)
 *
 * with s(D) equal to a(p) above.
 * Next, we construct a mapping of the form
 *
 *	[D -> i] -> [D -> (i + S(D))/g]
 *
 * This mapping is computed as follows.
 * We first introduce "i" in the domain through precomposition
 * with [D -> i] -> D obtaining
 *
 *	[D -> i] -> s(D)
 *
 * Adding [D -> i] -> i produces
 *
 *	[D -> i] -> i + s(D)
 *
 * and the domain product with [D -> i] -> D yields
 *
 *	[D -> i] -> [D -> i + s(D)]
 *
 * Composition with [D -> i] -> [D -> i/g] gives the desired result.
 */
static __isl_give isl_basic_map *check_stride(struct gpu_array_bound *bound,
	__isl_take isl_basic_map *bounds)
{
	isl_space *space;
	isl_basic_map *hull;
	isl_basic_map *shift, *id, *bmap, *scale;
	isl_basic_set *bset;
	isl_aff *aff;

	bound->stride = NULL;

	hull = isl_basic_map_affine_hull(isl_basic_map_copy(bounds));

	isl_basic_map_foreach_constraint(hull, &check_stride_constraint, bound);

	isl_basic_map_free(hull);

	if (!bound->stride)
		return bounds;

	shift = isl_basic_map_from_aff(isl_aff_copy(bound->shift));
	space = isl_basic_map_get_space(bounds);
	bmap = isl_basic_map_domain_map(isl_basic_map_universe(space));
	shift = isl_basic_map_apply_range(bmap, shift);
	space = isl_basic_map_get_space(bounds);
	id = isl_basic_map_range_map(isl_basic_map_universe(space));
	shift = isl_basic_map_sum(id, shift);
	space = isl_basic_map_get_space(bounds);
	id = isl_basic_map_domain_map(isl_basic_map_universe(space));
	shift = isl_basic_map_range_product(id, shift);

	space = isl_space_domain(isl_basic_map_get_space(bounds));
	id = isl_basic_map_identity(isl_space_map_from_set(space));
	space = isl_space_range(isl_basic_map_get_space(bounds));
	aff = isl_aff_zero_on_domain(isl_local_space_from_space(space));
	aff = isl_aff_add_coefficient_si(aff, isl_dim_in, 0, 1);
	aff = isl_aff_scale_down_val(aff, isl_val_copy(bound->stride));
	scale = isl_basic_map_from_aff(aff);
	scale = isl_basic_map_product(id, scale);

	bmap = isl_basic_map_apply_range(shift, scale);
	bset = isl_basic_set_apply(isl_basic_map_wrap(bounds), bmap);
	bounds = isl_basic_set_unwrap(bset);

	return bounds;
}

/* Data used in compute_array_dim_size and compute_size_in_direction.
 *
 * pos is the position of the variable representing the array index,
 * i.e., the variable for which want to compute the size.  This variable
 * is also the last variable in the set.
 */
struct gpu_size_info {
	isl_basic_set *bset;
	struct gpu_array_bound *bound;
	int pos;
};

/* Given a constraint from the basic set describing the bounds on
 * an array index, check if it is a lower bound, say m i >= b(x), and,
 * if so, check whether the expression "i - ceil(b(x)/m) + 1" has a constant
 * upper bound.  If so, and if this bound is smaller than any bound
 * derived from earlier constraints, set the size to this bound on
 * the expression and the lower bound to ceil(b(x)/m).
 */
static int compute_size_in_direction(__isl_take isl_constraint *c, void *user)
{
	struct gpu_size_info *size = user;
	unsigned nparam;
	unsigned n_div;
	isl_val *v;
	isl_aff *aff;
	isl_aff *lb;

	nparam = isl_basic_set_dim(size->bset, isl_dim_param);
	n_div = isl_constraint_dim(c, isl_dim_div);

	if (isl_constraint_involves_dims(c, isl_dim_div, 0, n_div) ||
	    !isl_constraint_is_lower_bound(c, isl_dim_set, size->pos)) {
		isl_constraint_free(c);
		return 0;
	}

	aff = isl_constraint_get_bound(c, isl_dim_set, size->pos);
	aff = isl_aff_ceil(aff);

	lb = isl_aff_copy(aff);

	aff = isl_aff_neg(aff);
	aff = isl_aff_add_coefficient_si(aff, isl_dim_in, size->pos, 1);

	v = isl_basic_set_max_val(size->bset, aff);
	isl_aff_free(aff);

	if (isl_val_is_int(v)) {
		v = isl_val_add_ui(v, 1);
		if (!size->bound->size || isl_val_lt(v, size->bound->size)) {
			isl_val_free(size->bound->size);
			size->bound->size = isl_val_copy(v);
			lb = isl_aff_drop_dims(lb, isl_dim_in, size->pos, 1);
			isl_aff_free(size->bound->lb);
			size->bound->lb = isl_aff_copy(lb);
		}
	}
	isl_val_free(v);
	isl_aff_free(lb);

	isl_constraint_free(c);

	return 0;
}

/* Given a basic map "bounds" that maps parameters and input dimensions
 * to a single output dimension, look for an expression in the parameters
 * and input dimensions such that the range of the output dimension shifted
 * by this expression is a constant.
 *
 * In particular, we currently only consider lower bounds on the output
 * dimension as candidate expressions.
 */
static int compute_array_dim_size(struct gpu_array_bound *bound,
	__isl_take isl_basic_map *bounds)
{
	struct gpu_size_info size;

	bounds = isl_basic_map_detect_equalities(bounds);
	bounds = check_stride(bound, bounds);

	bound->size = NULL;
	bound->lb = NULL;

	size.bound = bound;
	size.pos = isl_basic_map_dim(bounds, isl_dim_in);
	size.bset = isl_basic_map_wrap(bounds);
	size.bset = isl_basic_set_flatten(size.bset);
	size.bset = isl_set_simple_hull(isl_basic_set_compute_divs(size.bset));
	isl_basic_set_foreach_constraint(size.bset, &compute_size_in_direction,
					&size);
	isl_basic_set_free(size.bset);

	return bound->size ? 0 : -1;
}

/* Check if we can find a memory tile for the given array
 * based on the given accesses, and if so, put the results in "tile".
 *
 * We project the accesses on each index in turn and look for a parametric
 * offset such that the size is constant.
 */
static int can_tile(__isl_keep isl_map *access, struct gpu_array_tile *tile)
{
	int i;

	for (i = 0; i < tile->n; ++i) {
		isl_map *access_i;
		isl_basic_map *hull;

		access_i = isl_map_copy(access);
		access_i = isl_map_project_out(access_i, isl_dim_out, 0, i);
		access_i = isl_map_project_out(access_i, isl_dim_out,
					    1, tile->n - (i + 1));
		access_i = isl_map_compute_divs(access_i);
		hull = isl_map_simple_hull(access_i);
		if (compute_array_dim_size(&tile->bound[i], hull) < 0)
			return 0;
	}

	return 1;
}

/* Construct a map from domain_dim to domain_dim that increments
 * the dimension at position "pos" and leaves all other dimensions
 * constant.
 */
static __isl_give isl_map *next(__isl_take isl_space *domain_dim, int pos)
{
	int i;
	int len = isl_space_dim(domain_dim, isl_dim_set);
	isl_space *dim;
	isl_basic_map *next;
	isl_local_space *ls;

	dim = isl_space_map_from_set(domain_dim);
	next = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len; ++i) {
		isl_constraint *c;

		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, i, 1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
		if (i == pos)
			c = isl_constraint_set_constant_si(c, 1);
		next = isl_basic_map_add_constraint(next, c);
	}

	isl_local_space_free(ls);

	return isl_map_from_basic_map(next);
}

/* Check if the given access is coalesced.
 * That is, check whether incrementing the dimension that will get
 * wrapped over the last thread index results in incrementing
 * the last array index.
 *
 * This function is only called for access relations without reuse and
 * kernels with at least one block dimension.
 */
static int access_is_coalesced(struct gpu_gen *gen,
	__isl_keep isl_union_map *access)
{
	isl_space *dim;
	isl_map *access_map;
	isl_map *next_thread_x;
	isl_map *next_element;
	isl_map *map;
	int coalesced;

	access = isl_union_map_copy(access);
	access = isl_union_map_apply_domain(access,
				isl_union_map_copy(gen->tiled_sched));
	access_map = isl_map_from_union_map(access);

	dim = isl_map_get_space(access_map);
	dim = isl_space_domain(dim);
	next_thread_x = next(dim, gen->shared_len + gen->n_block - 1);

	dim = isl_map_get_space(access_map);
	dim = isl_space_range(dim);
	next_element = next(dim, isl_space_dim(dim, isl_dim_set) - 1);

	map = isl_map_apply_domain(next_thread_x, isl_map_copy(access_map));
	map = isl_map_apply_range(map, access_map);

	coalesced = isl_map_is_subset(map, next_element);

	isl_map_free(next_element);
	isl_map_free(map);

	return coalesced;
}

/* Given an access relation in terms of the first gen->shared_len + gen->n_block
 * dimensions of the computed schedule, check if it is bijective for
 * fixed values of the first gen->shared_len dimensions.
 * We perform this check by equating these dimensions to parameters.
 */
static int access_is_bijective(struct gpu_gen *gen, __isl_keep isl_map *access)
{
	int res;
	isl_set *par;
	isl_space *space;
	isl_id_list *ids;

	access = isl_map_copy(access);
	space = isl_space_params(isl_map_get_space(access));
	ids = ppcg_scop_generate_names(gen->prog->scop, gen->shared_len, "s");
	par = parametrization(space, gen->shared_len + gen->n_block, 0, ids);
	isl_id_list_free(ids);
	access = isl_map_intersect_domain(access, par);
	res = isl_map_is_bijective(access);
	isl_map_free(access);

	return res;
}

/* Look for the last shared tile loop that affects the offset of "tile"
 * and return the result.
 * If there is no such loop, then return the index of the loop
 * before the first shared tile loop, in particular gen->tile_first - 1.
 */
static int compute_tile_last_shared(struct gpu_gen *gen,
	struct gpu_array_tile *tile)
{
	int i, j;

	for (j = gen->shared_len - 1; j >= gen->tile_first; --j) {
		for (i = 0; i < tile->n; ++i) {
			isl_aff *lb;
			isl_aff *shift;

			lb = tile->bound[i].lb;
			if (isl_aff_involves_dims(lb, isl_dim_in, j, 1))
				break;

			shift = tile->bound[i].shift;
			if (!shift)
				continue;
			if (isl_aff_involves_dims(shift, isl_dim_in, j, 1))
				break;
		}
		if (i < tile->n)
			break;
	}

	return j;
}

/* Look for the last shared tile loop that affects the offset of the
 * shared or private tile and store the result in group->last_shared.
 * If there is no such loop, then group->last_shared is set to a value
 * before the first shared tile loop, in particular gen->tile_first - 1.
 * If there is no tile defined on the array reference group,
 * then set group->last_shared to gen->shared_len - 1.
 */
static void set_last_shared(struct gpu_gen *gen,
	struct gpu_array_ref_group *group)
{
	struct gpu_array_tile *tile;

	group->last_shared = gen->shared_len - 1;

	tile = group->private_tile;
	if (!tile)
		tile = group->shared_tile;
	if (!tile)
		return;

	group->last_shared = compute_tile_last_shared(gen, tile);
}

/* Fill up the groups array with singleton groups, i.e., one group
 * per reference, initializing the array, access, write, n_ref and refs fields.
 * In particular the access field is initialized to the scheduled
 * access relation of the array reference.
 *
 * Return the number of elements initialized, i.e., the number of
 * active references in the current kernel.
 */
static int populate_array_references(struct gpu_array_info *array,
	__isl_keep isl_union_map *sched, struct gpu_array_ref_group **groups)
{
	int i;
	int n;
	isl_ctx *ctx = isl_union_map_get_ctx(sched);

	n = 0;
	for (i = 0; i < array->n_ref; ++i) {
		isl_union_map *umap;
		isl_map *map;
		struct gpu_array_ref_group *group;
		struct gpu_stmt_access *access = array->refs[i];

		map = isl_map_copy(access->access);
		umap = isl_union_map_from_map(map);
		umap = isl_union_map_apply_domain(umap,
				isl_union_map_copy(sched));

		if (isl_union_map_is_empty(umap)) {
			isl_union_map_free(umap);
			continue;
		}

		map = isl_map_from_union_map(umap);
		map = isl_map_detect_equalities(map);

		group = isl_calloc_type(ctx, struct gpu_array_ref_group);
		if (!group)
			return -1;
		group->array = array;
		group->access = map;
		group->write = access->write;
		group->exact_write = access->exact_write;
		group->slice = access->n_index < array->n_index;
		group->refs = &array->refs[i];
		group->n_ref = 1;

		groups[n++] = group;
	}

	return n;
}

/* If group->n_ref == 1, then group->refs was set by
 * populate_array_references to point directly into
 * group->array->refs and should not be freed.
 * If group->n_ref > 1, then group->refs was set by join_groups
 * to point to a newly allocated array.
 */
struct gpu_array_ref_group *gpu_array_ref_group_free(
	struct gpu_array_ref_group *group)
{
	if (!group)
		return NULL;
	gpu_array_tile_free(group->shared_tile);
	gpu_array_tile_free(group->private_tile);
	isl_map_free(group->access);
	if (group->n_ref > 1)
		free(group->refs);
	free(group);
	return NULL;
}

/* Given a map where the input dimensions represent the tile loops,
 * eliminate the innermost of those that have a fixed value
 * until we reach one that does not (obviously) have a fixed value.
 */
static __isl_give isl_map *eliminate_fixed_inner_loops(
	__isl_take isl_map *access)
{
	int i, n;

	n = isl_map_dim(access, isl_dim_in);

	for (i = n - 1; i >= 0; --i) {
		if (!map_plain_is_fixed(access, isl_dim_in, i))
			break;
		access = isl_map_eliminate(access, isl_dim_in, i, 1);
	}
	return access;
}

/* Check if the access relations of group1 and group2 overlap within
 * the innermost loop.  In particular, ignore any inner dimension
 * with a fixed value.
 * The copying to and from shared memory will be performed within
 * the innermost actual loop so we are only allowed to consider
 * the dimensions up to that innermost loop while checking whether
 * two access relations overlap.
 */
static int accesses_overlap(struct gpu_array_ref_group *group1,
	struct gpu_array_ref_group *group2)
{
	int empty;
	isl_map *access1, *access2;

	access1 = isl_map_copy(group1->access);
	access1 = eliminate_fixed_inner_loops(access1);
	access2 = isl_map_copy(group2->access);
	access2 = eliminate_fixed_inner_loops(access2);
	access1 = isl_map_intersect(access1, access2);
	empty = isl_map_is_empty(access1);
	isl_map_free(access1);

	return !empty;
}

/* Combine the given two groups into a single group, containing
 * the references of both groups.
 */
static struct gpu_array_ref_group *join_groups(
	struct gpu_array_ref_group *group1,
	struct gpu_array_ref_group *group2)
{
	int i;
	isl_ctx *ctx;
	struct gpu_array_ref_group *group;

	ctx = isl_map_get_ctx(group1->access);
	group = isl_calloc_type(ctx, struct gpu_array_ref_group);
	assert(group);
	group->array = group1->array;
	group->access = isl_map_union(isl_map_copy(group1->access),
					isl_map_copy(group2->access));
	group->write = group1->write || group2->write;
	group->exact_write = group1->exact_write && group2->exact_write;
	group->slice = group1->slice || group2->slice;
	group->n_ref = group1->n_ref + group2->n_ref;
	group->refs = isl_alloc_array(ctx, struct gpu_stmt_access *,
					group->n_ref);
	assert(group->refs);
	for (i = 0; i < group1->n_ref; ++i)
		group->refs[i] = group1->refs[i];
	for (i = 0; i < group2->n_ref; ++i)
		group->refs[group1->n_ref + i] = group2->refs[i];

	return group;
}

/* Combine the given two groups into a single group and free
 * the original two groups.
 */
static struct gpu_array_ref_group *join_groups_and_free(
	struct gpu_array_ref_group *group1,
	struct gpu_array_ref_group *group2)
{
	struct gpu_array_ref_group *group;

	group = join_groups(group1, group2);
	gpu_array_ref_group_free(group1);
	gpu_array_ref_group_free(group2);
	return group;
}

/* Report that the array reference group with the given access relation
 * is not mapped to shared memory in the given kernel because
 * it does not exhibit any reuse and is considered to be coalesced.
 */
static void report_no_reuse_and_coalesced(struct ppcg_kernel *kernel,
	__isl_keep isl_union_map *access)
{
	isl_ctx *ctx;
	isl_printer *p;

	ctx = isl_union_map_get_ctx(access);
	p = isl_printer_to_file(ctx, stdout);
	p = isl_printer_print_str(p, "Array reference group ");
	p = isl_printer_print_union_map(p, access);
	p = isl_printer_print_str(p,
	    " not considered for mapping to shared memory in kernel");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p,
	    " because it exhibits no reuse and is considered to be coalesced");
	p = isl_printer_end_line(p);
	isl_printer_free(p);
}

/* Compute the private and/or shared memory tiles for the array
 * reference group "group" of array "array".
 * Return 0 on success and -1 on error.
 *
 * If the array is a read-only scalar or if the user requested
 * not to use shared or private memory, then we do not need to do anything.
 *
 * If any reference in the reference group accesses more than one element,
 * then we would have to make sure that the layout in shared memory
 * is the same as that in global memory.  Since we do not handle this yet
 * (and it may not even be possible), we refuse to map to private or
 * shared memory in such cases.
 *
 * If the array group involves any may writes (that are not must writes),
 * then we would have to make sure that we load the data into shared/private
 * memory first in case the data is not written by the kernel
 * (but still written back out to global memory).
 * Since we don't have any such mechanism at the moment, we don't
 * compute shared/private tiles for groups involving may writes.
 *
 * We only try to compute a shared memory tile if there is any reuse
 * or if the access is not coalesced.
 *
 * For computing a private memory tile, we also require that there is
 * some reuse.  Moreover, we require that the access is private
 * to the thread.  That is, we check that any given array element
 * is only accessed by a single thread.
 * We compute an access relation that maps the shared tile loop iterators
 * and the shared point loop iterators that will be wrapped over the
 * threads to the array elements.
 * We actually check that those iterators that will be wrapped
 * partition the array space.  This check is stricter than necessary
 * since several iterations may be mapped onto the same thread
 * and then they could be allowed to access the same memory elements,
 * but our check does not allow this situation.
 *
 * We also check that the index expression only depends on parallel
 * loops.  That way, we can move those loops innermost and unroll them.
 * Again, we use a test that is stricter than necessary.
 * We actually check whether the index expression only depends
 * on the iterators that are wrapped over the threads.
 * These are necessarily parallel, but there may be more parallel loops.
 *
 * Combining the injectivity of the first test with the single-valuedness
 * of the second test, we simply test for bijectivity.
 *
 * If the array is marked force_private, then we bypass all checks
 * and assume we can (and should) use registers.
 *
 * If it turns out we can (or have to) use registers, we compute
 * the private memory tile size using can_tile, after introducing a dependence
 * on the thread indices.
 */
static int compute_group_bounds_core(struct gpu_gen *gen,
	struct gpu_array_ref_group *group)
{
	isl_ctx *ctx = isl_space_get_ctx(group->array->space);
	isl_union_map *access;
	int n_index = group->array->n_index;
	int no_reuse, coalesced;
	isl_map *acc;
	int force_private = group->array->force_private;
	int use_shared = gen->options->use_shared_memory && gen->n_block > 0;
	int use_private = force_private || gen->options->use_private_memory;

	if (!use_shared && !use_private)
		return 0;
	if (gpu_array_is_read_only_scalar(group->array))
		return 0;
	if (!force_private && !group->exact_write)
		return 0;
	if (group->slice)
		return 0;

	access = gpu_array_ref_group_access_relation(group, 1, 1);
	no_reuse = isl_union_map_is_injective(access);
	if (use_shared && no_reuse)
		coalesced = access_is_coalesced(gen, access);

	if (gen->options->debug->verbose && use_shared && no_reuse && coalesced)
		report_no_reuse_and_coalesced(gen->kernel, access);

	if (use_shared && (!no_reuse || !coalesced)) {
		group->shared_tile = gpu_array_tile_create(ctx,
							group->array->n_index);
		assert(group->shared_tile);
		if (!can_tile(group->access, group->shared_tile))
			group->shared_tile =
					gpu_array_tile_free(group->shared_tile);
	}

	if (!force_private && (!use_private || no_reuse)) {
		isl_union_map_free(access);
		return 0;
	}

	access = isl_union_map_apply_domain(access,
					isl_union_map_copy(gen->shared_sched));

	acc = isl_map_from_union_map(access);

	if (!force_private && !access_is_bijective(gen, acc)) {
		isl_map_free(acc);
		return 0;
	}

	group->private_tile = gpu_array_tile_create(gen->ctx, n_index);
	assert(group->private_tile);
	acc = isl_map_apply_domain(acc, isl_map_copy(gen->privatization));
	if (!can_tile(acc, group->private_tile))
		group->private_tile = gpu_array_tile_free(group->private_tile);

	isl_map_free(acc);

	if (force_private && !group->private_tile)
		isl_die(ctx, isl_error_internal,
			"unable to map array reference group to registers",
			return -1);

	return 0;
}

/* Compute the private and/or shared memory tiles for the array
 * reference group "group" of array "array" and set last_shared.
 * Return 0 on success and -1 on error.
 */
static int compute_group_bounds(struct gpu_gen *gen,
	struct gpu_array_ref_group *group)
{
	if (compute_group_bounds_core(gen, group) < 0)
		return -1;
	set_last_shared(gen, group);

	return 0;
}

/* If two groups have overlapping access relations (as determined by
 * the "overlap" function) and if one of them involves a write,
 * then merge the two groups into one.
 * If "compute_bounds" is set, then call compute_group_bounds
 * on the merged groups.
 *
 * Return the updated number of groups.
 * Return -1 on error.
 */
static int group_writes(struct gpu_gen *gen,
	int n, struct gpu_array_ref_group **groups,
	int (*overlap)(struct gpu_array_ref_group *group1,
		struct gpu_array_ref_group *group2), int compute_bounds)
{
	int i, j;

	for (i = 0; i < n; ++i) {
		for (j = n - 1; j > i; --j) {
			if (!groups[i]->write && !groups[j]->write)
				continue;

			if (!overlap(groups[i], groups[j]))
				continue;

			groups[i] = join_groups_and_free(groups[i], groups[j]);
			if (j != n - 1)
				groups[j] = groups[n - 1];
			groups[n - 1] = NULL;
			n--;

			if (compute_bounds &&
			    compute_group_bounds(gen, groups[i]) < 0)
				return -1;
		}
	}

	return n;
}

/* If two groups have overlapping access relations (within the innermost
 * loop) and if one of them involves a write, then merge the two groups
 * into one.
 *
 * Return the updated number of groups.
 */
static int group_overlapping_writes(struct gpu_gen *gen,
	int n, struct gpu_array_ref_group **groups)
{
	return group_writes(gen, n, groups, &accesses_overlap, 0);
}

/* Check if the access relations of group1 and group2 overlap within
 * the outermost min(group1->last_shared, group2->last_shared) loops.
 */
static int last_shared_accesses_overlap(struct gpu_array_ref_group *group1,
	struct gpu_array_ref_group *group2)
{
	int last_shared;
	int dim;
	int empty;
	isl_map *map_i, *map_j, *map;

	last_shared = group1->last_shared;
	if (group2->last_shared < last_shared)
		last_shared = group2->last_shared;
	map_i = isl_map_copy(group1->access);
	dim = isl_map_dim(map_i, isl_dim_in);
	map_i = isl_map_eliminate(map_i, isl_dim_in,
				last_shared + 1, dim - (last_shared + 1));
	map_j = isl_map_copy(group2->access);
	map_j = isl_map_eliminate(map_j, isl_dim_in,
				last_shared + 1, dim - (last_shared + 1));
	map = isl_map_intersect(map_i, map_j);
	empty = isl_map_is_empty(map);
	isl_map_free(map);

	return !empty;
}

/* If two groups have overlapping access relations (within the outer
 * last_shared loops) and if one of them involves a write,
 * then merge the two groups into one.
 *
 * Return the updated number of groups.
 */
static int group_last_shared_overlapping_writes(struct gpu_gen *gen, int n,
	struct gpu_array_ref_group **groups)
{
	return group_writes(gen, n, groups, &last_shared_accesses_overlap, 1);
}

/* Is the size of the tile specified by "tile" smaller than the sum of
 * the sizes of the tiles specified by "tile1" and "tile2"?
 */
static int smaller_tile(struct gpu_array_tile *tile,
	struct gpu_array_tile *tile1, struct gpu_array_tile *tile2)
{
	int smaller;
	isl_val *size, *size1, *size2;

	size = gpu_array_tile_size(tile);
	size1 = gpu_array_tile_size(tile1);
	size2 = gpu_array_tile_size(tile2);

	size = isl_val_sub(size, size1);
	size = isl_val_sub(size, size2);
	smaller = isl_val_is_neg(size);

	isl_val_free(size);

	return smaller;
}

/* Given an initial grouping of array references and shared memory tiles
 * for each group that allows for a shared memory tile, merge two groups
 * if both have a shared memory tile, the merged group also has
 * a shared memory tile and the size of the tile for the merge group
 * is smaller than the sum of the tile sizes of the individual groups.
 *
 * If merging two groups decreases the "last_shared" dimension of
 * one or both of the two groups, then we need to check for overlapping
 * writes again.
 *
 * Return the number of groups after merging.
 * Return -1 on error.
 */
static int group_common_shared_memory_tile(struct gpu_gen *gen,
	struct gpu_array_info *array, int n,
	struct gpu_array_ref_group **groups)
{
	int i, j;
	int recompute_overlap = 0;
	isl_ctx *ctx = isl_space_get_ctx(array->space);

	for (i = 0; i < n; ++i) {
		if (!groups[i]->shared_tile)
			continue;
		for (j = n - 1; j > i; --j) {
			isl_map *map;
			int empty;
			struct gpu_array_ref_group *group;

			if (!groups[j]->shared_tile)
				continue;

			map = isl_map_intersect(isl_map_copy(groups[i]->access),
					    isl_map_copy(groups[j]->access));
			empty = isl_map_is_empty(map);
			isl_map_free(map);

			if (empty)
				continue;

			group = join_groups(groups[i], groups[j]);
			if (compute_group_bounds(gen, group) < 0) {
				gpu_array_ref_group_free(group);
				return -1;
			}
			if (!group->shared_tile ||
			    !smaller_tile(group->shared_tile,
					groups[i]->shared_tile,
					groups[j]->shared_tile)) {
				gpu_array_ref_group_free(group);
				continue;
			}

			if (group->last_shared < groups[i]->last_shared ||
			    group->last_shared < groups[j]->last_shared)
				recompute_overlap = 1;
			gpu_array_ref_group_free(groups[i]);
			gpu_array_ref_group_free(groups[j]);
			groups[i] = group;
			if (j != n - 1)
				groups[j] = groups[n - 1];
			n--;
		}
	}

	if (recompute_overlap)
		n = group_last_shared_overlapping_writes(gen, n, groups);
	return n;
}

/* Set array->n_group and array->groups to n and groups.
 *
 * Additionally, set the "nr" field of each group
 * and the "group" field of each reference in each group.
 */
static void set_array_groups(struct gpu_array_info *array,
	int n, struct gpu_array_ref_group **groups)
{
	int i, j;

	array->n_group = n;
	array->groups = groups;

	for (i = 0; i < n; ++i) {
		groups[i]->nr = i;

		for (j = 0; j < groups[i]->n_ref; ++j)
			groups[i]->refs[j]->group = i;
	}
}

/* Group array references that should be considered together when
 * deciding whether to access them from private, shared or global memory.
 * Return -1 on error.
 *
 * In particular, if two array references overlap and if one of them
 * is a write, then the two references are grouped together.
 * We first perform an initial grouping based only on the access relation.
 * After computing shared and private memory tiles, we check for
 * overlapping writes again, but this time taking into account
 * the "last_shared" property.
 *
 * Furthermore, if two groups admit a shared memory tile and if the
 * combination of the two also admits a shared memory tile, we merge
 * the two groups.
 *
 * If the array contains structures, then there is no need to compute
 * reference groups since we do not map such arrays to private or shared
 * memory.
 */
static int group_array_references(struct gpu_gen *gen,
	struct gpu_array_info *array, __isl_keep isl_union_map *sched)
{
	int i;
	int n;
	isl_ctx *ctx = isl_union_map_get_ctx(sched);
	struct gpu_array_ref_group **groups;

	if (array->has_compound_element)
		return 0;

	groups = isl_calloc_array(ctx, struct gpu_array_ref_group *,
					array->n_ref);
	if (!groups)
		return -1;

	n = populate_array_references(array, sched, groups);

	n = group_overlapping_writes(gen, n, groups);

	for (i = 0; i < n; ++i)
		if (compute_group_bounds(gen, groups[i]) < 0)
			n = -1;

	n = group_last_shared_overlapping_writes(gen, n, groups);

	n = group_common_shared_memory_tile(gen, array, n, groups);

	set_array_groups(array, n, groups);

	if (n >= 0)
		return 0;

	for (i = 0; i < array->n_ref; ++i)
		gpu_array_ref_group_free(groups[i]);
	return -1;
}

/* For each scalar in the input program, check if there are any
 * order dependences active inside the current kernel, within
 * the same iteration of the host schedule.
 * If so, mark the scalar as force_private so that it will be
 * mapped to a register.
 */
static void check_scalar_live_ranges(struct gpu_gen *gen)
{
	int i;
	isl_map *proj;
	isl_union_map *sched;
	isl_union_set *domain;
	isl_union_map *same_host_iteration;

	gen->any_force_private = 0;

	if (!gen->options->live_range_reordering)
		return;

	sched = gen->shared_sched;
	sched = isl_union_map_universe(isl_union_map_copy(sched));
	domain = isl_union_map_domain(sched);

	sched = isl_union_map_copy(gen->sched);
	proj = projection(isl_union_map_get_space(sched),
			    gen->untiled_len, gen->tile_first);
	sched = isl_union_map_apply_range(sched, isl_union_map_from_map(proj));
	same_host_iteration = isl_union_map_apply_range(sched,
			    isl_union_map_reverse(isl_union_map_copy(sched)));

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];
		isl_union_map *order;

		array->force_private = 0;
		if (array->n_index != 0)
			continue;
		order = isl_union_map_copy(array->dep_order);
		order = isl_union_map_intersect_domain(order,
						    isl_union_set_copy(domain));
		order = isl_union_map_intersect_range(order,
						    isl_union_set_copy(domain));
		order = isl_union_map_intersect(order,
				    isl_union_map_copy(same_host_iteration));
		if (!isl_union_map_is_empty(order)) {
			array->force_private = 1;
			gen->any_force_private = 1;
		}
		isl_union_map_free(order);
	}

	isl_union_map_free(same_host_iteration);
	isl_union_set_free(domain);
}

/* Group references of all arrays in the program.
 */
int gpu_group_references(struct gpu_gen *gen)
{
	int i;
	int r = 0;
	isl_union_map *sched;

	check_scalar_live_ranges(gen);

	sched = isl_union_map_apply_range(isl_union_map_copy(gen->shared_sched),
					  isl_union_map_copy(gen->shared_proj));

	for (i = 0; i < gen->prog->n_array; ++i) {
		r = group_array_references(gen, &gen->prog->array[i], sched);
		if (r < 0)
			break;
	}

	isl_union_map_free(sched);

	return r;
}

/* Given a description of an array tile "tile" and the "space"
 *
 *	{ D -> A }
 *
 * where D represents the first shared_len schedule dimensions
 * and A represents the array, construct an isl_multi_aff
 *
 *	{ [D[i] -> A[a]] -> A'[a'] }
 *
 * with A' a scaled down copy of A according to the shifts and strides
 * in "tile".  In particular,
 *
 *	a' = (a + shift(i))/stride
 *
 * "insert_array" represents
 *
 *	{ [D -> A] -> D }
 *
 * and is used to insert A into the domain of functions that only
 * reference D.
 */
static __isl_give isl_multi_aff *strided_tile(
	struct gpu_array_tile *tile, __isl_keep isl_space *space,
	__isl_keep isl_multi_aff *insert_array)
{
	int i;
	isl_ctx *ctx;
	isl_multi_aff *shift;
	isl_multi_val *stride;
	isl_space *space2;
	isl_local_space *ls;
	isl_multi_aff *tiling;

	ctx = isl_space_get_ctx(space);
	space2 = isl_space_domain(isl_space_copy(space));
	ls = isl_local_space_from_space(space2);
	space2 = isl_space_range(isl_space_copy(space));
	stride = isl_multi_val_zero(space2);
	shift = isl_multi_aff_zero(isl_space_copy(space));

	for (i = 0; i < tile->n; ++i) {
		struct gpu_array_bound *bound = &tile->bound[i];
		isl_val *stride_i;
		isl_aff *shift_i;

		if (tile->bound[i].shift) {
			stride_i = isl_val_copy(bound->stride);
			shift_i = isl_aff_copy(bound->shift);
		} else {
			stride_i = isl_val_one(ctx);
			shift_i = isl_aff_zero_on_domain(
					isl_local_space_copy(ls));
		}

		stride = isl_multi_val_set_val(stride, i, stride_i);
		shift = isl_multi_aff_set_aff(shift, i, shift_i);
	}
	isl_local_space_free(ls);

	shift = isl_multi_aff_pullback_multi_aff(shift,
				    isl_multi_aff_copy(insert_array));

	tiling = isl_multi_aff_range_map(isl_space_copy(space));
	tiling = isl_multi_aff_add(tiling, shift);
	tiling = isl_multi_aff_scale_down_multi_val(tiling, stride);

	return tiling;
}

/* Compute a tiling for the array reference group "group".
 *
 * The tiling is of the form
 *
 *	{ [D[i] -> A[a]] -> T[t] }
 *
 * where D represents the first shared_len schedule dimensions,
 * A represents the global array and T represents the shared or
 * private memory tile.  The name of T is the name of the local
 * array.
 *
 * If there is any stride in the accesses, then the mapping is
 *
 *	t = (a + shift(i))/stride - lb(i)
 *
 * otherwise, it is simply
 *
 *	t = a - lb(i)
 */
void gpu_array_ref_group_compute_tiling(struct gpu_array_ref_group *group)
{
	int i;
	struct gpu_array_tile *tile;
	struct gpu_array_info *array = group->array;
	isl_space *space;
	isl_multi_aff *tiling, *lb, *insert_array;
	isl_printer *p;
	char *local_name;

	tile = group->private_tile;
	if (!tile)
		tile = group->shared_tile;
	if (!tile)
		return;

	space = isl_map_get_space(group->access);
	insert_array = isl_multi_aff_domain_map(isl_space_copy(space));

	for (i = 0; i < tile->n; ++i)
		if (tile->bound[i].shift)
			break;

	if (i < tile->n)
		tiling = strided_tile(tile, space, insert_array);
	else
		tiling = isl_multi_aff_range_map(isl_space_copy(space));

	lb = isl_multi_aff_zero(space);
	for (i = 0; i < tile->n; ++i) {
		isl_aff *lb_i = isl_aff_copy(tile->bound[i].lb);
		lb = isl_multi_aff_set_aff(lb, i, lb_i);
	}
	lb = isl_multi_aff_pullback_multi_aff(lb, insert_array);

	tiling = isl_multi_aff_sub(tiling, lb);

	p = isl_printer_to_str(isl_multi_aff_get_ctx(tiling));
	p = gpu_array_ref_group_print_name(group, p);
	local_name = isl_printer_get_str(p);
	isl_printer_free(p);
	tiling = isl_multi_aff_set_tuple_name(tiling, isl_dim_out, local_name);
	free(local_name);

	tile->tiling = tiling;
}
