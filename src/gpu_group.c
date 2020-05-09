/*
 * Copyright 2010-2011 INRIA Saclay
 * Copyright 2012-2014 Ecole Normale Superieure
 * Copyright 2015      Sven Verdoolaege
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 * and Ecole Normale Superieure, 45 rue d'Ulm, 75230 Paris, France
 */

#include <isl/aff.h>
#include <isl/map.h>
#include <isl/constraint.h>

#include "gpu_array_tile.h"
#include "gpu_group.h"
#include "gpu_tree.h"
#include "schedule.h"

/* Print the name of the local copy of a given group of array references.
 */
__isl_give isl_printer *gpu_array_ref_group_print_name(
	struct gpu_array_ref_group *group, __isl_take isl_printer *p)
{
	int global = 0;
	enum ppcg_group_access_type type;

	type = gpu_array_ref_group_type(group);
	if (type == ppcg_access_private)
		p = isl_printer_print_str(p, "private_");
	else if (type == ppcg_access_shared)
		p = isl_printer_print_str(p, "shared_");
	else
		global = 1;
	p = isl_printer_print_str(p, group->array->name);
	if (!global && group->local_array->n_group > 1) {
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

/* Should this array reference group be mapped to private, shared or global
 * memory?
 * If we have computed both a private and a shared tile, then
 * the tile with the smallest depth is used.  If both have the same depth,
 * then the private tile is used.
 */
enum ppcg_group_access_type gpu_array_ref_group_type(
	struct gpu_array_ref_group *group)
{
	if (group->private_tile && group->shared_tile &&
	    group->shared_tile->depth < group->private_tile->depth)
		return ppcg_access_shared;
	if (group->private_tile)
		return ppcg_access_private;
	if (group->shared_tile)
		return ppcg_access_shared;
	return ppcg_access_global;
}


/* Return the effective gpu_array_tile associated to "group" or
 * NULL if there is no such gpu_array_tile.
 */
struct gpu_array_tile *gpu_array_ref_group_tile(
	struct gpu_array_ref_group *group)
{
	switch (gpu_array_ref_group_type(group)) {
	case ppcg_access_global:
		return NULL;
	case ppcg_access_shared:
		return group->shared_tile;
	case ppcg_access_private:
		return group->private_tile;
	}
}

/* Does the tile associated to "group" require unrolling of the schedule
 * dimensions mapped to threads?
 * Note that this can only happen for private tiles.
 */
int gpu_array_ref_group_requires_unroll(struct gpu_array_ref_group *group)
{
	struct gpu_array_tile *tile;

	tile = gpu_array_ref_group_tile(group);
	if (!tile)
		return 0;
	return tile->requires_unroll;
}

/* Given an array access "access", check if for any index i there is
 * a shift a(p) and a stride g such that
 *
 *	a(p) + i = 0 mod g
 *
 * If so, record the information in tile->bound[i]->stride and
 * tile->bound[i]->shift.
 * Otherwise, set tile->bound[i]->stride to 1 (and tile->bound[i]->shift to 0).
 * Return isl_bool_true if any non-trivial stride was found.
 *
 * Note that the stride info returned by isl_map_get_range_stride_info
 * is of the form
 *
 *	i = o(p) + g n
 *
 * a(p) can therefore be taken to be equal to -o(p).
 */
static isl_bool detect_strides(struct gpu_array_tile *tile,
	__isl_keep isl_map *access)
{
	int i;
	isl_bool has_strides = isl_bool_false;

	for (i = 0; i < tile->n; ++i) {
		struct gpu_array_bound *bound = &tile->bound[i];
		isl_stride_info *si;

		si = isl_map_get_range_stride_info(access, i);
		bound->stride = isl_stride_info_get_stride(si);
		bound->shift = isl_aff_neg(isl_stride_info_get_offset(si));
		isl_stride_info_free(si);

		if (!has_strides)
			has_strides = isl_val_gt_si(bound->stride, 1);
		if (has_strides < 0)
			return isl_bool_error;
	}

	return has_strides;
}

/* Given an array access "access", remove the strides based
 * on the information in tile->bound[i]->stride and tile->bound[i]->shift.
 *
 * In particular let the access be A[a] and
 * let the shifts s_i(p) and the strides g_i be such that
 *
 *  S(p) + a = 0 mod G
 *
 * Replace the access by
 *
 *  A[(a + S(p))/G]
 *
 * First collect the shifts s_i into an isl_multi_aff and
 * the strides into the scaling function A[i] -> A[G i].
 * Then add the shifts to the original access and
 * take the preimage over the scaling.
 */
static __isl_give isl_map *remove_strides(__isl_take isl_map *access,
	struct gpu_array_tile *tile)
{
	int i;
	isl_space *space;
	isl_multi_aff *shift, *scale;
	isl_multi_val *stride;

	space = isl_map_get_space(access);
	shift = isl_multi_aff_zero(isl_space_copy(space));
	space = isl_space_range(space);
	stride = isl_multi_val_zero(isl_space_copy(space));
	scale = isl_multi_aff_identity(isl_space_map_from_set(space));
	for (i = 0; i < tile->n; ++i) {
		struct gpu_array_bound *bound = &tile->bound[i];
		isl_aff *shift_i;
		isl_val *stride_i;

		shift_i = isl_aff_copy(bound->shift);
		stride_i = isl_val_copy(bound->stride);
		shift = isl_multi_aff_set_aff(shift, i, shift_i);
		stride = isl_multi_val_set_val(stride, i, stride_i);
	}
	scale = isl_multi_aff_scale_multi_val(scale, stride);

	access = isl_map_sum(access, isl_map_from_multi_aff(shift));
	access = isl_map_preimage_range_multi_aff(access, scale);

	return access;
}

/* Check if we can find a memory tile for the given array
 * based on the given accesses, and if so, put the results in "tile".
 *
 * We project the accesses on each index in turn and look for a parametric
 * offset such that the size is constant, after removing
 * any stride that may appear in the accesses.
 *
 * tile->depth is initialized to the input dimension of the computed bounds.
 */
static isl_bool can_tile(__isl_keep isl_map *access,
	struct gpu_array_tile *tile)
{
	int i;
	isl_bool has_strides, valid;
	isl_fixed_box *box;
	isl_multi_aff *offset;
	isl_multi_val *size;

	if (!tile)
		return isl_bool_error;

	isl_map_free(isl_map_detect_equalities(isl_map_copy(access)));

	has_strides = detect_strides(tile, access);
	if (has_strides < 0)
		return isl_bool_error;

	tile->depth = isl_map_dim(access, isl_dim_in);

	access = isl_map_copy(access);
	if (has_strides)
		access = remove_strides(access, tile);

	box = isl_map_get_range_simple_fixed_box_hull(access);
	isl_map_free(access);

	valid = isl_fixed_box_is_valid(box);
	if (valid >= 0 && valid) {
		offset = isl_fixed_box_get_offset(box);
		size = isl_fixed_box_get_size(box);
		for (i = 0; i < tile->n; ++i) {
			tile->bound[i].size = isl_multi_val_get_val(size, i);
			tile->bound[i].lb = isl_multi_aff_get_aff(offset, i);
		}
		isl_multi_aff_free(offset);
		isl_multi_val_free(size);
	}
	isl_fixed_box_free(box);

	return valid;
}

/* Internal data structure for gpu_group_references.
 *
 * scop represents the input scop.
 * kernel_depth is the schedule depth where the kernel launch will
 * be introduced, i.e., it is the depth of the band that is mapped
 * to blocks.
 * shared_depth is the schedule depth at which the copying to/from
 * shared memory is computed.  The copy operation may then
 * later be hoisted to a higher level.
 * thread_depth is the schedule depth where the thread mark is located,
 * i.e., it is the depth of the band that is mapped to threads and also
 * the schedule depth at which the copying to/from private memory
 * is computed.  The copy operation may then later be hoisted to
 * a higher level.
 * n_thread is the number of schedule dimensions in the band that
 * is mapped to threads.
 * privatization lives in the range of thread_sched (i.e., it is
 * of dimension thread_depth + n_thread) and encodes the mapping
 * to thread identifiers (as parameters).
 * host_sched contains the kernel_depth dimensions of the host schedule.
 * shared_sched contains the first shared_depth dimensions of the
 * kernel schedule.
 * copy_sched contains the first thread_depth dimensions of the
 * kernel schedule.
 * thread_sched contains the first (thread_depth + n_thread) dimensions
 * of the kernel schedule.
 * full_sched is a union_map representation of the entire kernel schedule.
 * The schedules are all formulated in terms of the original statement
 * instances, i.e., those that appear in the domains of the access
 * relations.
 */
struct gpu_group_data {
	struct ppcg_scop *scop;
	int kernel_depth;
	int shared_depth;
	int thread_depth;
	int n_thread;
	isl_set *privatization;
	isl_union_map *host_sched;
	isl_union_map *shared_sched;
	isl_union_map *copy_sched;
	isl_union_map *thread_sched;
	isl_union_map *full_sched;
};

/* Construct a map from domain_space to domain_space that increments
 * the dimension at position "pos" and leaves all other dimensions
 * constant.
 */
static __isl_give isl_map *next(__isl_take isl_space *domain_space, int pos)
{
	isl_space *space;
	isl_aff *aff;
	isl_multi_aff *next;

	space = isl_space_map_from_set(domain_space);
	next = isl_multi_aff_identity(space);
	aff = isl_multi_aff_get_aff(next, pos);
	aff = isl_aff_add_constant_si(aff, 1);
	next = isl_multi_aff_set_aff(next, pos, aff);

	return isl_map_from_multi_aff(next);
}

/* Check if the given access is coalesced (or if there is no point
 * in trying to coalesce the access by mapping the array to shared memory).
 * That is, check whether incrementing the dimension that will get
 * wrapped over the last thread index results in incrementing
 * the last array index.
 *
 * If no two consecutive array elements are ever accessed by "access",
 * then mapping the corresponding array to shared memory will not
 * improve coalescing.  In fact, the copying will likely be performed
 * by a single thread.  Consider the access as coalesced such that
 * the caller will not try and map the array to shared memory just
 * to improve coalescing.
 *
 * This function is only called for access relations without reuse and
 * kernels with at least one thread identifier.
 */
static int access_is_coalesced(struct gpu_group_data *data,
	__isl_keep isl_union_map *access)
{
	int dim;
	isl_space *space;
	isl_set *accessed;
	isl_map *access_map;
	isl_map *next_thread_x;
	isl_map *next_element;
	isl_map *map;
	int coalesced, empty;

	access = isl_union_map_copy(access);
	access = isl_union_map_apply_domain(access,
				isl_union_map_copy(data->full_sched));
	access_map = isl_map_from_union_map(access);

	space = isl_map_get_space(access_map);
	space = isl_space_range(space);
	dim = isl_space_dim(space, isl_dim_set);
	if (dim == 0)
		next_element = isl_map_empty(isl_space_map_from_set(space));
	else
		next_element = next(space, dim - 1);

	accessed = isl_map_range(isl_map_copy(access_map));
	map = isl_map_copy(next_element);
	map = isl_map_intersect_domain(map, isl_set_copy(accessed));
	map = isl_map_intersect_range(map, accessed);
	empty = isl_map_is_empty(map);
	isl_map_free(map);

	if (empty < 0 || empty) {
		isl_map_free(next_element);
		isl_map_free(access_map);
		return empty;
	}

	space = isl_map_get_space(access_map);
	space = isl_space_domain(space);
	next_thread_x = next(space, data->thread_depth + data->n_thread - 1);

	map = isl_map_apply_domain(next_thread_x, isl_map_copy(access_map));
	map = isl_map_apply_range(map, access_map);

	coalesced = isl_map_is_subset(map, next_element);

	isl_map_free(next_element);
	isl_map_free(map);

	return coalesced;
}

/* Replace the host schedule dimensions in the access relation "access"
 * by parameters, so that they are treated as fixed when checking for reuse
 * (within a kernel) or whether two consecutive elements are accessed
 * (within a kernel).
 */
static __isl_give isl_union_map *localize_access(struct gpu_group_data *data,
	__isl_take isl_union_map *access)
{
	int n;
	isl_space *space;
	isl_set *param;
	isl_union_map *umap;
	isl_id_list *ids;

	umap = isl_union_map_copy(data->host_sched);
	space = isl_union_map_get_space(umap);
	n = data->kernel_depth;
	ids = ppcg_scop_generate_names(data->scop, n, "__ppcg_host_");
	param = parametrization(space, n, 0, ids);
	isl_id_list_free(ids);
	umap = isl_union_map_intersect_range(umap,
						isl_union_set_from_set(param));
	access = isl_union_map_intersect_domain(access,
						isl_union_map_domain(umap));

	return access;
}

/* Given an access relation in terms of at least data->thread_depth initial
 * dimensions of the computed schedule, check if it is bijective for
 * fixed values of the first data->thread_depth dimensions.
 * We perform this check by equating these dimensions to parameters.
 */
static int access_is_bijective(struct gpu_group_data *data,
	__isl_keep isl_map *access)
{
	int res;
	int dim;
	isl_set *par;
	isl_space *space;
	isl_id_list *ids;

	access = isl_map_copy(access);
	space = isl_space_params(isl_map_get_space(access));
	ids = ppcg_scop_generate_names(data->scop, data->thread_depth, "s");
	dim = isl_map_dim(access, isl_dim_in);
	par = parametrization(space, dim, 0, ids);
	isl_id_list_free(ids);
	access = isl_map_intersect_domain(access, par);
	res = isl_map_is_bijective(access);
	isl_map_free(access);

	return res;
}

/* Compute the number of outer schedule tile dimensions that affect
 * the offset of "tile".
 * If there is no such dimension, then return the index
 * of the first kernel dimension, i.e., data->kernel_depth.
 */
static int compute_tile_depth(struct gpu_group_data *data,
	struct gpu_array_tile *tile)
{
	int i, j;

	for (j = tile->depth - 1; j >= data->kernel_depth; --j) {
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

	return ++j;
}

/* Return the lowest depth between data->kernel_depth and data->thread_depth
 * at which every array element accessed through "acc" is accessed
 * by a single thread.  The input dimension of "acc" is
 * data->thread_depth + data->n_thread, where the final data->n_thread
 * dimensions are those that will be mapped to threads.
 * If the values for these dimensions are uniquely determined
 * by the array index and a given number of outer dimensions, then
 * there is only one thread accessing that array element within those
 * outer dimensions.
 *
 * The input space of "acc" is first split up, such that it has the form
 *
 *	[O -> T] -> A
 *
 * with O the outer dimensions, T the dimensions that will be mapped to threads
 * and A the array index.
 *
 * Then the positions of T and A are interchanged to simplify the test
 * whether T uniquely depends on O and A.
 * In particular, the above access relation is first combined with
 *
 *	[O -> T] -> T
 *
 * to form
 *
 *	[O -> T] -> [A -> T]
 *
 * from which
 *
 *	O -> [A -> T]
 *
 * is extracted, which is then uncurried to
 *
 *	[O -> A] -> T
 *
 * Finally, the final dimensions of O are projected out one by one
 * until T is no longer uniquely determined by A and the remaining
 * dimensions in O.  The value returned is that of the last dimension
 * that was successfully projected out.
 * Note that there is no need to test whether [O -> A] -> T itself
 * is single-valued as that was already tested in access_is_bijective.
 */
static int compute_accessed_by_single_thread_depth(struct gpu_group_data *data,
	__isl_keep isl_map *acc)
{
	int i;
	isl_space *space;
	isl_map *map;
	isl_bool sv;

	if (data->thread_depth == data->kernel_depth)
		return data->thread_depth;

	acc = isl_map_copy(acc);

	space = isl_map_get_space(acc);
	space = isl_space_params(space);
	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, data->thread_depth);
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, data->n_thread);
	space = isl_space_wrap(space);
	map = isl_set_flatten_map(isl_set_universe(space));
	acc = isl_map_apply_range(map, acc);

	space = isl_space_domain(isl_map_get_space(acc));
	map = isl_map_range_map(isl_map_universe(isl_space_unwrap(space)));
	acc = isl_map_range_product(acc, map);
	acc = isl_map_domain_factor_domain(acc);
	acc = isl_map_uncurry(acc);

	for (i = data->thread_depth - 1; i >= data->kernel_depth; --i) {
		acc = isl_map_project_out(acc, isl_dim_in, i, 1);
		sv = isl_map_is_single_valued(acc);
		if (sv < 0)
			goto error;
		if (!sv)
			break;
	}

	isl_map_free(acc);

	return ++i;
error:
	isl_map_free(acc);
	return -1;
}

/* Adjust the fields of "tile" to reflect the new input dimension "depth".
 * The dimension beyond "depth" are assumed not to affect the tile,
 * so they can simply be dropped.
 */
static int tile_adjust_depth(struct gpu_array_tile *tile, int depth)
{
	int i;

	if (tile->depth == depth)
		return 0;

	for (i = 0; i < tile->n; ++i) {
		tile->bound[i].lb = isl_aff_drop_dims(tile->bound[i].lb,
					isl_dim_in, depth, tile->depth - depth);
		if (!tile->bound[i].lb)
			return -1;
		if (!tile->bound[i].shift)
			continue;
		tile->bound[i].shift = isl_aff_drop_dims(tile->bound[i].shift,
					isl_dim_in, depth, tile->depth - depth);
		if (!tile->bound[i].shift)
			return -1;
	}

	tile->depth = depth;

	return 0;
}

/* Determine the number of schedule dimensions that affect the offset of the
 * shared or private tile "tile" and store the result in tile->depth, with
 * a lower bound of data->kernel_depth.
 * Also adjust the fields of the tile to only refer to the tile->depth
 * outer schedule dimensions.
 */
static isl_stat tile_set_depth(struct gpu_group_data *data,
	struct gpu_array_tile *tile)
{
	if (tile_adjust_depth(tile, compute_tile_depth(data, tile)) < 0)
		return isl_stat_error;

	return isl_stat_ok;
}

/* Determine the number of schedule dimensions that affect the offset of the
 * shared tile and store the minimum of the private and shared tile depth
 * in group->min_depth, with a lower bound of data->kernel_depth.
 * If there is no tile defined on the array reference group,
 * then set group->min_depth to data->thread_depth.
 */
static int set_depth(struct gpu_group_data *data,
	struct gpu_array_ref_group *group)
{
	group->min_depth = data->thread_depth;

	if (group->private_tile) {
		if (group->private_tile->depth < group->min_depth)
			group->min_depth = group->private_tile->depth;
	}
	if (group->shared_tile) {
		if (tile_set_depth(data, group->shared_tile) < 0)
			return -1;
		if (group->shared_tile->depth < group->min_depth)
			group->min_depth = group->shared_tile->depth;
	}

	return 0;
}

/* Fill up the groups array with singleton groups, i.e., one group
 * per reference, initializing the array, access, write, n_ref and refs fields.
 * In particular the access field is initialized to the scheduled
 * access relation of the array reference.
 *
 * Return the number of elements initialized, i.e., the number of
 * active references in the current kernel.
 */
static int populate_array_references(struct gpu_local_array_info *local,
	struct gpu_array_ref_group **groups, struct gpu_group_data *data)
{
	int i;
	int n;
	isl_ctx *ctx = isl_union_map_get_ctx(data->copy_sched);

	n = 0;
	for (i = 0; i < local->array->n_ref; ++i) {
		isl_union_map *umap;
		isl_map *map;
		struct gpu_array_ref_group *group;
		struct gpu_stmt_access *access = local->array->refs[i];

		map = isl_map_copy(access->access);
		umap = isl_union_map_from_map(map);
		umap = isl_union_map_apply_domain(umap,
				isl_union_map_copy(data->copy_sched));

		if (isl_union_map_is_empty(umap)) {
			isl_union_map_free(umap);
			continue;
		}

		map = isl_map_from_union_map(umap);
		map = isl_map_detect_equalities(map);

		group = isl_calloc_type(ctx, struct gpu_array_ref_group);
		if (!group) {
			isl_map_free(map);
			return -1;
		}
		group->local_array = local;
		group->array = local->array;
		group->access = map;
		group->write = access->write;
		group->exact_write = access->exact_write;
		group->slice = access->n_index < local->array->n_index;
		group->refs = &local->array->refs[i];
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

/* Check if the access relations of group1 and group2 overlap within
 * copy_sched.
 */
static int accesses_overlap(struct gpu_array_ref_group *group1,
	struct gpu_array_ref_group *group2)
{
	int disjoint;

	disjoint = isl_map_is_disjoint(group1->access, group2->access);
	if (disjoint < 0)
		return -1;

	return !disjoint;
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

	if (!group1 || !group2)
		return NULL;

	ctx = isl_map_get_ctx(group1->access);
	group = isl_calloc_type(ctx, struct gpu_array_ref_group);
	if (!group)
		return NULL;
	group->local_array = group1->local_array;
	group->array = group1->array;
	group->access = isl_map_union(isl_map_copy(group1->access),
					isl_map_copy(group2->access));
	group->write = group1->write || group2->write;
	group->exact_write = group1->exact_write && group2->exact_write;
	group->slice = group1->slice || group2->slice;
	group->n_ref = group1->n_ref + group2->n_ref;
	group->refs = isl_alloc_array(ctx, struct gpu_stmt_access *,
					group->n_ref);
	if (!group->refs)
		return gpu_array_ref_group_free(group);
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

/* Given an access relation in terms of the data->thread_depth initial
 * dimensions of the computed schedule and the thread identifiers
 * (as parameters), check if the use of the corresponding private tile
 * requires unrolling.
 *
 * If we are creating a private tile because we are forced to,
 * then no unrolling is required.
 * Otherwise we check if "access" is bijective and unrolling
 * is required if it is not.  Note that the access relation
 * has already been determined to be bijective before the introduction
 * of the thread identifiers and the removal of the schedule dimensions
 * that are mapped to these threads.  If the access relation is no longer
 * bijective, then this means that more than one value of one of those
 * schedule dimensions is mapped to the same thread and therefore
 * unrolling is required.
 */
static int check_requires_unroll(struct gpu_group_data *data,
	__isl_keep isl_map *access, int force_private)
{
	int bijective;

	if (force_private)
		return 0;
	bijective = access_is_bijective(data, access);
	if (bijective < 0)
		return -1;
	return !bijective;
}

/* Map the domain of "access" to the outer data->shared_depth
 * schedule dimensions.  When data->shared_depth is equal to
 * data->thread_depth, this result is already available in group->access.
 */
static __isl_give isl_map *shared_access(struct gpu_array_ref_group *group,
	__isl_keep isl_union_map *access, struct gpu_group_data *data)
{
	isl_union_map *shared;

	if (data->shared_depth == data->thread_depth)
		return isl_map_copy(group->access);

	shared = isl_union_map_copy(access);
	shared = isl_union_map_apply_domain(shared,
			isl_union_map_copy(data->shared_sched));
	return isl_map_from_union_map(shared);
}

/* Compute the private and/or shared memory tiles for the array
 * reference group "group" of array "array".
 * Return isl_stat_ok on success and isl_stat_error on error.
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
 * Reuse and coalescing are checked within the given kernel.
 *
 * For computing a private memory tile, we also require that there is
 * some reuse.  Moreover, we require that the access is private
 * to the thread.  That is, we check that any given array element
 * is only accessed by a single thread.
 * We compute an access relation that maps the outer
 * data->thread_depth + data->n_thread schedule dimensions.
 * The latter data->n_thread will be mapped to thread identifiers.
 * We actually check that those iterators that will be wrapped
 * partition the array space.  This check is stricter than necessary
 * since several iterations may be mapped onto the same thread
 * and then they could be allowed to access the same memory elements,
 * but our check does not allow this situation.
 *
 * For private memory tiles, the number of schedule dimensions that
 * affect the offset is computed and stored in tile->depth, with
 * a lower bound of data->kernel_depth.  If this depth is smaller
 * than the minimal depth that still ensures that every element
 * is accessed by a single thread, then the depth is raised
 * to this minimal depth.
 * The fields of the tile are then adjusted to only refer to the tile->depth
 * outer schedule dimensions.
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
 * If the use of the private tile requires unrolling, but some
 * of the other arrays are forcibly mapped to private memory,
 * then we do not allow the use of this private tile since
 * we cannot move the schedule dimensions that need to be unrolled down
 * without performing some kind of expansion on those arrays
 * that are forcibly mapped to private memory.
 *
 * If the array is marked force_private, then we bypass all checks
 * and assume we can (and should) use registers only.
 *
 * If it turns out we can (or have to) use registers, we compute
 * the private memory tile size using can_tile, after introducing a dependence
 * on the thread indices.
 */
static isl_stat compute_group_bounds_core(struct ppcg_kernel *kernel,
	struct gpu_array_ref_group *group, struct gpu_group_data *data)
{
	isl_ctx *ctx = isl_space_get_ctx(group->array->space);
	isl_union_map *access, *local;
	int n_index = group->array->n_index;
	int no_reuse, coalesced;
	isl_map *acc;
	int force_private = group->local_array->force_private;
	int use_shared = !force_private && kernel->options->use_shared_memory &&
				data->n_thread > 0;
	int use_private = force_private || kernel->options->use_private_memory;
	isl_stat r = isl_stat_ok;
	isl_bool ok;
	int requires_unroll;
	int unique_depth;

	if (!use_shared && !use_private)
		return isl_stat_ok;
	if (gpu_array_is_read_only_scalar(group->array))
		return isl_stat_ok;
	if (!force_private && !group->exact_write)
		return isl_stat_ok;
	if (group->slice)
		return isl_stat_ok;

	access = gpu_array_ref_group_access_relation(group, 1, 1);
	local = localize_access(data, isl_union_map_copy(access));
	no_reuse = isl_union_map_is_injective(local);
	if (no_reuse < 0)
		r = isl_stat_error;
	if (use_shared && no_reuse)
		coalesced = access_is_coalesced(data, local);
	isl_union_map_free(local);

	if (r >= 0 && kernel->options->debug->verbose &&
	    use_shared && no_reuse && coalesced)
		report_no_reuse_and_coalesced(kernel, access);

	if (use_shared && (!no_reuse || !coalesced)) {
		group->shared_tile = gpu_array_tile_create(ctx,
							group->array->n_index);
		acc = shared_access(group, access, data);
		ok = can_tile(acc, group->shared_tile);
		if (ok < 0)
			r = isl_stat_error;
		else if (!ok)
			group->shared_tile =
					gpu_array_tile_free(group->shared_tile);
		isl_map_free(acc);
	}

	if (r < 0 || (!force_private && (!use_private || no_reuse))) {
		isl_union_map_free(access);
		return r;
	}

	access = isl_union_map_apply_domain(access,
					isl_union_map_copy(data->thread_sched));

	acc = isl_map_from_union_map(access);

	if (!force_private && !access_is_bijective(data, acc)) {
		isl_map_free(acc);
		return isl_stat_ok;
	}

	unique_depth = compute_accessed_by_single_thread_depth(data, acc);

	acc = isl_map_intersect_domain(acc, isl_set_copy(data->privatization));
	acc = isl_map_project_out(acc, isl_dim_in, data->thread_depth,
								data->n_thread);
	requires_unroll = check_requires_unroll(data, acc, force_private);
	if (unique_depth < 0 || requires_unroll < 0 ||
	    (requires_unroll && kernel->any_force_private)) {
		isl_map_free(acc);
		return requires_unroll < 0 ? isl_stat_error : isl_stat_ok;
	}

	group->private_tile = gpu_array_tile_create(ctx, n_index);
	group->private_tile->requires_unroll = requires_unroll;
	ok = can_tile(acc, group->private_tile);
	if (ok >= 0 && !ok)
		group->private_tile = gpu_array_tile_free(group->private_tile);
	isl_map_free(acc);
	if (ok < 0)
		return isl_stat_error;

	if (group->private_tile) {
		struct gpu_array_tile *tile = group->private_tile;
		int tile_depth = compute_tile_depth(data, tile);
		if (tile_depth < unique_depth)
			tile_depth = unique_depth;
		if (tile_adjust_depth(tile, tile_depth) < 0)
			return isl_stat_error;
	}

	if (force_private && !group->private_tile)
		isl_die(ctx, isl_error_internal,
			"unable to map array reference group to registers",
			return isl_stat_error);

	return isl_stat_ok;
}

/* Compute the private and/or shared memory tiles for the array
 * reference group "group" of array "array" and set the tile depth.
 * Return 0 on success and -1 on error.
 */
static int compute_group_bounds(struct ppcg_kernel *kernel,
	struct gpu_array_ref_group *group, struct gpu_group_data *data)
{
	if (!group)
		return -1;
	if (compute_group_bounds_core(kernel, group, data) < 0)
		return -1;
	if (set_depth(data, group) < 0)
		return -1;

	return 0;
}

/* If two groups have overlapping access relations (as determined by
 * the "overlap" function) and if one of them involves a write,
 * then merge the two groups into one.
 * If "compute_bounds" is set, then call compute_group_bounds
 * on the merged groups.
 * If any group is merged into the current group, then its access
 * relation may have changed or it may have been turned into a write.
 * The combined group might therefore overlap with groups that
 * the original group did not overlap with.  The groups therefore
 * need to be checked again.
 *
 * Return the updated number of groups.
 * Return -1 on error.
 */
static int group_writes(struct ppcg_kernel *kernel,
	int n, struct gpu_array_ref_group **groups,
	int (*overlap)(struct gpu_array_ref_group *group1,
		struct gpu_array_ref_group *group2), int compute_bounds,
	struct gpu_group_data *data)
{
	int i, j;
	int any_merge;

	for (i = 0; i < n; i += !any_merge) {
		any_merge = 0;
		for (j = n - 1; j > i; --j) {
			if (!groups[i]->write && !groups[j]->write)
				continue;

			if (!overlap(groups[i], groups[j]))
				continue;

			any_merge = 1;
			groups[i] = join_groups_and_free(groups[i], groups[j]);
			if (j != n - 1)
				groups[j] = groups[n - 1];
			groups[n - 1] = NULL;
			n--;

			if (!groups[i])
				return -1;
			if (compute_bounds &&
			    compute_group_bounds(kernel, groups[i], data) < 0)
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
static int group_overlapping_writes(struct ppcg_kernel *kernel,
	int n, struct gpu_array_ref_group **groups,
	struct gpu_group_data *data)
{
	return group_writes(kernel, n, groups, &accesses_overlap, 0, data);
}

/* Check if the access relations of group1 and group2 overlap within
 * the outermost min(group1->min_depth, group2->min_depth) loops.
 */
static int depth_accesses_overlap(struct gpu_array_ref_group *group1,
	struct gpu_array_ref_group *group2)
{
	int depth;
	int dim;
	int empty;
	isl_map *map_i, *map_j, *map;

	depth = group1->min_depth;
	if (group2->min_depth < depth)
		depth = group2->min_depth;
	map_i = isl_map_copy(group1->access);
	dim = isl_map_dim(map_i, isl_dim_in);
	map_i = isl_map_eliminate(map_i, isl_dim_in, depth, dim - depth);
	map_j = isl_map_copy(group2->access);
	map_j = isl_map_eliminate(map_j, isl_dim_in, depth, dim - depth);
	map = isl_map_intersect(map_i, map_j);
	empty = isl_map_is_empty(map);
	isl_map_free(map);

	return !empty;
}

/* If two groups have overlapping access relations (within the outer
 * depth loops) and if one of them involves a write,
 * then merge the two groups into one.
 *
 * Return the updated number of groups.
 */
static int group_depth_overlapping_writes(struct ppcg_kernel *kernel,
	int n, struct gpu_array_ref_group **groups, struct gpu_group_data *data)
{
	return group_writes(kernel, n, groups, &depth_accesses_overlap, 1,
				data);
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
 * If any group is merged into the current group, then it may become
 * profitable to combine it with groups that were considered before
 * the merge.  The groups are therefore checked again after a merge.
 *
 * If merging two groups decreases the depth of the tile of
 * one or both of the two groups, then we need to check for overlapping
 * writes again.
 *
 * Return the number of groups after merging.
 * Return -1 on error.
 */
static int group_common_shared_memory_tile(struct ppcg_kernel *kernel,
	struct gpu_array_info *array, int n,
	struct gpu_array_ref_group **groups, struct gpu_group_data *data)
{
	int i, j;
	int recompute_overlap = 0;
	int any_merge;

	for (i = 0; i < n; i += !any_merge) {
		any_merge = 0;
		if (!groups[i]->shared_tile)
			continue;
		for (j = n - 1; j > i; --j) {
			struct gpu_array_ref_group *group;

			if (!groups[j]->shared_tile)
				continue;

			if (!depth_accesses_overlap(groups[i], groups[j]))
				continue;

			group = join_groups(groups[i], groups[j]);
			if (compute_group_bounds(kernel, group, data) < 0) {
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

			any_merge = 1;
			if (group->min_depth < groups[i]->min_depth ||
			    group->min_depth < groups[j]->min_depth)
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
		n = group_depth_overlapping_writes(kernel, n, groups, data);
	return n;
}

/* Set array->n_group and array->groups to n and groups.
 *
 * Additionally, set the "nr" field of each group.
 */
static void set_array_groups(struct gpu_local_array_info *array,
	int n, struct gpu_array_ref_group **groups)
{
	int i;

	array->n_group = n;
	array->groups = groups;

	for (i = 0; i < n; ++i)
		groups[i]->nr = i;
}

/* Combine all groups in "groups" into a single group and return
 * the new number of groups (1 or 0 if there were no groups to start with).
 */
static int join_all_groups(int n, struct gpu_array_ref_group **groups)
{
	int i;

	for (i = n - 1; i > 0; --i) {
		groups[0] = join_groups_and_free(groups[0], groups[i]);
		groups[i] = NULL;
		n--;
	}

	return n;
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
 * the depth of the effective tile.
 *
 * Furthermore, if two groups admit a shared memory tile and if the
 * combination of the two also admits a shared memory tile, we merge
 * the two groups.
 *
 * If the array contains structures, then we compute a single
 * reference group without trying to find any tiles
 * since we do not map such arrays to private or shared
 * memory.  The only exception is when those arrays of structures
 * are required to be mapped to private memory.
 */
static int group_array_references(struct ppcg_kernel *kernel,
	struct gpu_local_array_info *local, struct gpu_group_data *data)
{
	int i;
	int n;
	isl_ctx *ctx = isl_union_map_get_ctx(data->shared_sched);
	struct gpu_array_ref_group **groups;

	groups = isl_calloc_array(ctx, struct gpu_array_ref_group *,
					local->array->n_ref);
	if (!groups)
		return -1;

	n = populate_array_references(local, groups, data);

	if (local->array->has_compound_element && !local->force_private) {
		n = join_all_groups(n, groups);
		set_array_groups(local, n, groups);
		return 0;
	}

	n = group_overlapping_writes(kernel, n, groups, data);

	for (i = 0; i < n; ++i)
		if (compute_group_bounds(kernel, groups[i], data) < 0)
			n = -1;

	n = group_depth_overlapping_writes(kernel, n, groups, data);

	n = group_common_shared_memory_tile(kernel, local->array,
					    n, groups, data);

	set_array_groups(local, n, groups);

	if (n >= 0)
		return 0;

	for (i = 0; i < local->array->n_ref; ++i)
		gpu_array_ref_group_free(groups[i]);
	return -1;
}

/* For each array in the input program that can be mapped to private memory,
 * check if there are any order dependences active inside the current kernel,
 * within the same iteration of the host schedule, i.e., the prefix
 * schedule at "node".
 * If so, mark the array as force_private so that its reference groups will be
 * mapped to a registers.
 *
 * Note that the arrays that cannot be mapped to private memory have
 * had their order dependences added to prog->array_order and
 * subsequently to the coincidence constraints.
 */
static void check_can_be_private_live_ranges(struct ppcg_kernel *kernel,
	__isl_keep isl_schedule_node *node)
{
	int i;
	isl_union_set *domain;
	isl_multi_union_pw_aff *prefix;
	isl_union_pw_multi_aff *contraction;

	if (!kernel->options->live_range_reordering)
		return;

	kernel->any_force_private = 0;

	prefix = isl_schedule_node_get_prefix_schedule_multi_union_pw_aff(node);
	contraction = isl_union_pw_multi_aff_copy(kernel->contraction);
	prefix = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(prefix,
								contraction);
	domain = isl_union_set_copy(kernel->expanded_domain);
	domain = isl_union_set_universe(domain);

	for (i = 0; i < kernel->n_array; ++i) {
		struct gpu_local_array_info *local = &kernel->array[i];
		isl_union_map *order;

		local->force_private = 0;
		if (!gpu_array_can_be_private(local->array))
			continue;
		order = isl_union_map_copy(local->array->dep_order);
		order = isl_union_map_intersect_domain(order,
						    isl_union_set_copy(domain));
		order = isl_union_map_intersect_range(order,
						    isl_union_set_copy(domain));
		order = isl_union_map_eq_at_multi_union_pw_aff(order,
					isl_multi_union_pw_aff_copy(prefix));
		if (!isl_union_map_is_empty(order)) {
			local->force_private = 1;
			kernel->any_force_private = 1;
		}
		isl_union_map_free(order);
	}

	isl_multi_union_pw_aff_free(prefix);
	isl_union_set_free(domain);
}

/* Expand the domain of the schedule "s" by plugging in
 * the contraction "contraction" and return the result.
 */
static __isl_give isl_union_map *expand(__isl_take isl_union_map *s,
	__isl_keep isl_union_pw_multi_aff *contraction)
{
	contraction = isl_union_pw_multi_aff_copy(contraction);
	s = isl_union_map_preimage_domain_union_pw_multi_aff(s, contraction);
	return s;
}

/* Create a set of dimension data->thread_depth + data->n_thread
 * that equates the residue of the final data->n_thread dimensions
 * modulo the kernel->block_dim sizes to the thread identifiers.
 * Store the computed set in data->privatization.
 *
 * The construction starts with the space of kernel->thread_filter,
 * which is known to reference all thread identifiers.
 */
static void compute_privatization(struct gpu_group_data *data,
	struct ppcg_kernel *kernel)
{
	int i;
	isl_ctx *ctx;
	isl_space *space;
	isl_local_space *ls;
	isl_set *set;

	ctx = isl_union_map_get_ctx(data->shared_sched);
	space = isl_union_set_get_space(kernel->thread_filter);
	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set,
				    data->thread_depth + data->n_thread);
	set = isl_set_universe(space);
	space = isl_set_get_space(set);
	ls = isl_local_space_from_space(space);

	for (i = 0; i < data->n_thread; ++i) {
		isl_aff *aff, *aff2;
		isl_constraint *c;
		isl_val *v;
		isl_id *id;
		int pos;

		if (!set)
			break;

		aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
					isl_dim_set, data->thread_depth + i);
		v = isl_val_int_from_si(ctx, kernel->block_dim[i]);
		aff = isl_aff_mod_val(aff, v);
		id = isl_id_list_get_id(kernel->thread_ids, i);
		pos = isl_set_find_dim_by_id(set, isl_dim_param, id);
		isl_id_free(id);
		aff2 = isl_aff_var_on_domain(isl_local_space_copy(ls),
					isl_dim_param, pos);
		aff = isl_aff_sub(aff, aff2);
		c = isl_equality_from_aff(aff);
		set = isl_set_add_constraint(set, c);
	}

	isl_local_space_free(ls);
	data->privatization = set;
}

/* Return the prefix schedule at "node" as a relation
 * between domain elements and schedule dimensions after detecting
 * equalities in this relation.
 */
static __isl_give isl_union_map *prefix_with_equalities(
	__isl_keep isl_schedule_node *node)
{
	isl_union_map *schedule;

	schedule = isl_schedule_node_get_prefix_schedule_relation(node);
	schedule = isl_union_map_detect_equalities(schedule);

	return schedule;
}

/* Group references of all arrays in "kernel".
 * "node" points to the kernel mark.
 * The mapping to shared memory in computed at the "shared" mark.
 *
 * We first extract all required schedule information into
 * a gpu_group_data structure and then consider each array
 * in turn.
 */
int gpu_group_references(struct ppcg_kernel *kernel,
	__isl_keep isl_schedule_node *node)
{
	int i;
	int r = 0;
	isl_union_pw_multi_aff *contraction;
	struct gpu_group_data data;

	check_can_be_private_live_ranges(kernel, node);

	data.scop = kernel->prog->scop;

	data.kernel_depth = isl_schedule_node_get_schedule_depth(node);
	data.host_sched = isl_schedule_node_get_prefix_schedule_relation(node);

	node = isl_schedule_node_copy(node);
	node = gpu_tree_move_down_to_shared(node, kernel->core);
	data.shared_depth = isl_schedule_node_get_schedule_depth(node);
	data.shared_sched = prefix_with_equalities(node);

	node = gpu_tree_move_down_to_thread(node, kernel->core);
	node = isl_schedule_node_child(node, 0);
	data.thread_depth = isl_schedule_node_get_schedule_depth(node);
	data.n_thread = isl_schedule_node_band_n_member(node);
	if (data.thread_depth == data.shared_depth)
		data.copy_sched = isl_union_map_copy(data.shared_sched);
	else
		data.copy_sched = prefix_with_equalities(node);
	data.thread_sched = isl_union_map_copy(data.copy_sched);
	data.thread_sched = isl_union_map_flat_range_product(data.thread_sched,
		isl_schedule_node_band_get_partial_schedule_union_map(node));
	data.thread_sched = isl_union_map_detect_equalities(data.thread_sched);

	contraction = isl_union_pw_multi_aff_copy(kernel->contraction);
	data.host_sched = expand(data.host_sched, contraction);
	data.shared_sched = expand(data.shared_sched, contraction);
	if (data.thread_depth == data.shared_depth) {
		isl_union_map_free(data.copy_sched);
		data.copy_sched = isl_union_map_copy(data.shared_sched);
	} else {
		data.copy_sched = expand(data.copy_sched, contraction);
	}
	data.thread_sched = expand(data.thread_sched, contraction);
	isl_union_pw_multi_aff_free(contraction);

	node = isl_schedule_node_child(node, 0);
	data.full_sched = isl_union_map_copy(data.thread_sched);
	data.full_sched = isl_union_map_flat_range_product(data.full_sched,
		isl_schedule_node_get_subtree_schedule_union_map(node));
	isl_schedule_node_free(node);

	compute_privatization(&data, kernel);

	for (i = 0; i < kernel->n_array; ++i) {
		r = group_array_references(kernel, &kernel->array[i], &data);
		if (r < 0)
			break;
	}

	isl_union_map_free(data.host_sched);
	isl_union_map_free(data.shared_sched);
	isl_union_map_free(data.copy_sched);
	isl_union_map_free(data.thread_sched);
	isl_union_map_free(data.full_sched);
	isl_set_free(data.privatization);

	return r;
}

/* Given a description of an array tile "tile" and the "space"
 *
 *	{ D -> A }
 *
 * where D represents the first tile->depth schedule dimensions
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

		stride_i = isl_val_copy(bound->stride);
		shift_i = isl_aff_copy(bound->shift);

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
 * where D represents the first tile->depth schedule dimensions,
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
	isl_space *space;
	isl_multi_aff *tiling, *lb, *insert_array;
	isl_printer *p;
	char *local_name;

	tile = gpu_array_ref_group_tile(group);
	if (!tile)
		return;

	space = isl_map_get_space(group->access);
	space = isl_space_from_range(isl_space_range(space));
	space = isl_space_add_dims(space, isl_dim_in, tile->depth);
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
