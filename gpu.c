/*
 * Copyright 2010-2011 INRIA Saclay
 * Copyright 2012-2013 Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 * and Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <isl/polynomial.h>
#include <isl/union_set.h>
#include <isl/aff.h>
#include <isl/ilp.h>
#include <isl/flow.h>
#include <isl/band.h>
#include <isl/schedule.h>
#include <isl/options.h>
#include <isl/ast_build.h>

#include "cpu.h"
#include "gpu.h"
#include "schedule.h"
#include "ppcg_options.h"
#include "print.h"

/* The fields stride, shift and shift_map only contain valid information
 * if shift != NULL.
 * If so, they express that current index is such that if you add shift,
 * then the result is always a multiple of stride.
 * shift_map contains the mapping
 *
 *	i -> (i + shift)/stride
 *
 * Let D represent the initial shared_len dimensions of the computed schedule.
 * The spaces of "lb" and "shift" are of the form
 *
 *	D -> [b]
 *
 * "shift_map" is of the form
 *
 *	[D -> i] -> [D -> (i + shift(D))/stride]
 */
struct gpu_array_bound {
	isl_val *size;
	isl_aff *lb;

	isl_val *stride;
	isl_aff *shift;
	isl_basic_map *shift_map;
};

/* A tile of an array.
 *
 * n is the dimension of the array.
 * bound is an array of size "n" representing the lower bound
 *	and size for each index.
 *
 * tiling maps a tile in the global array to the corresponding
 * shared/private memory tile and is of the form
 *
 *	{ [D[i] -> A[a]] -> T[(a + shift(i))/stride - lb(i)] }
 *
 * where D represents the initial shared_len dimensions
 * of the computed schedule.
 */
struct gpu_array_tile {
	int n;
	struct gpu_array_bound *bound;
	isl_multi_aff *tiling;
};

struct gpu_array_info;

/* A group of array references in a kernel that should be handled together.
 * If private_tile is not NULL, then it is mapped to registers.
 * Otherwise, if shared_tile is not NULL, it is mapped to shared memory.
 * Otherwise, it is accessed from global memory.
 */
struct gpu_array_ref_group {
	/* The references in this group access this array. */
	struct gpu_array_info *array;
	/* Position of this group in the list of reference groups of array. */
	int nr;

	/* The following fields are use during the construction of the groups.
	 * access is the combined access relation relative to the shared
	 * memory tiling.  In particular, the domain of the map corresponds
	 * to the first shared_len dimensions of the computed schedule.
	 * write is set if any access in the group is a write.
	 * exact_write is set if all writes are definite writes.
	 */
	isl_map *access;
	int write;
	int exact_write;

	/* The shared memory tile, NULL if none. */
	struct gpu_array_tile *shared_tile;

	/* The private memory tile, NULL if none. */
	struct gpu_array_tile *private_tile;

	/* References in this group; point to elements of a linked list. */
	int n_ref;
	struct gpu_stmt_access **refs;

	/* Last shared memory tile dimension that affects tile of this group. */
	int last_shared;
};

struct gpu_gen {
	isl_ctx *ctx;
	struct ppcg_options *options;

	/* Callback for printing of AST in appropriate format. */
	__isl_give isl_printer *(*print)(__isl_take isl_printer *p,
		struct gpu_prog *prog, __isl_keep isl_ast_node *tree,
		struct gpu_types *types, void *user);
	void *print_user;

	struct gpu_prog *prog;
	/* The generated AST. */
	isl_ast_node *tree;

	/* The sequence of types for which a definition has been printed. */
	struct gpu_types types;

	/* User specified tile, grid and block sizes for each kernel */
	isl_union_map *sizes;

	/* Effectively used tile, grid and block sizes for each kernel */
	isl_union_map *used_sizes;

	/* Identifier of current kernel. */
	int kernel_id;
	/* Pointer to the current kernel. */
	struct ppcg_kernel *kernel;
	/* Does the computed schedule exhibit any parallelism? */
	int any_parallelism;

	/* First tile dimension. */
	int tile_first;
	/* Number of tile dimensions. */
	int tile_len;
	/* Number of initial parallel loops among tile dimensions. */
	int n_parallel;

	/* Number of dimensions determining shared memory. */
	int shared_len;

	/* Number of rows in the untiled schedule. */
	int untiled_len;
	/* Number of rows in the tiled schedule. */
	int tiled_len;
	/* Number of rows in schedule after tiling/wrapping over threads. */
	int thread_tiled_len;

	/* Global untiled schedule. */
	isl_union_map *sched;
	/* Local (per kernel launch) tiled schedule. */
	isl_union_map *tiled_sched;
	/* Local schedule per shared memory tile loop iteration. */
	isl_union_map *local_sched;

	/* Local tiled schedule projected onto the shared tile loops and
	 * the loops that will be wrapped over the threads,
	 * with all shared tile loops parametrized.
	 */
	isl_union_map *shared_sched;
	/* Projects out the loops that will be wrapped over the threads
	 * from shared_sched.
	 */
	isl_union_map *shared_proj;

	/* A map that takes the range of shared_sched as input,
	 * wraps the appropriate loops over the threads and then projects
	 * out these loops.
	 */
	isl_map *privatization;

	/* A map from the shared memory tile loops and the thread indices
	 * (as parameters) to the set of accessed memory elements that
	 * will be accessed through private copies.
	 */
	isl_union_map *private_access;

	/* The schedule for the current private/shared access
	 * (within print_private_access or print_shared_access).
	 */
	isl_map *copy_sched;
	/* The array reference group corresponding to copy_sched. */
	struct gpu_array_ref_group *copy_group;

	/* Is any array in the current kernel marked force_private? */
	int any_force_private;

	/* First loop to unroll (or -1 if none) in the current part of the
	 * schedule.
	 */
	int first_unroll;

	int n_grid;
	int n_block;
	/* Note: in the input file, the sizes of the grid and the blocks
	 * are specified in the order x, y, z, but internally, the sizes
	 * are stored in reverse order, so that the last element always
	 * refers to the x dimension.
	 */
	int grid_dim[2];
	int block_dim[3];
	int *tile_size;
};

/* Print the name of the local copy of a given group of array references.
 */
static __isl_give isl_printer *print_array_name(__isl_take isl_printer *p,
	struct gpu_array_ref_group *group)
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

/* Collect all references to the given array and store pointers to them
 * in array->refs.
 *
 * If the array contains structures, then there is no need to collect
 * the references since we will not be computing any reference groups.
 */
static void collect_references(struct gpu_prog *prog,
	struct gpu_array_info *array)
{
	int i;
	int n;

	if (array->has_compound_element)
		return;

	n = 0;
	for (i = 0; i < prog->n_stmts; ++i) {
		struct gpu_stmt *stmt = &prog->stmts[i];
		struct gpu_stmt_access *access;

		for (access = stmt->accesses; access; access = access->next) {
			const char *name;
			name = isl_map_get_tuple_name(access->access,
						      isl_dim_out);
			if (name && !strcmp(array->name, name))
				n++;
		}
	}

	array->n_ref = n;
	array->refs = isl_alloc_array(prog->ctx, struct gpu_stmt_access *, n);
	assert(array->refs);

	n = 0;
	for (i = 0; i < prog->n_stmts; ++i) {
		struct gpu_stmt *stmt = &prog->stmts[i];
		struct gpu_stmt_access *access;

		for (access = stmt->accesses; access; access = access->next) {
			const char *name;
			name = isl_map_get_tuple_name(access->access,
						      isl_dim_out);
			if (!name || strcmp(array->name, name))
				continue;

			array->refs[n++] = access;
		}
	}
}

/* Create a gpu_array_tile for an array of dimension "n_index".
 */
static struct gpu_array_tile *create_tile(isl_ctx *ctx, int n_index)
{
	int i;
	struct gpu_array_tile *tile;

	tile = isl_calloc_type(ctx, struct gpu_array_tile);
	assert(tile);

	tile->n = n_index;

	tile->bound = isl_alloc_array(ctx, struct gpu_array_bound, n_index);
	assert(tile->bound);

	for (i = 0; i < n_index; ++i) {
		tile->bound[i].size = NULL;
		tile->bound[i].lb = NULL;
		tile->bound[i].stride = NULL;
		tile->bound[i].shift = NULL;
		tile->bound[i].shift_map = NULL;
	}

	return tile;
}

static void *free_tile(struct gpu_array_tile *tile)
{
	int j;

	if (!tile)
		return NULL;

	for (j = 0; j < tile->n; ++j) {
		isl_val_free(tile->bound[j].size);
		isl_val_free(tile->bound[j].stride);
		isl_aff_free(tile->bound[j].lb);
		isl_aff_free(tile->bound[j].shift);
		isl_basic_map_free(tile->bound[j].shift_map);
	}
	free(tile->bound);
	isl_multi_aff_free(tile->tiling);
	free(tile);

	return NULL;
}

static struct pet_array *find_array(struct ppcg_scop *scop,
	__isl_keep isl_set *accessed)
{
	int i;
	isl_id *id;

	id = isl_set_get_tuple_id(accessed);

	for (i = 0; i < scop->n_array; ++i) {
		isl_id *id_i;

		id_i = isl_set_get_tuple_id(scop->arrays[i]->extent);
		isl_id_free(id_i);
		if (id == id_i)
			break;
	}
	isl_id_free(id);

	return i < scop->n_array ? scop->arrays[i] : NULL;
}

/* Compute and return the extent of "array", taking into account the set of
 * accessed elements.
 *
 * In particular, the extent in the outer dimension is taken
 * from "accessed", while then extent in the remaing dimensions
 * are taken from array->extent.
 *
 * The extent in the outer dimension cannot be taken from array->extent
 * because that may be unbounded.  Furthermore, even if it is bounded,
 * it may be larger than the piece of the array that is being accessed.
 */
static __isl_give isl_set *compute_extent(struct pet_array *array,
	__isl_keep isl_set *accessed)
{
	int n_index;
	isl_id *id;
	isl_set *outer;
	isl_set *extent;

	extent = isl_set_copy(array->extent);

	n_index = isl_set_dim(accessed, isl_dim_set);
	if (n_index == 0)
		return extent;

	extent = isl_set_project_out(extent, isl_dim_set, 0, 1);
	outer = isl_set_copy(accessed);
	outer = isl_set_project_out(outer, isl_dim_set, 1, n_index - 1);
	extent = isl_set_flat_product(outer, extent);
	id = isl_set_get_tuple_id(accessed);
	extent = isl_set_set_tuple_id(extent, id);

	return extent;
}

/* Is the array "array" being extracted a read-only scalar?
 *
 * That is, is "array" a scalar that is never possibly written to.
 * An array containing structures is never considered to be a scalar.
 */
static int is_read_only_scalar(struct gpu_array_info *array,
	struct gpu_prog *prog)
{
	isl_set *space;
	isl_union_map *write;
	int empty;

	if (array->has_compound_element)
		return 0;
	if (array->n_index != 0)
		return 0;

	write = isl_union_map_copy(prog->may_write);
	space = isl_set_universe(isl_space_copy(array->space));
	write = isl_union_map_intersect_range(write,
						isl_union_set_from_set(space));
	empty = isl_union_map_is_empty(write);
	isl_union_map_free(write);

	return empty;
}

/* Compute bounds on the host arrays based on the accessed elements
 * and collect all references to the array.
 *
 * If the array is zero-dimensional and does not contain structures,
 * i.e., if the array is a scalar, we check whether it is read-only.
 */
static int extract_array_info(__isl_take isl_set *array, void *user)
{
	int i;
	struct gpu_prog *prog = (struct gpu_prog *)user;
	const char *name;
	int n_index;
	isl_pw_aff **bounds;
	struct pet_array *pa;
	struct gpu_array_info *info;
	isl_set *extent;

	info = &prog->array[prog->n_array];
	prog->n_array++;

	n_index = isl_set_dim(array, isl_dim_set);
	name = isl_set_get_tuple_name(array);
	bounds = isl_alloc_array(isl_set_get_ctx(array),
				 isl_pw_aff *, n_index);
	if (!bounds)
		goto error;

	info->space = isl_set_get_space(array);
	info->name = strdup(name);
	info->n_index = n_index;
	info->bound = bounds;
	info->linearize = prog->scop->options->linearize_device_arrays;

	pa = find_array(prog->scop, array);
	if (!pa)
		isl_die(isl_set_get_ctx(array), isl_error_internal,
			"unable to find array in scop", goto error);

	info->type = strdup(pa->element_type);
	info->size = pa->element_size;
	info->local = pa->declared && !pa->exposed;
	info->has_compound_element = pa->element_is_record;
	info->read_only_scalar = is_read_only_scalar(info, prog);

	extent = compute_extent(pa, array);
	info->extent = extent;
	for (i = 0; i < n_index; ++i) {
		isl_set *dom;
		isl_local_space *ls;
		isl_aff *one;
		isl_pw_aff *bound;

		dom = isl_set_copy(extent);
		dom = isl_set_project_out(dom, isl_dim_set, i + 1,
					    n_index - (i + 1));
		dom = isl_set_project_out(dom, isl_dim_set, 0, i);
		if (!isl_set_dim_has_upper_bound(dom, isl_dim_set, 0)) {
			fprintf(stderr, "unable to determine extent of '%s' "
				"in dimension %d\n", info->name, i);
			dom = isl_set_free(dom);
		}
		bound = isl_set_dim_max(dom, 0);
		dom = isl_pw_aff_domain(isl_pw_aff_copy(bound));
		ls = isl_local_space_from_space(isl_set_get_space(dom));
		one = isl_aff_zero_on_domain(ls);
		one = isl_aff_add_constant_si(one, 1);
		bound = isl_pw_aff_add(bound, isl_pw_aff_alloc(dom, one));
		bound = isl_pw_aff_gist(bound, isl_set_copy(prog->context));

		bounds[i] = bound;
		if (!isl_pw_aff_is_cst(bound))
			info->linearize = 1;
	}

	collect_references(prog, info);

	isl_set_free(array);
	return 0;
error:
	isl_set_free(array);
	return -1;
}

/* Compute a mapping from all outer arrays (of structs) in scop
 * to their innermost arrays.
 *
 * In particular, for each array of a primitive type, the result
 * contains the identity mapping on that array.
 * For each array involving member accesses, the result
 * contains a mapping from the elements of the outer array of structs
 * to all corresponding elements of the innermost nested arrays.
 */
static __isl_give isl_union_map *compute_to_inner(struct ppcg_scop *scop)
{
	int i;
	isl_union_map *to_inner;

	to_inner = isl_union_map_empty(isl_set_get_space(scop->context));

	for (i = 0; i < scop->n_array; ++i) {
		struct pet_array *array = scop->arrays[i];
		isl_set *set;
		isl_map *map;

		if (array->element_is_record)
			continue;

		set = isl_set_copy(array->extent);
		map = isl_set_identity(isl_set_copy(set));

		while (set && isl_set_is_wrapping(set)) {
			isl_id *id;
			isl_map *wrapped;

			id = isl_set_get_tuple_id(set);
			wrapped = isl_set_unwrap(set);
			wrapped = isl_map_domain_map(wrapped);
			wrapped = isl_map_set_tuple_id(wrapped, isl_dim_in, id);
			map = isl_map_apply_domain(map, wrapped);
			set = isl_map_domain(isl_map_copy(map));
		}

		map = isl_map_gist_domain(map, set);

		to_inner = isl_union_map_add_map(to_inner, map);
	}

	return to_inner;
}

/* Remove independence from the order constraints "order" on array "array".
 * Since the pairs of iterations in the filter relation of an independence
 * are guaranteed to be completely independent by the user, there is
 * no need to ensure that live ranges are ordered along thong pairs.
 * We make an exception for local variables, though, as the independence
 * guarantee does not apply to those.
 *
 * The order constraints are used in two places.
 * Those on scalars are used in check_scalar_live_ranges to check if
 * we need to force the scalar to be private.  Any non-local scalar
 * should not be forced scalar if it only appears in independent loops.
 * Those on non-scalars are added to the coincidence constraints
 * in compute_schedule because we do not support any array expansion.
 * Accesses to non-local arrays should not prevent a loop from being
 * considered coincident so we should indeed remove those constraints
 * from the order constraints.
 */
static __isl_give isl_union_map *remove_independences(struct gpu_prog *prog,
	struct gpu_array_info *array, __isl_take isl_union_map *order)
{
	int i;

	for (i = 0; i < prog->scop->n_independence; ++i) {
		struct pet_independence *pi = prog->scop->independences[i];
		if (isl_union_set_contains(pi->local, array->space))
			continue;

		order = isl_union_map_subtract(order,
						isl_union_map_copy(pi->filter));
	}

	return order;
}

/* For each array in "prog", store the (untagged) order dependences
 * derived from the array in array->dep_order.
 * In particular, consider all references that access the given array
 * and take the order dependences that have one of these references
 * as source.  (Since an order dependence relates two references to
 * the same array, the target of these order dependences will also
 * be one of these references.)
 * Additionally, store the union of these array->dep_order relations
 * for all non-scalar arrays in prog->array_order.
 */
void collect_order_dependences(struct gpu_prog *prog)
{
	int i;
	isl_space *space;
	isl_union_map *accesses;

	space = isl_union_map_get_space(prog->read);
	prog->array_order = isl_union_map_empty(space);

	accesses = isl_union_map_copy(prog->scop->tagged_reads);
	accesses = isl_union_map_union(accesses,
			    isl_union_map_copy(prog->scop->tagged_may_writes));
	accesses = isl_union_map_universe(accesses);
	accesses = isl_union_map_apply_range(accesses,
					    isl_union_map_copy(prog->to_outer));

	for (i = 0; i < prog->n_array; ++i) {
		struct gpu_array_info *array = &prog->array[i];
		isl_set *set;
		isl_union_set *uset;
		isl_union_map *order;

		set = isl_set_universe(isl_space_copy(array->space));
		uset = isl_union_set_from_set(set);
		uset = isl_union_map_domain(
		    isl_union_map_intersect_range(isl_union_map_copy(accesses),
						    uset));
		order = isl_union_map_copy(prog->scop->tagged_dep_order);
		order = isl_union_map_intersect_domain(order, uset);
		order = isl_union_map_zip(order);
		order = isl_union_set_unwrap(isl_union_map_domain(order));
		order = remove_independences(prog, array, order);
		array->dep_order = order;

		if (gpu_array_is_scalar(array))
			continue;

		prog->array_order = isl_union_map_union(prog->array_order,
					isl_union_map_copy(array->dep_order));
	}

	isl_union_map_free(accesses);
}

/* Construct a gpu_array_info for each array possibly accessed by "prog" and
 * collect them in prog->array.
 *
 * If there are any member accesses involved, then they are first mapped
 * to the outer arrays of structs.
 *
 * If we are allowing live range reordering, then also set
 * the dep_order field.  Otherwise leave it NULL.
 */
static int collect_array_info(struct gpu_prog *prog)
{
	int r;
	isl_union_set *arrays;

	arrays = isl_union_map_range(isl_union_map_copy(prog->read));
	arrays = isl_union_set_union(arrays,
		    isl_union_map_range(isl_union_map_copy(prog->may_write)));

	arrays = isl_union_set_apply(arrays,
					isl_union_map_copy(prog->to_outer));

	arrays = isl_union_set_coalesce(arrays);

	prog->n_array = isl_union_set_n_set(arrays);
	prog->array = isl_calloc_array(prog->ctx,
				     struct gpu_array_info, prog->n_array);
	assert(prog->array);
	prog->n_array = 0;
	r = isl_union_set_foreach_set(arrays, &extract_array_info, prog);
	isl_union_set_free(arrays);

	if (prog->scop->options->live_range_reordering)
		collect_order_dependences(prog);

	return r;
}

static void free_array_info(struct gpu_prog *prog)
{
	int i, j;

	for (i = 0; i < prog->n_array; ++i) {
		int n_index = prog->array[i].n_index;
		free(prog->array[i].type);
		free(prog->array[i].name);
		for (j = 0; j < n_index; ++j)
			isl_pw_aff_free(prog->array[i].bound[j]);
		isl_space_free(prog->array[i].space);
		isl_set_free(prog->array[i].extent);
		free(prog->array[i].bound);
		free(prog->array[i].refs);
		isl_union_map_free(prog->array[i].dep_order);
	}
	free(prog->array);
}

/* Check if a gpu array is a scalar.  A scalar is a value that is not stored
 * as an array or through a pointer reference, but as a single data element.
 * At the moment, scalars are represented as zero-dimensional arrays.
 * A zero-dimensional array containing structures is not considered
 * to be a scalar.
 */
int gpu_array_is_scalar(struct gpu_array_info *array)
{
	return !array->has_compound_element && array->n_index == 0;
}

/* Is "array" a read-only scalar?
 */
int gpu_array_is_read_only_scalar(struct gpu_array_info *array)
{
	return array->read_only_scalar;
}

/* Return the set of parameter values for which the array has a positive
 * size in all dimensions.
 * If the sizes are only valid for some parameter values, then those
 * constraints are also taken into account.
 */
__isl_give isl_set *gpu_array_positive_size_guard(struct gpu_array_info *array)
{
	int i;
	isl_space *space;
	isl_set *guard;

	space = isl_space_params(isl_space_copy(array->space));
	guard = isl_set_universe(space);

	for (i = 0; i < array->n_index; ++i) {
		isl_pw_aff *bound;
		isl_set *guard_i, *zero;

		bound = isl_pw_aff_copy(array->bound[i]);
		guard_i = isl_pw_aff_nonneg_set(isl_pw_aff_copy(bound));
		zero = isl_pw_aff_zero_set(bound);
		guard_i = isl_set_subtract(guard_i, zero);
		guard = isl_set_intersect(guard, guard_i);
	}

	return guard;
}

/* Internal data structure for extract_size_of_type.
 * "type" specifies the name of the space that we want to extract.
 * "res" is used to store the subset of that space.
 */
struct ppcg_extract_size_data {
	const char *type;
	isl_set *res;
};

/* This function is called for each set in a union_set.
 * If the name of the set matches data->type, we store the
 * set in data->res.
 */
static int extract_size_of_type(__isl_take isl_set *size, void *user)
{
	struct ppcg_extract_size_data *data = user;
	const char *name;

	name = isl_set_get_tuple_name(size);
	if (name && !strcmp(name, data->type)) {
		data->res = size;
		return -1;
	}

	isl_set_free(size);
	return 0;
}

/* Given a union map { kernel[i] -> *[...] },
 * return the range in the space called "type" for the kernel with
 * sequence number "id".
 */
static __isl_give isl_set *extract_sizes(__isl_keep isl_union_map *sizes,
	const char *type, int id)
{
	isl_space *space;
	isl_set *dom;
	isl_union_set *local_sizes;
	struct ppcg_extract_size_data data = { type, NULL };

	if (!sizes)
		return NULL;

	space = isl_union_map_get_space(sizes);
	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, 1);
	space = isl_space_set_tuple_name(space, isl_dim_set, "kernel");
	dom = isl_set_universe(space);
	dom = isl_set_fix_si(dom, isl_dim_set, 0, id);

	local_sizes = isl_union_set_apply(isl_union_set_from_set(dom),
					isl_union_map_copy(sizes));
	isl_union_set_foreach_set(local_sizes, &extract_size_of_type, &data);
	isl_union_set_free(local_sizes);
	return data.res;
}

/* Given a singleton set, extract the first (at most *len) elements
 * of the single integer tuple into *sizes and update *len if needed.
 */
static void read_sizes_from_set(__isl_take isl_set *set, int *sizes, int *len)
{
	int i;
	int dim;

	if (!set)
		return;

	dim = isl_set_dim(set, isl_dim_set);
	if (dim < *len)
		*len = dim;

	for (i = 0; i < *len; ++i) {
		isl_val *v;

		v = isl_set_plain_get_val_if_fixed(set, isl_dim_set, i);
		assert(v);

		sizes[i] = isl_val_get_num_si(v);
		isl_val_free(v);
	}

	isl_set_free(set);
}

/* Add the map { kernel[id] -> type[sizes] } to gen->used_sizes,
 * if the option debug->dump_sizes is set.
 */
static void set_used_sizes(struct gpu_gen *gen, const char *type, int id,
	int *sizes, int len)
{
	int i;
	isl_space *space;
	isl_map *map;

	if (!gen->options->debug->dump_sizes)
		return;

	space = isl_union_map_get_space(gen->used_sizes);
	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, 1);
	space = isl_space_set_tuple_name(space, isl_dim_set, "kernel");
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, len);
	space = isl_space_set_tuple_name(space, isl_dim_out, type);

	map = isl_map_universe(space);
	map = isl_map_fix_si(map, isl_dim_in, 0, id);
	for (i = 0; i < len; ++i)
		map = isl_map_fix_si(map, isl_dim_out, i, sizes[i]);

	gen->used_sizes = isl_union_map_add_map(gen->used_sizes, map);
}

/* Extract user specified "tile" sizes from the "sizes" command line option,
 * defaulting to option->tile_size in each dimension.
 * Add the effectively used sizes to gen->used_sizes.
 */
static void read_tile_sizes(struct gpu_gen *gen)
{
	int n;
	isl_set *size;

	gen->tile_size = isl_alloc_array(gen->ctx, int, gen->tile_len);
	assert(gen->tile_size);
	for (n = 0; n < gen->tile_len; ++n)
		gen->tile_size[n] = gen->options->tile_size;

	size = extract_sizes(gen->sizes, "tile", gen->kernel_id);
	read_sizes_from_set(size, gen->tile_size, &gen->tile_len);
	set_used_sizes(gen, "tile", gen->kernel_id,
			gen->tile_size, gen->tile_len);

	if (gen->n_parallel > gen->tile_len)
		gen->n_parallel = gen->tile_len;
}

/* Extract user specified "block" sizes from the "sizes" command line option,
 * after filling in some potentially useful defaults.
 * Add the effectively used sizes to gen->used_sizes.
 */
static void read_block_sizes(struct gpu_gen *gen)
{
	int n;
	isl_set *size;

	n = gen->n_parallel;
	gen->n_block = (n <= 3) ? n : 3;
	switch (gen->n_block) {
	case 1:
		gen->block_dim[0] = 512;
		break;
	case 2:
		gen->block_dim[0] = 32;
		gen->block_dim[1] = 16;
		break;
	default:
		gen->block_dim[0] = 32;
		gen->block_dim[1] = 4;
		gen->block_dim[2] = 4;
		break;
	}

	size = extract_sizes(gen->sizes, "block", gen->kernel_id);
	read_sizes_from_set(size, gen->block_dim, &gen->n_block);
	set_used_sizes(gen, "block", gen->kernel_id,
			gen->block_dim, gen->n_block);
}

/* Extract user specified "grid" sizes from the "sizes" command line option,
 * after filling in some potentially useful defaults.
 * Add the effectively used sizes to gen->used_sizes.
 */
static void read_grid_sizes(struct gpu_gen *gen)
{
	int n = gen->n_parallel;
	isl_set *size;

	gen->n_grid = (n <= 2) ? n : 2;
	switch (gen->n_grid) {
	case 1:
		gen->grid_dim[0] = 32768;
		break;
	default:
		gen->grid_dim[0] = 256;
		gen->grid_dim[1] = 256;
		break;
	}

	size = extract_sizes(gen->sizes, "grid", gen->kernel_id);
	read_sizes_from_set(size, gen->grid_dim, &gen->n_grid);
	set_used_sizes(gen, "grid", gen->kernel_id, gen->grid_dim, gen->n_grid);
}

/* Extract user specified sizes from the "sizes" command line option
 * after filling in some potentially useful defaults.
 */
static void read_sizes(struct gpu_gen *gen)
{
	read_tile_sizes(gen);
	read_block_sizes(gen);
	read_grid_sizes(gen);
}

static void *free_stmts(struct gpu_stmt *stmts, int n)
{
	int i;

	if (!stmts)
		return NULL;

	for (i = 0; i < n; ++i) {
		struct gpu_stmt_access *access, *next;

		for (access = stmts[i].accesses; access; access = next) {
			next = access->next;
			isl_id_free(access->ref_id);
			isl_map_free(access->access);
			isl_map_free(access->tagged_access);
			free(access);
		}

		isl_id_free(stmts[i].id);
	}
	free(stmts);

	return NULL;
}

/* Construct a map from a domain of dimensionality "len"
 * to a domain of dimensionality "len" + "tile_len" that tiles
 * the "tile_len" coordinates starting at "first".
 * In particular, [s_i] -> [s_i / tile_size[i], s_i % tile_size[i]].
 * "dim" prescribes the parameters.
 */
static __isl_give isl_map *tile(__isl_take isl_space *dim, int len,
        int first, int tile_len, int *tile_size)
{
	int i;
	isl_basic_map *bmap;
	isl_constraint *c;
	isl_local_space *ls;

	dim = isl_space_add_dims(dim, isl_dim_in, len);
	dim = isl_space_add_dims(dim, isl_dim_out, len + tile_len);
	bmap = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len - tile_len; ++i) {
		int j = i < first ? i : i + tile_len;
		int k = i < first ? i : i + 2 * tile_len;

		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, j, -1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, k, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	for (i = 0; i < tile_len; ++i) {
		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in,
						first + i, -1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						first + i, tile_size[i]);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						first + i + tile_len, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);

		c = isl_inequality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						   first + i + tile_len, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);

		c = isl_inequality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						   first + i + tile_len, -1);
		c = isl_constraint_set_constant_si(c, tile_size[i] - 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	isl_local_space_free(ls);

	return isl_map_from_basic_map(bmap);
}

/* Construct a map from a domain of dimensionality "len"
 * to a domain of dimensionality "len" + "wrap_len" that "wraps"
 * the "wrap_len" coordinates starting at "first" according to "wrap_size".
 * In particular, [s_i] -> [s_i, s_i % wrap_size[i]].
 * To do so, we need extra variables corresponding to [s_i / wrap_size[i]],
 * that are projected out at the end.
 * "dim" prescribes the parameters.
 */
static __isl_give isl_map *wrap(__isl_take isl_space *dim, int len,
        int first, int wrap_len, int *wrap_size)
{
	int i;
	isl_basic_map *bmap;
	isl_constraint *c;
	isl_local_space *ls;

	dim = isl_space_add_dims(dim, isl_dim_in, len);
	dim = isl_space_add_dims(dim, isl_dim_out, len + 2 * wrap_len);
	bmap = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len; ++i) {
		int k = i < first + wrap_len ? i : i + 2 * wrap_len;

		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, i, -1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, k, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	for (i = 0; i < wrap_len; ++i) {
		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + i, -1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + wrap_len + i, 1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
				    first + 2 * wrap_len + i, wrap_size[i]);
		bmap = isl_basic_map_add_constraint(bmap, c);

		c = isl_inequality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + wrap_len + i, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);

		c = isl_inequality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + wrap_len + i, -1);
		c = isl_constraint_set_constant_si(c, wrap_size[i] - 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	isl_local_space_free(ls);

	bmap = isl_basic_map_project_out(bmap, isl_dim_out,
				first + 2 * wrap_len, wrap_len);

	return isl_map_from_basic_map(bmap);
}

/* Add "n" parameters named prefix%d.
 */
static __isl_give isl_set *add_params( __isl_take isl_set *set,
	int n, const char *prefix)
{
	int i;
	unsigned nparam;
	char name[20];

	nparam = isl_set_dim(set, isl_dim_param);
	set = isl_set_add_dims(set, isl_dim_param, n);

	for (i = 0; i < n; ++i) {
		snprintf(name, sizeof(name), "%s%d", prefix, i);
		set = isl_set_set_dim_name(set, isl_dim_param,
					    nparam + i, name);
	}

	return set;
}

/* Equate the "n" dimensions of "set" starting at "first" to
 * freshly created parameters named prefix%d.
 */
static __isl_give isl_set *parametrize(__isl_take isl_set *set,
	int first, int n, const char *prefix)
{
	int i;
	unsigned nparam;

	nparam = isl_set_dim(set, isl_dim_param);

	set = add_params(set, n, prefix);

	for (i = 0; i < n; ++i)
		set = isl_set_equate(set, isl_dim_param, nparam + i,
					isl_dim_set, first + i);

	return set;
}

/* Given a parameter space "space", create a set of dimension "len"
 * of which the "n" dimensions starting at "first" are equated to
 * freshly created parameters named prefix%d.
 */
static __isl_give isl_set *parametrization(__isl_take isl_space *space,
	int len, int first, int n, const char *prefix)
{
	isl_set *set;

	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, len);
	set = isl_set_universe(space);

	return parametrize(set, first, n, prefix);
}

/* Tile the B loops over the tile sizes and then tile/wrap
 * the T1 loops over the blocks.
 */
static __isl_give isl_union_map *tile_schedule(struct gpu_gen *gen,
	__isl_take isl_union_map *sched)
{
	isl_space *dim;
	isl_map *tiling, *block_tiling;

	dim = isl_union_map_get_space(sched);
	tiling = tile(isl_space_copy(dim), gen->untiled_len,
		      gen->tile_first, gen->tile_len, gen->tile_size);

	if (gen->options->wrap)
		block_tiling = wrap(dim, gen->untiled_len + gen->tile_len,
				gen->tile_first, gen->n_grid, gen->grid_dim);
	else
		block_tiling = tile(dim, gen->untiled_len + gen->tile_len,
				gen->tile_first, gen->n_grid, gen->grid_dim);

	gen->tiled_len = gen->untiled_len + gen->tile_len + gen->n_grid;

	tiling = isl_map_apply_range(tiling, block_tiling);

	sched = isl_union_map_apply_range(sched,
					     isl_union_map_from_map(tiling));

	gen->shared_len = gen->tile_first + gen->tile_len + gen->n_grid;

	return sched;
}

/* Equate the "T1P" iterators in the tiled schedule "sched"
 * to the block dimensions.
 */
static __isl_give isl_union_map *parametrize_tiled_schedule(
	struct gpu_gen *gen, __isl_take isl_union_map *sched)
{
	isl_space *dim;
	isl_set *par;

	dim = isl_union_map_get_space(sched);
	par = parametrization(dim, gen->tiled_len,
		gen->tile_first + gen->n_grid, gen->n_grid, "b");
	sched = isl_union_map_intersect_range(sched,
						isl_union_set_from_set(par));

	return sched;
}

/* Tile/wrap the P1 loops over the threads.
 */
static __isl_give isl_union_map *thread_tile_schedule(struct gpu_gen *gen,
	__isl_take isl_union_map *sched)
{
	isl_space *dim;
	isl_map *tiling;
	isl_set *par;

	dim = isl_union_map_get_space(sched);

	if (gen->options->wrap)
		tiling = wrap(isl_space_copy(dim), gen->tiled_len,
				gen->shared_len, gen->n_block, gen->block_dim);
	else
		tiling = tile(isl_space_copy(dim), gen->tiled_len,
				gen->shared_len, gen->n_block, gen->block_dim);
	gen->thread_tiled_len = gen->tiled_len + gen->n_block;

	sched = isl_union_map_apply_range(sched,
					     isl_union_map_from_map(tiling));

	par = parametrization(dim, gen->thread_tiled_len,
		gen->tile_first + gen->tile_len + gen->n_grid + gen->n_block,
		gen->n_block, "t");
	sched = isl_union_map_intersect_range(sched,
						isl_union_set_from_set(par));

	gen->shared_len = gen->tile_first + gen->tile_len + gen->n_grid;

	return sched;
}

/* If the user asked for it, scale the shared memory tile loops
 * (T1T and T2) of "sched" by gen->tile_size[i].
 * If we are not performing "wrapping", then additionally scale the T1P
 * loops by gen->grid_dim[i].
 */
static __isl_give isl_union_map *scale_tile_loops(struct gpu_gen *gen,
	__isl_take isl_union_map *sched)
{
	int i;
	isl_space *dim;
	isl_basic_map *scale;
	isl_constraint *c;
	isl_local_space *ls;

	if (!gen->options->scale_tile_loops)
		return sched;

	dim = isl_union_map_get_space(sched);
	dim = isl_space_add_dims(dim, isl_dim_in, gen->tiled_len);
	dim = isl_space_add_dims(dim, isl_dim_out, gen->tiled_len);
	scale = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < gen->tiled_len; ++i) {
		int f = 1;

		if (i >= gen->tile_first && i < gen->tile_first + gen->n_grid) {
			f = gen->tile_size[i - gen->tile_first];
			if (!gen->options->wrap)
				f *= gen->grid_dim[i - gen->tile_first];
		} else if (i >= gen->tile_first + gen->n_grid &&
			   i < gen->tile_first + gen->n_grid + gen->tile_len) {
			f = gen->tile_size[i - (gen->tile_first + gen->n_grid)];
		}

		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, i, f);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
		scale = isl_basic_map_add_constraint(scale, c);
	}

	isl_local_space_free(ls);

	sched = isl_union_map_apply_range(sched,
		isl_union_map_from_map(isl_map_from_basic_map(scale)));

	return sched;
}

/* If we are not performing "wrapping" and if the user asked for it,
 * scale the thread tile loops (P1T) of "sched" by gen->block_dim[i].
 */
static __isl_give isl_union_map *scale_thread_tile_loops(struct gpu_gen *gen,
	__isl_take isl_union_map *sched)
{
	int i;
	isl_space *dim;
	isl_basic_map *scale;
	isl_constraint *c;
	isl_local_space *ls;

	if (gen->options->wrap)
		return sched;
	if (!gen->options->scale_tile_loops)
		return sched;

	dim = isl_union_map_get_space(sched);
	dim = isl_space_add_dims(dim, isl_dim_in, gen->thread_tiled_len);
	dim = isl_space_add_dims(dim, isl_dim_out, gen->thread_tiled_len);
	scale = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < gen->thread_tiled_len; ++i) {
		int f = 1;

		if (i >= gen->shared_len &&
		    i < gen->shared_len + gen->n_block)
			f = gen->block_dim[i - gen->shared_len];

		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, i, f);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
		scale = isl_basic_map_add_constraint(scale, c);
	}

	isl_local_space_free(ls);

	sched = isl_union_map_apply_range(sched,
		isl_union_map_from_map(isl_map_from_basic_map(scale)));

	return sched;
}

/* If we are not performing "wrapping" and if the user asked for it,
 * scale the "n_tile" loops starting at "first" of "sched" by gen->block_dim[i].
 */
static __isl_give isl_union_map *scale_access_tile_loops(struct gpu_gen *gen,
	__isl_take isl_union_map *sched, int len, int first, int n_tile)
{
	int i;
	isl_space *dim;
	isl_basic_map *scale;
	isl_constraint *c;
	isl_local_space *ls;

	if (gen->options->wrap)
		return sched;
	if (!gen->options->scale_tile_loops)
		return sched;

	dim = isl_union_map_get_space(sched);
	dim = isl_space_add_dims(dim, isl_dim_in, len);
	dim = isl_space_add_dims(dim, isl_dim_out, len);
	scale = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len; ++i) {
		int f = 1;

		if (i >= first && i < first + n_tile)
			f = gen->kernel->block_dim[i - first];

		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, i, f);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
		scale = isl_basic_map_add_constraint(scale, c);
	}

	isl_local_space_free(ls);

	sched = isl_union_map_apply_range(sched,
		isl_union_map_from_map(isl_map_from_basic_map(scale)));

	return sched;
}

/* Add "len" parameters p[i] called prefix%d,
 * with bounds to 0 <= p[i] < size[i].
 */
__isl_give isl_set *add_bounded_parameters(__isl_take isl_set *set,
	int len, int *size, const char *prefix)
{
	int i;
	unsigned nparam;
	isl_space *dim;
	isl_basic_set *bset;
	isl_constraint *c;
	isl_local_space *ls;
	char name[20];

	nparam = isl_set_dim(set, isl_dim_param);
	set = isl_set_add_dims(set, isl_dim_param, len);

	for (i = 0; i < len; ++i) {
		snprintf(name, sizeof(name), "%s%d", prefix, i);
		set = isl_set_set_dim_name(set, isl_dim_param,
					    nparam + i, name);
	}

	dim = isl_set_get_space(set);
	bset = isl_basic_set_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len; ++i) {
		c = isl_inequality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_param,
							nparam + i, 1);
		bset = isl_basic_set_add_constraint(bset, c);
	
		c = isl_inequality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_param,
							nparam + i, -1);
		c = isl_constraint_set_constant_si(c, size[i] - 1);
		bset = isl_basic_set_add_constraint(bset, c);
	}

	isl_local_space_free(ls);

	return isl_set_intersect(set, isl_set_from_basic_set(bset));
}

/* Add "len" parameters p[i] called prefix%d and intersect "set"
 * with
 *
 *	{ : 0 <= p[i] < size[i] }
 *
 * or an overapproximation.
 */
static __isl_give isl_set *add_bounded_parameters_dynamic(
	__isl_take isl_set *set, __isl_keep isl_multi_pw_aff *size,
	const char *prefix)
{
	int i, len;
	unsigned nparam;
	isl_space *space;
	isl_local_space *ls;
	char name[20];

	len = isl_multi_pw_aff_dim(size, isl_dim_out);
	nparam = isl_set_dim(set, isl_dim_param);
	set = isl_set_add_dims(set, isl_dim_param, len);

	for (i = 0; i < len; ++i) {
		snprintf(name, sizeof(name), "%s%d", prefix, i);
		set = isl_set_set_dim_name(set, isl_dim_param,
					    nparam + i, name);
	}

	space = isl_space_params(isl_set_get_space(set));
	ls = isl_local_space_from_space(space);
	for (i = 0; i < len; ++i) {
		isl_pw_aff *param, *size_i, *zero;
		isl_set *bound;

		param = isl_pw_aff_var_on_domain(isl_local_space_copy(ls),
						isl_dim_param, nparam + i);

		size_i = isl_multi_pw_aff_get_pw_aff(size, i);
		bound = isl_pw_aff_lt_set(isl_pw_aff_copy(param), size_i);
		bound = isl_set_from_basic_set(isl_set_simple_hull(bound));
		set = isl_set_intersect_params(set, bound);

		zero = isl_pw_aff_zero_on_domain(isl_local_space_copy(ls));
		bound = isl_pw_aff_ge_set(param, zero);
		set = isl_set_intersect_params(set, bound);
	}
	isl_local_space_free(ls);

	return set;
}

/* Construct a map from an access to group->array to the corresponding
 * shared/private memory tile.
 * The map is of the form
 *
 *	{ [D[i] -> A[a]] -> T[t] }
 *
 * where D represents the initial shared_len dimensions
 * of the computed schedule.
 */
static __isl_give isl_map *shift_access(struct gpu_array_ref_group *group)
{
	struct gpu_array_tile *tile;
	isl_multi_aff *tiling;

	tile = group->private_tile;
	if (!tile)
		tile = group->shared_tile;

	tiling = isl_multi_aff_copy(tile->tiling);

	return isl_map_from_multi_aff(tiling);
}

/* Does "map" have an obviously fixed value at variable "pos" of "type"?
 */
static int map_plain_is_fixed(isl_map *map, enum isl_dim_type type,
	unsigned pos)
{
	isl_val *v;
	int fixed;

	v = isl_map_plain_get_val_if_fixed(map, type, pos);
	if (!v)
		return -1;
	fixed = isl_val_is_int(v);
	isl_val_free(v);

	return fixed;
}

/* Given a schedule that iterates over all elements in a piece of an array,
 * perform tiling/wrapping over the threads.
 *
 * In particular, we tile the final iterators so that the final thread
 * dimension runs over the final array dimension.
 * However, if those final iterators have only a single iteration,
 * we try to tile earlier iterators instead.
 */
static __isl_give isl_map *tile_access_schedule(struct gpu_gen *gen,
	__isl_take isl_map *sched)
{
	isl_space *dim;
	isl_union_map *usched;
	isl_map *tiling;
	isl_set *par;
	unsigned nvar = isl_map_dim(sched, isl_dim_out);
	int n_tile;
	int first;

	n_tile = gen->kernel->n_block;
	if (n_tile > nvar) {
		int i;
		sched = isl_map_insert_dims(sched,
						isl_dim_out, 0, n_tile - nvar);
		for (i = 0; i < n_tile - nvar; ++i)
			sched = isl_map_fix_si(sched, isl_dim_out, i, 0);
		nvar = n_tile;
	}

	first = nvar - n_tile;

	for (; first > 0; first --)
		if (!map_plain_is_fixed(sched, isl_dim_out, first + n_tile - 1))
			break;

	dim = isl_map_get_space(sched);
	dim = isl_space_params(dim);
	if (gen->options->wrap)
		tiling = wrap(isl_space_copy(dim), nvar, first,
				n_tile, gen->kernel->block_dim);
	else
		tiling = tile(isl_space_copy(dim), nvar, first,
				n_tile, gen->kernel->block_dim);
	sched = isl_map_apply_range(sched, tiling);

	par = parametrization(dim, nvar + n_tile, first + n_tile, n_tile, "t");
	sched = isl_map_intersect_range(sched, par);

	usched = isl_union_map_from_map(sched);
	usched = scale_access_tile_loops(gen, usched, nvar + n_tile,
					 first, n_tile);
	sched = isl_map_from_union_map(usched);

	return sched;
}

/* Return the union of all read (read = 1) and/or write (write = 1)
 * access relations in the group.
 */
static __isl_give isl_union_map *group_access_relation(
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

/* Return the union of all tagged access relations in the group.
 */
static __isl_give isl_union_map *group_tagged_access_relation(
	struct gpu_array_ref_group *group)
{
	int i;
	isl_union_map *access;

	access = isl_union_map_empty(isl_map_get_space(group->access));
	for (i = 0; i < group->n_ref; ++i) {
		isl_map *map_i;

		map_i = isl_map_copy(group->refs[i]->tagged_access);
		access = isl_union_map_union(access,
					    isl_union_map_from_map(map_i));
	}

	return access;
}

/* Return the extent of "array", recomputed from the bounds.
 * The recomputed extent may be simpler than the original extent.
 */
static __isl_give isl_set *array_extent(struct gpu_array_info *array)
{
	int i;
	isl_id *id;
	isl_space *space;
	isl_local_space *ls;
	isl_set *extent;

	id = isl_set_get_tuple_id(array->extent);
	space = isl_set_get_space(array->extent);
	extent = isl_set_universe(isl_space_copy(space));
	ls = isl_local_space_from_space(space);
	for (i = 0; i < array->n_index; ++i) {
		isl_pw_aff *bound;
		isl_aff *aff;
		isl_pw_aff *index;
		isl_set *lt;

		extent = isl_set_lower_bound_si(extent, isl_dim_set, i, 0);

		aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
						isl_dim_set, i);
		index = isl_pw_aff_from_aff(aff);
		bound = isl_pw_aff_copy(array->bound[i]);
		bound = isl_pw_aff_from_range(bound);
		bound = isl_pw_aff_add_dims(bound, isl_dim_in, array->n_index);
		bound = isl_pw_aff_set_tuple_id(bound, isl_dim_in,
						isl_id_copy(id));
		lt = isl_pw_aff_lt_set(index, bound);
		extent = isl_set_intersect(extent, lt);
	}
	isl_local_space_free(ls);
	isl_id_free(id);

	return extent;
}

/* Return a map from the first shared_len dimensions of the computed
 * schedule to the array tile in
 * global memory that corresponds to the shared memory copy.
 *
 * In particular, return a map
 *
 *	{ D[i] -> A[a] }
 *
 * with constraints
 *
 *	tile_offset(i) <= a <= tile_offset(i) + tile_size - 1		(1)
 *
 * and
 *
 *	0 <= a <= array_size - 1					(2)
 *
 * Note that if some stride has been detected (i.e., when
 * group->shared_tile->bound[i].shift is set), then a in (1) refers
 * to the shifted and scaled down version.
 *
 * Constraints (1) are obtained by mapping the size constraints on the
 * shared/private memory tile back to the access relation.
 * Constraints (2) are obtained from the (recomputed) extent.
 */
static __isl_give isl_map *group_tile(struct gpu_array_ref_group *group)
{
	int i;
	int n_index = group->array->n_index;
	isl_map *tile;
	isl_space *space;
	isl_set *local;
	isl_set *extent;

	space = isl_multi_aff_get_space(group->shared_tile->tiling);
	space = isl_space_range(space);
	local = isl_set_universe(space);
	for (i = 0; i < n_index; ++i) {
		isl_val *bound;

		local = isl_set_lower_bound_si(local, isl_dim_set, i, 0);
		bound = isl_val_copy(group->shared_tile->bound[i].size);
		bound = isl_val_sub_ui(bound, 1);
		local = isl_set_upper_bound_val(local, isl_dim_set, i, bound);
	}
	local = isl_set_preimage_multi_aff(local,
				isl_multi_aff_copy(group->shared_tile->tiling));
	tile = isl_set_unwrap(local);
	extent = array_extent(group->array);
	tile = isl_map_intersect_range(tile, extent);

	return tile;
}

/* Given a mapping "iterator_map" from the AST schedule to a domain,
 * return the corresponding mapping from the AST schedule to
 * to the first shared_len dimensions of the schedule computed by PPCG.
 */
static __isl_give isl_pw_multi_aff *compute_sched_to_shared(struct gpu_gen *gen,
	__isl_take isl_pw_multi_aff *iterator_map)
{
	isl_union_map *umap;
	isl_space *space;
	isl_map *map, *sched;;

	space = isl_space_range(isl_pw_multi_aff_get_space(iterator_map));
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, gen->shared_len);

	umap = isl_union_map_copy(gen->shared_sched);
	umap = isl_union_map_apply_range(umap,
			isl_union_map_copy(gen->shared_proj));
	map = isl_union_map_extract_map(umap, space);
	isl_union_map_free(umap);

	sched = isl_map_preimage_domain_pw_multi_aff(map, iterator_map);
	sched = isl_map_detect_equalities(sched);

	return isl_pw_multi_aff_from_map(sched);
}

/* Set unroll[j] if the input dimension j is involved in
 * the index expression represented by ma.
 */
static int check_unroll(__isl_take isl_set *set, __isl_take isl_multi_aff *ma,
	void *user)
{
	int i, j;
	int n_in = isl_multi_aff_dim(ma, isl_dim_in);
	int n_out = isl_multi_aff_dim(ma, isl_dim_out);
	int *unroll = user;

	for (i = 0; i < n_out; ++i) {
		isl_aff *aff;

		aff = isl_multi_aff_get_aff(ma, i);
		for (j = 0; j < n_in; ++j)
			if (isl_aff_involves_dims(aff, isl_dim_in, j, 1))
				unroll[j] = 1;
		isl_aff_free(aff);
	}

	isl_set_free(set);
	isl_multi_aff_free(ma);
	return 0;
}

/* Given an array pos mapping input dimensions to the corresponding
 * output dimension, construct the corresponding map.
 */
static __isl_give isl_map *permutation(__isl_take isl_space *dim,
	int *pos, int len)
{
	int i;
	isl_constraint *c;
	isl_basic_map *bmap;
	isl_local_space *ls;

	dim = isl_space_add_dims(dim, isl_dim_in, len);
	dim = isl_space_add_dims(dim, isl_dim_out, len);
	bmap = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len; ++i) {
		c = isl_equality_alloc(isl_local_space_copy(ls));
		c = isl_constraint_set_coefficient_si(c, isl_dim_in, i,
						      -1);
		c = isl_constraint_set_coefficient_si(c, isl_dim_out, pos[i],
						      1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}
	isl_local_space_free(ls);

	return isl_map_from_basic_map(bmap);
}

/* Remove the private tiles from all array reference groups,
 * except for the groups of arrays that are marked force_private.
 */
static void remove_private_tiles(struct gpu_gen *gen)
{
	int i, j;

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		if (array->force_private)
			continue;

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group = array->groups[j];

			group->private_tile = free_tile(group->private_tile);
		}
	}
}

/* Find all loops involved in any of the index expressions for any of
 * the private accesses, move them innermost and then mark them as
 * requiring unrolling by setting gen->first_unroll.
 * The loops involved should all be parallel because of the checks
 * we performed in check_private_group_access.  Moving them innermost
 * is therefore a valid transformation.
 *
 * If any of the arrays are marked force_private, however, then
 * those loops may not be parallel with respect to the marked arrays.
 * If any of the loops would have to be moved innermost for the
 * (non forced) private accesses and if there are any force_private
 * arrays, then we revert the decision to map the selected arrays
 * to private memory.  An alternative solution would be to expand
 * the force_private arrays.
 *
 * Loops up to gen->shared_len are generated before the mapping to
 * threads is applied.  They should therefore be ignored.
 *
 * We compute the hidden equalities of the schedule first
 * since we will need them in our calls to isl_pw_multi_aff_from_map
 * and because we want to make sure that the same equalities
 * are also available to the code generator.
 */
static __isl_give isl_union_map *interchange_for_unroll(struct gpu_gen *gen,
	__isl_take isl_union_map *sched)
{
	int i, j;
	int unroll[gen->thread_tiled_len];
	int perm[gen->thread_tiled_len];
	isl_space *dim;
	isl_map *permute;
	int len = gen->shared_len + gen->n_parallel + gen->n_block;

	gen->first_unroll = -1;

	sched = isl_union_map_detect_equalities(sched);
	for (i = 0; i < gen->thread_tiled_len; ++i)
		unroll[i] = 0;
	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			isl_union_map *access;
			isl_map *acc;
			isl_pw_multi_aff *pma;

			if (!array->groups[j]->private_tile)
				continue;

			access = group_access_relation(array->groups[j], 1, 1);
			access = isl_union_map_apply_domain(access,
						isl_union_map_copy(sched));

			acc = isl_map_from_union_map(access);
			pma = isl_pw_multi_aff_from_map(acc);
			isl_pw_multi_aff_foreach_piece(pma,
							&check_unroll, unroll);

			isl_pw_multi_aff_free(pma);
		}
	}

	for (i = gen->shared_len; i < len; ++i)
		if (unroll[i])
			break;

	if (i >= len)
		return sched;

	for (i = len; i < gen->thread_tiled_len; ++i)
		if (unroll[i])
			return sched;

	if (gen->any_force_private) {
		remove_private_tiles(gen);
		return sched;
	}

	j = 0;
	for (i = 0; i < gen->shared_len; ++i)
		perm[i] = j++;
	for (i = gen->shared_len; i < gen->thread_tiled_len; ++i)
		if (!unroll[i])
			perm[i] = j++;
	gen->first_unroll = j - gen->shared_len;
	for (i = gen->shared_len; i < len; ++i)
		if (unroll[i])
			perm[i] = j++;

	dim = isl_union_map_get_space(sched);
	permute = permutation(dim, perm, gen->thread_tiled_len);
	sched = isl_union_map_apply_range(sched,
					  isl_union_map_from_map(permute));

	return sched;
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
 * The mapping recorded in bound->shift_map is of the form
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

	bound->shift_map = isl_basic_map_apply_range(shift, scale);
	bmap = isl_basic_map_copy(bound->shift_map);
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

/* Construct a map with input the shared tile loops and the loops that
 * will be wrapped around the threads that relates these later loops
 * to the thread indices and then projects them out.
 */
static __isl_give isl_map *compute_privatization(struct gpu_gen *gen)
{
	isl_map *priv;
	isl_map *tiling;
	isl_map *proj;
	isl_set *par;
	isl_space *dim;

	dim = isl_union_map_get_space(gen->shared_sched);

	if (gen->options->wrap)
		tiling = wrap(isl_space_copy(dim), gen->shared_len + gen->n_block,
				gen->shared_len, gen->n_block, gen->block_dim);
	else
		tiling = tile(isl_space_copy(dim), gen->shared_len + gen->n_block,
				gen->shared_len, gen->n_block, gen->block_dim);

	priv = tiling;

	par = parametrization(dim, gen->shared_len + 2 * gen->n_block,
		gen->tile_first + gen->tile_len + gen->n_grid + gen->n_block,
		gen->n_block, "t");

	priv = isl_map_align_params(priv, isl_set_get_space(par));
	priv = isl_map_intersect_range(priv, par);

	dim = isl_map_get_space(priv);
	dim = isl_space_drop_dims(dim, isl_dim_in, 0, isl_space_dim(dim, isl_dim_in));
	dim = isl_space_drop_dims(dim, isl_dim_out, 0, isl_space_dim(dim, isl_dim_out));
	proj = projection(dim, gen->shared_len + 2 * gen->n_block,
			  gen->shared_len);

	priv = isl_map_apply_range(priv, proj);

	return priv;
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
 * This function is only called for access relations without reuse.
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

	access = isl_map_copy(access);
	space = isl_space_params(isl_map_get_space(access));
	par = parametrization(space, gen->shared_len + gen->n_block,
				0, gen->shared_len, "s");
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

/* Compute a privatized copy of all access relations from reference groups that
 * are mapped to private memory and store the result in gen->privatization.
 *
 * Read-only scalars and arrays containing structures are not mapped
 * to private memory.
 */
static void compute_private_access(struct gpu_gen *gen)
{
	int i, j;
	isl_union_map *private;

	if (!gen->options->use_private_memory)
		return;

	private = isl_union_map_empty(isl_union_map_get_space(gen->shared_sched));

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		if (gpu_array_is_read_only_scalar(array))
			continue;
		if (array->has_compound_element)
			continue;

		for (j = 0; j < array->n_group; ++j) {
			if (!array->groups[j]->private_tile)
				continue;

			private = isl_union_map_union(private,
				group_access_relation(array->groups[j], 1, 1));
		}
	}

	if (isl_union_map_is_empty(private))
		isl_union_map_free(private);
	else {
		isl_union_map *priv;

		private = isl_union_map_apply_domain(private,
					isl_union_map_copy(gen->shared_sched));
		priv = isl_union_map_from_map(isl_map_copy(gen->privatization));
		private = isl_union_map_apply_domain(private, priv);
		gen->private_access = private;
	}
}

/* Compute the size of the tile specified by "tile"
 * in number of elements and return the result.
 */
static __isl_give isl_val *tile_size(isl_ctx *ctx, struct gpu_array_tile *tile)
{
	int i;
	isl_val *size;

	size = isl_val_one(ctx);

	for (i = 0; i < tile->n; ++i)
		size = isl_val_mul(size, isl_val_copy(tile->bound[i].size));

	return size;
}

/* If max_shared_memory is not set to infinity (-1), then make
 * sure that the total amount of shared memory required by the
 * array reference groups mapped to shared memory is no larger
 * than this maximum.
 *
 * We apply a greedy approach and discard (keep in global memory)
 * those groups that would result in a total memory size that
 * is larger than the maximum.
 *
 * This function should be called after any function that may
 * affect the decision on whether to place a reference group
 * in private, shared or global memory.
 */
static void check_shared_memory_bound(struct gpu_gen *gen)
{
	int i, j;
	isl_val *left, *size;

	if (gen->options->max_shared_memory < 0)
		return;

	left = isl_val_int_from_si(gen->ctx, gen->options->max_shared_memory);

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group;

			group = array->groups[j];
			if (group->private_tile)
				continue;
			if (!group->shared_tile)
				continue;

			size = tile_size(gen->ctx, group->shared_tile);
			size = isl_val_mul_ui(size, array->size);

			if (isl_val_le(size, left)) {
				left = isl_val_sub(left, size);
				continue;
			}
			isl_val_free(size);

			group->shared_tile = free_tile(group->shared_tile);
		}
	}

	isl_val_free(left);
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
static void compute_group_tiling(struct gpu_array_ref_group *group)
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
	p = print_array_name(p, group);
	local_name = isl_printer_get_str(p);
	isl_printer_free(p);
	tiling = isl_multi_aff_set_tuple_name(tiling, isl_dim_out, local_name);
	free(local_name);

	tile->tiling = tiling;
}

/* Compute a tiling for all the array reference groups.
 */
static void compute_group_tilings(struct gpu_gen *gen)
{
	int i, j;

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j)
			compute_group_tiling(array->groups[j]);
	}
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
		assert(group);
		group->array = array;
		group->access = map;
		group->write = access->write;
		group->exact_write = access->exact_write;
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
static void free_array_ref_group(struct gpu_array_ref_group *group)
{
	if (!group)
		return;
	free_tile(group->shared_tile);
	free_tile(group->private_tile);
	isl_map_free(group->access);
	if (group->n_ref > 1)
		free(group->refs);
	free(group);
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
	free_array_ref_group(group1);
	free_array_ref_group(group2);
	return group;
}

/* Compute the private and/or shared memory tiles for the array
 * reference group "group" of array "array".
 * Return 0 on success and -1 on error.
 *
 * If the array is a read-only scalar or if the user requested
 * not to use shared or private memory, then we do not need to do anything.
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
	int no_reuse;
	isl_map *acc;
	int force_private = group->array->force_private;
	int use_shared = gen->options->use_shared_memory;
	int use_private = force_private || gen->options->use_private_memory;

	if (!use_shared && !use_private)
		return 0;
	if (gpu_array_is_read_only_scalar(group->array))
		return 0;
	if (!force_private && !group->exact_write)
		return 0;

	access = group_access_relation(group, 1, 1);
	no_reuse = isl_union_map_is_injective(access);

	if (use_shared && (!no_reuse || !access_is_coalesced(gen, access))) {
		group->shared_tile = create_tile(ctx, group->array->n_index);
		if (!can_tile(group->access, group->shared_tile))
			group->shared_tile = free_tile(group->shared_tile);
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

	group->private_tile = create_tile(gen->ctx, n_index);
	acc = isl_map_apply_domain(acc, isl_map_copy(gen->privatization));
	if (!can_tile(acc, group->private_tile))
		group->private_tile = free_tile(group->private_tile);

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
			if (compute_bounds &&
			    compute_group_bounds(gen, groups[i]) < 0)
				return -1;
			if (j != n - 1)
				groups[j] = groups[n - 1];
			groups[n - 1] = NULL;
			n--;
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
static int smaller_tile(isl_ctx *ctx, struct gpu_array_tile *tile,
	struct gpu_array_tile *tile1, struct gpu_array_tile *tile2)
{
	int smaller;
	isl_val *size, *size1, *size2;

	size = tile_size(ctx, tile);
	size1 = tile_size(ctx, tile1);
	size2 = tile_size(ctx, tile2);

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
				free_array_ref_group(group);
				return -1;
			}
			if (!group->shared_tile ||
			    !smaller_tile(ctx, group->shared_tile,
					groups[i]->shared_tile,
					groups[j]->shared_tile)) {
				free_array_ref_group(group);
				continue;
			}

			if (group->last_shared < groups[i]->last_shared ||
			    group->last_shared < groups[j]->last_shared)
				recompute_overlap = 1;
			free_array_ref_group(groups[i]);
			free_array_ref_group(groups[j]);
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
		free_array_ref_group(groups[i]);
	return -1;
}

/* Take tiled_sched, project it onto the shared tile loops and
 * the loops that will be wrapped over the threads and
 * store the result in gen->shared_sched.
 * Also compute a projection that projects out the loops that will be
 * wrapped over the threads and store this projection in gen->shared_proj.
 */
static void compute_shared_sched(struct gpu_gen *gen)
{
	isl_space *dim;
	isl_map *proj;
	isl_set *par;
	isl_union_map *sched;

	sched = isl_union_map_copy(gen->tiled_sched);

	dim = isl_union_map_get_space(sched);
	proj = projection(dim, gen->tiled_len, gen->shared_len + gen->n_block);
	sched = isl_union_map_apply_range(sched, isl_union_map_from_map(proj));

	dim = isl_union_map_get_space(sched);
	proj = projection(dim, gen->shared_len + gen->n_block, gen->shared_len);

	gen->shared_sched = sched;
	gen->shared_proj = isl_union_map_from_map(proj);
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
static int group_references(struct gpu_gen *gen)
{
	int i;
	int r = 0;
	isl_union_map *sched;

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

/* Free all array information that is local to the current kernel.
 */
static void free_local_array_info(struct gpu_gen *gen)
{
	int i, j;

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j)
			free_array_ref_group(array->groups[j]);
		free(array->groups);
	}
}

/* Compute the size of a bounding box around the origin and "set",
 * where "set" is assumed to contain only non-negative elements.
 * In particular, compute the maximal value of "set" in each direction
 * and add one.
 */
static __isl_give isl_multi_pw_aff *extract_size(__isl_take isl_set *set,
	__isl_keep isl_set *context)
{
	int i, n;
	isl_multi_pw_aff *mpa;

	n = isl_set_dim(set, isl_dim_set);
	mpa = isl_multi_pw_aff_zero(isl_set_get_space(set));
	for (i = 0; i < n; ++i) {
		isl_space *space;
		isl_aff *one;
		isl_pw_aff *bound;

		bound = isl_set_dim_max(isl_set_copy(set), i);
		bound = isl_pw_aff_coalesce(bound);
		bound = isl_pw_aff_gist(bound, isl_set_copy(context));

		space = isl_pw_aff_get_domain_space(bound);
		one = isl_aff_zero_on_domain(isl_local_space_from_space(space));
		one = isl_aff_add_constant_si(one, 1);
		bound = isl_pw_aff_add(bound, isl_pw_aff_from_aff(one));
		mpa = isl_multi_pw_aff_set_pw_aff(mpa, i, bound);
	}
	isl_set_free(set);

	return mpa;
}

/* Compute the effective grid size as a list of the sizes in each dimension.
 *
 * The grid size specified by the user or set by default
 * in read_grid_sizes() and applied in tile_schedule(),
 * may be too large for the given code in the sense that
 * it may contain blocks that don't need to execute anything.
 * We therefore don't return this grid size, but instead the
 * smallest grid size that ensures that all blocks that actually
 * execute code are included in the grid.
 *
 * We first extract a description of the grid, i.e., the possible values
 * of the block ids, from gen->tiled_sched.
 * The block ids are parameters in gen->tiled_sched.
 * We simply need to change them into set dimensions.
 *
 * Then, for each block dimension, we compute the maximal value of the block id
 * and add one.
 */
static __isl_give isl_multi_pw_aff *extract_grid_size(struct gpu_gen *gen,
	struct ppcg_kernel *kernel)
{
	int i;
	isl_set *grid;

	grid = isl_union_map_params(isl_union_map_copy(gen->tiled_sched));
	grid = isl_set_from_params(grid);
	grid = isl_set_add_dims(grid, isl_dim_set, gen->n_grid);
	for (i = 0; i < gen->n_grid; ++i) {
		int pos;
		char name[20];

		snprintf(name, sizeof(name), "b%d", i);
		pos = isl_set_find_dim_by_name(grid, isl_dim_param, name);
		assert(pos >= 0);
		grid = isl_set_equate(grid, isl_dim_param, pos, isl_dim_set, i);
		grid = isl_set_project_out(grid, isl_dim_param, pos, 1);
	}

	return extract_size(grid, kernel->context);
}

/* Compute the size of a fixed bounding box around the origin and "set",
 * where "set" is assumed to contain only non-negative elements,
 * and store the results in "size".
 * In particular, compute the maximal value of "set" in each direction
 * and add one.
 */
static void extract_fixed_size(__isl_take isl_set *set, int *size)
{
	int i, n;
	isl_local_space *ls;
	isl_aff *obj;

	n = isl_set_dim(set, isl_dim_set);
	ls = isl_local_space_from_space(isl_set_get_space(set));
	obj = isl_aff_zero_on_domain(ls);
	for (i = 0; i < n; ++i) {
		isl_val *max;

		obj = isl_aff_set_coefficient_si(obj, isl_dim_in, i, 1);
		max = isl_set_max_val(set, obj);
		size[i] = isl_val_get_num_si(max) + 1;
		isl_val_free(max);
		obj = isl_aff_set_coefficient_si(obj, isl_dim_in, i, 0);
	}
	isl_aff_free(obj);
	isl_set_free(set);
}

/* Compute the effective block size as a list of the sizes in each dimension
 * and store the sizes in kernel->block_dim.
 *
 * The block size specified by the user or set by default
 * in read_block_sizes() and applied in thread_tile_schedule(),
 * may be too large for the given code in the sense that
 * it may contain threads that don't need to execute anything.
 * We therefore don't store this block size in kernel->block_dim,
 * but instead the smallest block size that ensures that all threads
 * that actually execute code are included in the block.
 *
 * The current implementation eliminates all parameters, ensuring
 * that the size is a fixed constant in each dimension.
 * In principle we could also compute parametric sizes.
 * We would have to make sure to project out all b%d and t%d parameters,
 * however.
 */
static void extract_block_size(struct gpu_gen *gen, struct ppcg_kernel *kernel)
{
	int i;
	int nparam;
	isl_set *block;
	isl_multi_pw_aff *mpa;

	block = isl_union_map_params(isl_union_map_copy(gen->local_sched));
	block = isl_set_from_params(block);
	block = isl_set_add_dims(block, isl_dim_set, gen->n_block);
	kernel->n_block = gen->n_block;
	for (i = 0; i < gen->n_block; ++i) {
		int pos;
		char name[20];

		snprintf(name, sizeof(name), "t%d", i);
		pos = isl_set_find_dim_by_name(block, isl_dim_param, name);
		assert(pos >= 0);
		block = isl_set_equate(block, isl_dim_param, pos,
					isl_dim_set, i);
	}
	nparam = isl_set_dim(block, isl_dim_param);
	block = isl_set_project_out(block, isl_dim_param, 0, nparam);

	extract_fixed_size(block, kernel->block_dim);
}

void ppcg_kernel_free(void *user)
{
	struct ppcg_kernel *kernel = user;
	int i;

	if (!kernel)
		return;

	isl_multi_pw_aff_free(kernel->grid_size);
	isl_set_free(kernel->context);
	isl_union_set_free(kernel->arrays);
	isl_space_free(kernel->space);
	isl_ast_node_free(kernel->tree);

	for (i = 0; i < kernel->n_array; ++i)
		isl_pw_aff_list_free(kernel->array[i].bound);
	free(kernel->array);

	for (i = 0; i < kernel->n_var; ++i) {
		free(kernel->var[i].name);
		isl_vec_free(kernel->var[i].size);
	}
	free(kernel->var);

	free(kernel);
}

static void create_kernel_var(isl_ctx *ctx, struct gpu_array_ref_group *group,
	struct ppcg_kernel_var *var)
{
	int j;
	struct gpu_array_tile *tile;
	isl_printer *p;
	char *name;

	var->array = group->array;

	tile = group->private_tile;
	var->type = ppcg_access_private;
	if (!tile) {
		tile = group->shared_tile;
		var->type = ppcg_access_shared;
	}

	p = isl_printer_to_str(ctx);
	p = print_array_name(p, group);
	var->name = isl_printer_get_str(p);
	isl_printer_free(p);

	var->size = isl_vec_alloc(ctx, group->array->n_index);

	for (j = 0; j < group->array->n_index; ++j)
		var->size = isl_vec_set_element_val(var->size, j,
					    isl_val_copy(tile->bound[j].size));
}

static void create_kernel_vars(struct gpu_gen *gen, struct ppcg_kernel *kernel)
{
	int i, j, n;

	n = 0;
	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group = array->groups[j];
			if (group->private_tile || group->shared_tile)
				++n;
		}
	}

	kernel->n_var = n;
	kernel->var = isl_calloc_array(gen->ctx, struct ppcg_kernel_var, n);
	assert(kernel->var);

	n = 0;
	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group = array->groups[j];
			if (!group->private_tile && !group->shared_tile)
				continue;
			create_kernel_var(gen->ctx, group, &kernel->var[n]);
			++n;
		}
	}
}

/* The sizes of the arrays on the host that have been computed by
 * extract_array_info may depend on the parameters.  Use the extra
 * constraints on the parameters that are valid at "host_domain"
 * to simplify these expressions and store the results in kernel->array.
 *
 * We only need these localized bounds for arrays that are accessed
 * by the current kernel.  If we have found at least one reference group
 * then the array is accessed by the kernel.  If the array has compound
 * elements then we skipped the construction of array reference groups.
 */
static void localize_bounds(struct gpu_gen *gen, struct ppcg_kernel *kernel,
	__isl_keep isl_set *host_domain)
{
	int i, j;
	isl_set *context;

	kernel->array = isl_calloc_array(gen->ctx,
			    struct gpu_local_array_info, gen->prog->n_array);
	assert(kernel->array);
	kernel->n_array = gen->prog->n_array;

	context = isl_set_copy(host_domain);
	context = isl_set_params(context);

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];
		isl_pw_aff_list *local;

		if (array->n_group == 0 && !array->has_compound_element)
			continue;

		local = isl_pw_aff_list_alloc(gen->ctx, array->n_index);

		for (j = 0; j < array->n_index; ++j) {
			isl_pw_aff *pwaff;

			pwaff = isl_pw_aff_copy(array->bound[j]);
			pwaff = isl_pw_aff_gist(pwaff, isl_set_copy(context));
			local = isl_pw_aff_list_add(local, pwaff);
		}

		kernel->array[i].bound = local;
	}
	isl_set_free(context);
}

/* Find the element in gen->stmt that has the given "id".
 * Return NULL if no such gpu_stmt can be found.
 */
static struct gpu_stmt *find_stmt(struct gpu_prog *prog, __isl_keep isl_id *id)
{
	int i;

	for (i = 0; i < prog->n_stmts; ++i) {
		if (id == prog->stmts[i].id)
			break;
	}

	return i < prog->n_stmts ? &prog->stmts[i] : NULL;
}

/* Set gen->tile_len and gen->n_parallel to those of the statement
 * affected by the first map (part of the schedule)
 * on which this function is called.
 * Because of the way the schedule is constructed, the other statements
 * in the list, if any, should have the same values for these properties.
 */
static int extract_tile_len(__isl_take isl_map *map, void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	isl_id *id;
	struct gpu_stmt *stmt;

	id = isl_map_get_tuple_id(map, isl_dim_in);
	stmt = find_stmt(gen->prog, id);
	isl_id_free(id);

	isl_map_free(map);

	if (!stmt)
		isl_die(gen->ctx, isl_error_unknown,
			"statement not found", return -1);

	gen->tile_len = stmt->tile_len;
	gen->n_parallel = stmt->n_parallel;

	return -1;
}

void ppcg_kernel_stmt_free(void *user)
{
	int i;
	struct ppcg_kernel_stmt *stmt = user;

	if (!stmt)
		return;

	switch (stmt->type) {
	case ppcg_kernel_copy:
		isl_ast_expr_free(stmt->u.c.index);
		isl_ast_expr_free(stmt->u.c.local_index);
		break;
	case ppcg_kernel_domain:
		isl_id_to_ast_expr_free(stmt->u.d.ref2expr);
		break;
	case ppcg_kernel_sync:
		break;
	}

	free(stmt);
}

/* Set the options of "context" to
 *
 *	{ space -> [x] : x >= first }
 */
static __isl_give isl_ast_build *set_unroll(
	__isl_take isl_ast_build *build, __isl_take isl_space *space,
	int first)
{
	isl_ctx *ctx;
	isl_map *unroll;
	isl_union_map *opt;

	ctx = isl_ast_build_get_ctx(build);

	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, 1);
	space = isl_space_set_tuple_name(space, isl_dim_out, "unroll");
	unroll = isl_map_universe(space);
	unroll = isl_map_lower_bound_si(unroll, isl_dim_out, 0, first);
	opt = isl_union_map_from_map(unroll);

	build = isl_ast_build_set_options(build, opt);

	return build;
}

/* Return a list of isl_ids of the form "prefix%d".
 */
static __isl_give isl_id_list *generate_names(isl_ctx *ctx,
	int n, const char *prefix)
{
	int i;
	char name[10];
	isl_id_list *names;

	names = isl_id_list_alloc(ctx, n);
	for (i = 0; i < n; ++i) {
		isl_id *id;

		snprintf(name, sizeof(name), "%s%d", prefix, i);
		id = isl_id_alloc(ctx, name, NULL);
		names = isl_id_list_add(names, id);
	}

	return names;
}

/* Extend the schedule "schedule" with the part of "extension"
 * starting at "first" up to "len".
 */
static __isl_give isl_union_map *extend_schedule(
	__isl_take isl_union_map *schedule,
	__isl_take isl_union_map *extension, int first, int len)
{
	isl_space *space;
	isl_map *proj;
	isl_union_map *umap;
	isl_set *set;

	space = isl_union_map_get_space(schedule);
	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, len);
	proj = isl_set_identity(isl_set_universe(space));
	proj = isl_map_project_out(proj, isl_dim_out, 0, first);
	extension = isl_union_map_apply_range(extension,
						isl_union_map_from_map(proj));

	schedule = isl_union_map_range_product(schedule, extension);

	return schedule;
}

/* Return the gpu_stmt_access in the list "accesses" that corresponds
 * to "ref_id".
 */
static struct gpu_stmt_access *find_access(struct gpu_stmt_access *accesses,
	__isl_keep isl_id *ref_id)
{
	struct gpu_stmt_access *access;

	for (access = accesses; access; access = access->next)
		if (access->ref_id == ref_id)
			return access;

	return NULL;
}

/* Return the index of the array called "name" in the list of arrays.
 */
static int find_array_index(struct gpu_gen *gen, const char *name)
{
	int i;

	for (i = 0; i < gen->prog->n_array; ++i)
		if (!strcmp(name, gen->prog->array[i].name))
			return i;

	return -1;
}

/* Internal data structure for the index and AST expression transformation
 * callbacks for pet_stmt_build_ast_exprs.
 *
 * "accesses" is the list of gpu_stmt_access in the statement.
 * "iterator_map" expresses the statement iterators in terms of
 * the AST loop iterators.
 * "sched2shared" expresses the first shared_len dimensions of
 * the computed schedule in terms of the AST loop iterators.
 *
 * The following fields are set in transform_index and used in transform_expr.
 * "array" is the array that is being accessed.
 * "global" is set if the global array is accessed (rather than
 * shared/private memory).
 * "local_array" refers to information on the array specialized
 * to the current kernel.
 */
struct ppcg_transform_data {
	struct gpu_gen *gen;
	struct gpu_stmt_access *accesses;
	isl_pw_multi_aff *iterator_map;
	isl_pw_multi_aff *sched2shared;

	struct gpu_array_info *array;
	int global;
	struct gpu_local_array_info *local_array;
};

/* Return the name of the outer array (of structs) accessed by "access".
 */
static const char *get_outer_array_name(__isl_keep isl_map *access)
{
	isl_space *space;
	const char *name;

	space = isl_space_range(isl_map_get_space(access));
	while (space && isl_space_is_wrapping(space))
		space = isl_space_domain(isl_space_unwrap(space));
	name = isl_space_get_tuple_name(space, isl_dim_set);
	isl_space_free(space);

	return name;
}

/* Index transformation callback for pet_stmt_build_ast_exprs.
 *
 * "index" expresses the array indices in terms of statement iterators
 *
 * We first reformulate "index" in terms of the AST loop iterators.
 * Then we check if we are accessing the global array or
 * a shared/private copy.  In the former case, we simply return
 * the updated index.  If "index" is an affine expression rather
 * than an array access, then we also return the updated index here.
 *
 * If no reference groups have been computed for the array,
 * then we can only be accessing the global array.
 *
 * Otherwise, we apply the tiling to the index.
 * This tiling is of the form
 *
 *	[D -> A] -> T
 *
 * The index is of the form
 *
 *	L -> A
 *
 * We update the tiling to refer to the AST loop iteratos
 *
 *	[L -> A] -> T
 *
 * and modify index to keep track of those iterators
 *
 *	L -> [L -> A]
 *
 * Combining these two yields a tiled index expression in terms
 * of the AST loop iterators
 *
 *	L -> T
 */
static __isl_give isl_multi_pw_aff *transform_index(
	__isl_take isl_multi_pw_aff *index, __isl_keep isl_id *ref_id,
	void *user)
{
	struct ppcg_transform_data *data = user;
	struct gpu_stmt_access *access;
	struct gpu_array_ref_group *group;
	struct gpu_array_tile *tile;
	isl_pw_multi_aff *iterator_map;
	int i;
	const char *name;
	isl_space *space;
	isl_multi_pw_aff *tiling;
	isl_pw_multi_aff *pma;
	isl_multi_pw_aff *mpa;

	data->array = NULL;

	iterator_map = isl_pw_multi_aff_copy(data->iterator_map);
	index = isl_multi_pw_aff_pullback_pw_multi_aff(index, iterator_map);

	access = find_access(data->accesses, ref_id);
	if (!access)
		return index;
	if (!isl_map_has_tuple_name(access->access, isl_dim_out))
		return index;

	name = get_outer_array_name(access->access);
	i = find_array_index(data->gen, name);
	if (i < 0)
		isl_die(isl_multi_pw_aff_get_ctx(index), isl_error_internal,
			"cannot find array",
			return isl_multi_pw_aff_free(index));
	data->array = &data->gen->prog->array[i];
	data->local_array = &data->gen->kernel->array[i];

	if (access->group < 0) {
		data->global = 1;
		return index;
	}

	group = data->array->groups[access->group];
	tile = group->private_tile;
	if (!tile)
		tile = group->shared_tile;
	data->global = !tile;
	if (!tile)
		return index;

	space = isl_space_range(isl_multi_pw_aff_get_space(index));
	space = isl_space_map_from_set(space);
	pma = isl_pw_multi_aff_identity(space);
	pma = isl_pw_multi_aff_product(
			isl_pw_multi_aff_copy(data->sched2shared), pma);
	tiling = isl_multi_pw_aff_from_multi_aff(
				    isl_multi_aff_copy(tile->tiling));
	tiling = isl_multi_pw_aff_pullback_pw_multi_aff(tiling, pma);

	space = isl_space_domain(isl_multi_pw_aff_get_space(index));
	space = isl_space_map_from_set(space);
	mpa = isl_multi_pw_aff_identity(space);
	index = isl_multi_pw_aff_range_product(mpa, index);
	index = isl_multi_pw_aff_pullback_multi_pw_aff(tiling, index);

	return index;
}

/* Dereference "expr" by adding an index [0].
 * The original "expr" is assumed not to have any indices.
 *
 * If "expr" is a member access, then the dereferencing needs
 * to be applied to the structure argument of this member access.
 */
static __isl_give isl_ast_expr *dereference(__isl_take isl_ast_expr *expr)
{
	isl_ctx *ctx;
	isl_ast_expr *res;
	isl_ast_expr_list *list;

	if (isl_ast_expr_get_op_type(expr) == isl_ast_op_member) {
		isl_ast_expr *arg;

		arg = isl_ast_expr_get_op_arg(expr, 0);
		arg = dereference(arg);
		expr = isl_ast_expr_set_op_arg(expr, 0, arg);

		return expr;
	}

	ctx = isl_ast_expr_get_ctx(expr);
	res = isl_ast_expr_from_val(isl_val_zero(ctx));
	list = isl_ast_expr_list_from_ast_expr(res);
	res = isl_ast_expr_get_op_arg(expr, 0);
	res = isl_ast_expr_access(res, list);
	isl_ast_expr_free(expr);

	return res;
}

/* Linearize the index expression "expr" based on the array bounds
 * of "array".
 *
 * That is, transform expression
 *
 *	A[i_0][i_1]...[i_n]
 *
 * to
 *
 *	A[(..((i_0 * b_1 + i_1) ... ) * b_n + i_n]
 *
 * where b_0, b_1, ..., b_n are the bounds on the array.
 *
 * If the base of "expr" is a member access, then the linearization needs
 * to be applied to the structure argument of this member access.
 */
__isl_give isl_ast_expr *gpu_local_array_info_linearize_index(
	struct gpu_local_array_info *array, __isl_take isl_ast_expr *expr)
{
	int i, n;
	isl_ctx *ctx;
	isl_set *context;
	isl_ast_expr *arg0;
	isl_ast_expr *res;
	isl_ast_expr_list *list;
	isl_ast_build *build;

	arg0 = isl_ast_expr_get_op_arg(expr, 0);
	if (isl_ast_expr_get_type(arg0) == isl_ast_expr_op &&
	    isl_ast_expr_get_op_type(arg0) == isl_ast_op_member) {
		isl_ast_expr *arg;

		arg = isl_ast_expr_get_op_arg(arg0, 0);
		arg = gpu_local_array_info_linearize_index(array, arg);
		arg0 = isl_ast_expr_set_op_arg(arg0, 0, arg);
		expr = isl_ast_expr_set_op_arg(expr, 0, arg0);

		return expr;
	}
	isl_ast_expr_free(arg0);

	ctx = isl_ast_expr_get_ctx(expr);
	context = isl_set_universe(isl_space_params_alloc(ctx, 0));
	build = isl_ast_build_from_context(context);

	n = isl_ast_expr_get_op_n_arg(expr);
	res = isl_ast_expr_get_op_arg(expr, 1);
	for (i = 2; i < n; ++i) {
		isl_pw_aff *bound_i;
		isl_ast_expr *expr_i;

		bound_i = isl_pw_aff_list_get_pw_aff(array->bound, i - 1);
		expr_i = isl_ast_build_expr_from_pw_aff(build, bound_i);
		res = isl_ast_expr_mul(res, expr_i);
		expr_i = isl_ast_expr_get_op_arg(expr, i);
		res = isl_ast_expr_add(res, expr_i);
	}

	isl_ast_build_free(build);

	list = isl_ast_expr_list_from_ast_expr(res);
	res = isl_ast_expr_get_op_arg(expr, 0);
	res = isl_ast_expr_access(res, list);

	isl_ast_expr_free(expr);

	return res;
}

/* AST expression transformation callback for pet_stmt_build_ast_exprs.
 *
 * If the AST expression refers to a global scalar that is not
 * a read-only scalar, then its address was passed to the kernel and
 * we need to dereference it.
 *
 * If the AST expression refers to an access to a global array,
 * then we linearize the access exploiting the bounds in data->local_array.
 */
static __isl_give isl_ast_expr *transform_expr(__isl_take isl_ast_expr *expr,
	__isl_keep isl_id *id, void *user)
{
	struct ppcg_transform_data *data = user;

	if (!data->array)
		return expr;
	if (gpu_array_is_read_only_scalar(data->array))
		return expr;
	if (!data->global)
		return expr;
	if (data->array->n_index == 0)
		return dereference(expr);
	if (!data->array->linearize)
		return expr;

	return gpu_local_array_info_linearize_index(data->local_array, expr);
}

/* This function is called for each instance of a user statement
 * in the kernel.
 *
 * We attach a struct ppcg_kernel_stmt to the "node", containing
 * a computed AST expression for each access.
 * These AST expressions are computed from iterator_map,
 * which expresses the domain
 * elements in terms of the generated loops, and sched2shared,
 * which expresses the first shared_len dimensions of the schedule
 * computed by PPCG in terms of the generated loops.
 */
static __isl_give isl_ast_node *at_each_domain(__isl_take isl_ast_node *node,
	__isl_keep isl_ast_build *build, void *user)
{
	struct ppcg_transform_data data;
	struct gpu_gen *gen = (struct gpu_gen *) user;
	struct ppcg_kernel_stmt *stmt;
	isl_id *id;
	isl_pw_multi_aff *sched2shared;
	isl_map *map;
	isl_pw_multi_aff *iterator_map;
	isl_ast_expr *expr, *arg;
	isl_union_map *schedule;
	int i, n;

	stmt = isl_calloc_type(gen->ctx, struct ppcg_kernel_stmt);
	if (!stmt)
		return isl_ast_node_free(node);

	expr = isl_ast_node_user_get_expr(node);
	arg = isl_ast_expr_get_op_arg(expr, 0);
	id = isl_ast_expr_get_id(arg);

	schedule = isl_ast_build_get_schedule(build);
	map = isl_map_reverse(isl_map_from_union_map(schedule));
	iterator_map = isl_pw_multi_aff_from_map(map);
	sched2shared = compute_sched_to_shared(gen,
					isl_pw_multi_aff_copy(iterator_map));

	stmt->type = ppcg_kernel_domain;
	stmt->u.d.stmt = find_stmt(gen->prog, id);
	if (!stmt->u.d.stmt)
		goto error;

	data.gen = gen;
	data.accesses = stmt->u.d.stmt->accesses;
	data.iterator_map = iterator_map;
	data.sched2shared = sched2shared;
	stmt->u.d.ref2expr = pet_stmt_build_ast_exprs(stmt->u.d.stmt->stmt,
					    build, &transform_index, &data,
					    &transform_expr, &data);

	isl_id_free(id);
	isl_pw_multi_aff_free(iterator_map);
	isl_pw_multi_aff_free(sched2shared);
	isl_ast_expr_free(arg);
	isl_ast_expr_free(expr);

	id = isl_id_alloc(gen->ctx, NULL, stmt);
	id = isl_id_set_free_user(id, &ppcg_kernel_stmt_free);
	return isl_ast_node_set_annotation(node, id);
error:
	isl_id_free(id);
	isl_pw_multi_aff_free(iterator_map);
	ppcg_kernel_stmt_free(stmt);
	isl_pw_multi_aff_free(sched2shared);
	return isl_ast_node_free(node);
}

/* This function is called when code has been generated for the shared
 * tile loops.  The "schedule" refers only to the original statements.
 *
 * We extend the schedule with that part of gen->local_sched that hasn't
 * been taken into account yet.  This introduces parameters referring
 * to thread ids in the schedule, so we add them (with the appropriate
 * bounds to the context as well).
 * Finally, we set the appropriate unrolling options
 * if gen->first_unroll is set.
 */
static __isl_give isl_ast_node *create_domain_leaf(
	__isl_take isl_union_map *schedule, __isl_take isl_ast_build *build,
	void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	isl_space *space;
	isl_union_map *sched;
	isl_ast_node *tree;
	isl_set *set;
	isl_id_list *iterators;
	int n;

	schedule = extend_schedule(schedule,
			isl_union_map_copy(gen->local_sched),
			gen->shared_len, gen->thread_tiled_len);

	space = isl_ast_build_get_schedule_space(build);
	set = isl_set_universe(space);
	set = add_bounded_parameters(set, gen->kernel->n_block,
					gen->kernel->block_dim, "t");
	build = isl_ast_build_restrict(build, set);

	n = gen->thread_tiled_len - gen->shared_len;

	if (gen->first_unroll >= 0) {
		space = isl_space_set_alloc(gen->ctx, 0, n);
		build = set_unroll(build, space, gen->first_unroll);
	}
	iterators = generate_names(gen->ctx, n, "c");
	build = isl_ast_build_set_iterators(build, iterators);
	build = isl_ast_build_set_at_each_domain(build, &at_each_domain, gen);
	tree = isl_ast_build_ast_from_schedule(build, schedule);
	isl_ast_build_free(build);

	return tree;
}

/* This function is called for each statement node in the AST of the code
 * for copying to or from shared/private memory.
 * Attach a pointer to a ppcg_kernel_stmt representing the copy
 * statement to the node.
 * The statement name is "read" or "write", depending on whether we are
 * reading from global memory or writing to global memory.
 * The name of the T space is {shared,private}_<array>.
 *
 * The schedule is of the form
 *
 *	type[A -> T] -> L
 *
 * where A refers to a piece of an array and T to the corresponding
 * shifted tile.  We split this schedule into mappings L -> A and L -> T
 * and store the corresponding expressions in stmt->index and stmt->local_index,
 * where stmt points to the ppcg_kernel_stmt that is attached to the node.
 */
static __isl_give isl_ast_node *attach_copy_stmt(__isl_take isl_ast_node *node,
	__isl_keep isl_ast_build *build, void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	struct ppcg_kernel_stmt *stmt;
	isl_id *id;
	isl_ast_expr *expr;
	isl_space *space;
	isl_map *access, *local_access, *map;
	isl_pw_multi_aff *pma;
	const char *type;
	int array_index;

	stmt = isl_calloc_type(gen->ctx, struct ppcg_kernel_stmt);
	if (!stmt)
		return isl_ast_node_free(node);

	access = isl_map_from_union_map(isl_ast_build_get_schedule(build));
	type = isl_map_get_tuple_name(access, isl_dim_in);
	stmt->u.c.read = !strcmp(type, "read");
	access = isl_map_reverse(access);
	space = isl_space_unwrap(isl_space_range(isl_map_get_space(access)));
	local_access = isl_map_copy(access);

	map = isl_map_domain_map(isl_map_universe(isl_space_copy(space)));
	id = isl_map_get_tuple_id(access, isl_dim_out);
	map = isl_map_set_tuple_id(map, isl_dim_in, id);
	access = isl_map_apply_range(access, map);
	pma = isl_pw_multi_aff_from_map(access);
	expr = isl_ast_build_access_from_pw_multi_aff(build, pma);
	stmt->u.c.index = expr;

	map = isl_map_range_map(isl_map_universe(space));
	id = isl_map_get_tuple_id(local_access, isl_dim_out);
	map = isl_map_set_tuple_id(map, isl_dim_in, id);
	local_access = isl_map_apply_range(local_access, map);
	pma = isl_pw_multi_aff_from_map(local_access);
	expr = isl_ast_build_access_from_pw_multi_aff(build, pma);
	stmt->u.c.local_index = expr;

	stmt->u.c.array = gen->copy_group->array;
	array_index = stmt->u.c.array - gen->prog->array;
	stmt->u.c.local_array = &gen->kernel->array[array_index];
	stmt->type = ppcg_kernel_copy;

	id = isl_id_alloc(gen->ctx, NULL, stmt);
	id = isl_id_set_free_user(id, &ppcg_kernel_stmt_free);
	return isl_ast_node_set_annotation(node, id);
}

/* Given a schedule of the form
 *
 *	[S -> A] -> L
 *
 * (with S the first shared_len dimensions of the computed schedule,
 * A the array and L the schedule correponding to the generated loops),
 * indicating where to copy the array elements that need to be copied,
 * construct code for performing the copying.
 *
 * "group" is the array reference group that is being copied
 * "type" is either "read" or "write"
 * private is set if copying needs to be performed to/from registers
 *
 * We first construct a mapping to a shifted tile of the array,
 *
 *	[S -> A] -> T(S,A)					(1)
 *
 * If private is set, then we also use this mapping as a schedule
 * (which is already thread-specific and will be completely unrolled).
 * Otherwise, we wrap/tile the range over the threads.
 * The result is
 *
 *	[S -> A] -> T'(S,A)
 *
 * Combined with the given schedule, we have
 *
 *	[S -> A] -> [L -> T'(S,A)]				(2)
 *
 * From the shifted tile mapping, we construct a mapping
 *
 *	[S -> A] -> [A -> T(S,A)]
 *
 * and apply it to the schedule (2), obtaining
 *
 *	[A -> T(S(L),A)] -> [L -> T'(S(L),A)]
 *
 * Note that we can project out S because it is uniquely defined by L.
 */
static __isl_give isl_ast_node *copy_access(struct gpu_gen *gen,
	__isl_take isl_map *sched,
	const char *type, struct gpu_array_ref_group *group,
	__isl_take isl_ast_build *build, int private)
{
	isl_space *space;
	isl_ast_node *tree;
	isl_map *schedule, *shift, *map;
	isl_set *set;
	isl_id_list *iterators;
	int n;

	shift = shift_access(group);

	schedule = isl_map_copy(shift);
	schedule = isl_map_reset_tuple_id(schedule, isl_dim_out);
	if (!private)
		schedule = tile_access_schedule(gen, schedule);

	n = isl_map_dim(schedule, isl_dim_out);
	set = isl_set_universe(isl_ast_build_get_schedule_space(build));
	set = add_bounded_parameters(set, gen->kernel->n_block,
					gen->kernel->block_dim, "t");

	schedule = isl_map_range_product(sched, schedule);

	space = isl_space_domain(isl_map_get_space(shift));
	map = isl_map_range_map(isl_map_universe(isl_space_unwrap(space)));
	map = isl_map_range_product(map, shift);

	schedule = isl_map_apply_domain(schedule, map);

	schedule = isl_map_set_tuple_name(schedule, isl_dim_in, type);

	build = isl_ast_build_restrict(build, set);

	gen->copy_group = group;

	if (private) {
		space = isl_space_range(isl_map_get_space(schedule));
		space = isl_space_range(isl_space_unwrap(space));
		build = set_unroll(build, space, 0);
	}
	iterators = generate_names(gen->ctx, n, "c");
	build = isl_ast_build_set_iterators(build, iterators);
	build = isl_ast_build_set_at_each_domain(build, &attach_copy_stmt, gen);
	tree = isl_ast_build_ast_from_schedule(build,
					    isl_union_map_from_map(schedule));
	isl_ast_build_free(build);

	return tree;
}

/* Return code for reading into or writing from shared memory
 * the given array reference group.
 *
 * If we are performing a read from global memory to shared memory and
 * if the array involved is not a scalar, then we copy
 * the entire tile to shared memory.  This may result in some extra
 * elements getting copied, but it should lead to simpler code
 * (which means that fewer registers may be needed) and less divergence.
 *
 * Otherwise, we only copy the elements that will be read or have been written
 * in the kernel.
 *
 *
 * The input "sched" is of the form.
 *
 *	type[S -> A] -> L
 *
 * with S the first shared_len dimensions of the computed schedule,
 * A the array and L the schedule correponding to the generated loops.
 *
 * We first drop "type",
 *
 *	[S -> A] -> L
 *
 * If the above conditions are satisfied, we project out A,
 * resulting in
 *
 *	S -> L
 *
 * and then introduce the group tile [S -> T], resulting in
 *
 *	[S -> T] -> L
 */
static __isl_give isl_ast_node *copy_group_shared_accesses(
	struct gpu_gen *gen, struct gpu_array_ref_group *group,
	__isl_take isl_map *sched, __isl_take isl_ast_build *build)
{
	const char *type;
	int read;
	isl_union_map *access;

	type = isl_map_get_tuple_name(sched, isl_dim_in);
	read = !strcmp(type, "read");

	sched = isl_map_reset_tuple_id(sched, isl_dim_in);

	if (read && !gpu_array_is_scalar(group->array)) {
		isl_space *space;
		isl_map *map;

		space = isl_space_domain(isl_map_get_space(sched));
		space = isl_space_unwrap(space);
		map = isl_map_domain_map(isl_map_universe(space));
		sched = isl_map_apply_domain(sched, map);

		map = group_tile(group);
		map = isl_map_reverse(isl_map_domain_map(map));
		sched = isl_map_apply_domain(sched, map);
	}

	return copy_access(gen, sched, type, group, build, 0);
}

/* Return code for reading into or writing from private memory
 * the given array reference group.
 *
 * Let S be the first shared_len dimensions of the computed schedule,
 * D the iteration domains, A the array and L the schedule correponding
 * to the generated loops.
 * "sched" is of the form
 *
 *	type[S -> A] -> L
 *
 * where type is either "read" or "write".
 * We apply the privatization D -> S(t), with t the thread ids,
 * to the access relation D -> A to obtain the privatized access relation
 *
 *	S(t) -> A
 *
 * We drop the type from "sched" and intersect with the privatized access
 * relation to obtain
 *
 *	[S(t) -> A] -> L
 */
static __isl_give isl_ast_node *copy_group_private_accesses(
	struct gpu_gen *gen, struct gpu_array_ref_group *group,
	__isl_take isl_map *sched, __isl_take isl_ast_build *build)
{
	const char *type;
	int read;
	isl_union_map *priv;
	isl_union_map *access;
	isl_map *access_map;

	type = isl_map_get_tuple_name(sched, isl_dim_in);
	read = !strcmp(type, "read");

	priv = isl_union_map_from_map(isl_map_copy(gen->privatization));
	priv = isl_union_map_apply_range(isl_union_map_copy(gen->shared_sched),
					priv);

	access = group_access_relation(group, read, !read);
	access = isl_union_map_apply_domain(access, priv);
	access_map = isl_map_from_union_map(access);

	sched = isl_map_reset_tuple_id(sched, isl_dim_in);
	sched = isl_map_intersect_domain(sched, isl_map_wrap(access_map));

	return copy_access(gen, sched, type, group, build, 1);
}

/* Return code for reading into or writing from shared or private memory.
 *
 * "schedule" is of the form
 *
 *	type[S -> A] -> L
 *
 * with S be the first shared_len dimensions of the computed schedule,
 * A the array and L the schedule correponding to the generated loops.
 * The array reference group is attached to "type".
 */
static __isl_give isl_ast_node *create_access_leaf(
	struct gpu_gen *gen, __isl_take isl_map *schedule,
	__isl_take isl_ast_build *build)
{
	struct gpu_array_ref_group *group;
	isl_id *id;

	id = isl_map_get_tuple_id(schedule, isl_dim_in);
	group = isl_id_get_user(id);
	isl_id_free(id);

	if (group->private_tile)
		return copy_group_private_accesses(gen, group, schedule,
							build);
	else
		return copy_group_shared_accesses(gen, group, schedule,
							build);
}

/* Create a domain node representing a synchronization.
 */
static __isl_give isl_ast_node *create_sync_leaf(
	struct gpu_gen *gen, __isl_take isl_map *schedule,
	__isl_take isl_ast_build *build)
{
	struct ppcg_kernel_stmt *stmt;
	isl_id *id;
	isl_space *space;
	isl_ast_node *node;
	isl_ast_expr *expr;

	isl_map_free(schedule);

	stmt = isl_calloc_type(gen->ctx, struct ppcg_kernel_stmt);
	if (!stmt)
		return NULL;

	stmt->type = ppcg_kernel_sync;

	space = isl_ast_build_get_schedule_space(build);
	space = isl_space_from_domain(space);
	space = isl_space_set_tuple_name(space, isl_dim_out, "sync");
	expr = isl_ast_build_call_from_pw_multi_aff(build,
		    isl_pw_multi_aff_from_multi_aff(isl_multi_aff_zero(space)));
	node = isl_ast_node_alloc_user(expr);
	isl_ast_build_free(build);

	id = isl_id_alloc(gen->ctx, NULL, stmt);
	id = isl_id_set_free_user(id, &ppcg_kernel_stmt_free);
	return isl_ast_node_set_annotation(node, id);
}

/* This function is called during the code generation at the point
 * where the schedule domain element is completely determined by
 * the generated code.  The input schedule contains the original
 * statements as well as synchronization and copy "statements".
 * The latter are scheduled at different points than any of the original
 * statements, so they will only arrive here in isolation.
 *
 * If the current schedule only refers to a single statement,
 * we check if it is a copy or synchronization statement and
 * call the appropriate functions.
 * Otherwise, we assume we are dealing with the original statements
 * and we call create_domain_leaf.
 */
static __isl_give isl_ast_node *create_kernel_leaf(
	__isl_take isl_ast_build *build, void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	isl_map *map;
	isl_union_map *schedule;
	const char *name;

	schedule = isl_ast_build_get_schedule(build);

	if (isl_union_map_n_map(schedule) != 1)
		return create_domain_leaf(schedule, build, user);

	map = isl_map_from_union_map(schedule);
	name = isl_map_get_tuple_name(map, isl_dim_in);
	if (!strcmp(name, "read") || !strcmp(name, "write"))
		return create_access_leaf(gen, map, build);
	if (!strcmp(name, "sync"))
		return create_sync_leaf(gen, map, build);

	return create_domain_leaf(isl_union_map_from_map(map), build, user);
}

/* Mark all odd schedule dimensions as "atomic" (when the even dimensions
 * have value 0) and all even schedule dimensions as "unroll".
 *
 * That is, the options look as follows
 *
 *	{ [0, b, 0, d, ..., 0] -> atomic[i] : exists a : i = 2 a + 1;
 *	  [a, b, c, d, ..., z] -> unroll[i] : exists a : i = 2 a }
 *
 * The even positions are used to be able to schedule copying blocks
 * and synchronization before or after each level of the shared memory
 * tile loops and we want to make sure that code for these is generated
 * separately (within each level).
 */
static __isl_give isl_ast_build *set_atomic_and_unroll(
	__isl_take isl_ast_build *build,
	__isl_take isl_space *space, int sched_len)
{
	isl_ctx *ctx;
	isl_map *map;
	isl_constraint *c;
	isl_union_map *opt;
	isl_local_space *ls;
	int i, n;

	ctx = isl_ast_build_get_ctx(build);

	space = isl_space_params(space);
	space = isl_space_add_dims(space, isl_dim_set, sched_len);
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, 2);
	map = isl_map_universe(isl_space_copy(space));
	for (i = 0; i < sched_len; i += 2)
		map = isl_map_fix_si(map, isl_dim_in, i, 0);
	ls = isl_local_space_from_space(isl_map_get_space(map));
	c = isl_equality_alloc(ls);
	c = isl_constraint_set_coefficient_si(c, isl_dim_out, 0, 1);
	c = isl_constraint_set_coefficient_si(c, isl_dim_out, 1, 2);
	c = isl_constraint_set_constant_si(c, 1);
	map = isl_map_add_constraint(map, c);
	map = isl_map_project_out(map, isl_dim_out, 1, 1);
	map = isl_map_set_tuple_name(map, isl_dim_out, "atomic");
	opt = isl_union_map_from_map(map);

	map = isl_map_universe(space);
	ls = isl_local_space_from_space(isl_map_get_space(map));
	c = isl_equality_alloc(ls);
	c = isl_constraint_set_coefficient_si(c, isl_dim_out, 0, 1);
	c = isl_constraint_set_coefficient_si(c, isl_dim_out, 1, 2);
	map = isl_map_add_constraint(map, c);
	map = isl_map_project_out(map, isl_dim_out, 1, 1);
	map = isl_map_set_tuple_name(map, isl_dim_out, "unroll");
	opt = isl_union_map_add_map(opt, map);

	build = isl_ast_build_set_options(build, opt);

	return build;
}

/* Return a map that maps a space of dimension gen->shared_len
 * to its last dimensions starting at gen->tile_first.
 * The range is of dimension
 *
 *	2 * (gen->shared_len - gen->tile_first) + 1
 *
 * The input dimensions are mapped to the odd dimensions in the output,
 * while the even dimensions (except 2*pos) are fixed to 0.
 * Output dimension 2*pos (if pos >= 0) is fixed to "val".
 * If pos >= 0, then only the pos first dimensions starting at gen->tile_first
 * are mapped to the output.  The remaining input dimensions are projected
 * out and the corresponding output dimensions are fixed to 0.
 */
static __isl_give isl_map *insert_even(struct gpu_gen *gen,
	__isl_take isl_space *space, int pos, int val)
{
	int i, n;
	isl_map *proj;

	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, gen->shared_len);
	space = isl_space_map_from_set(space);
	proj = isl_map_identity(space);
	proj = isl_map_project_out(proj, isl_dim_out, 0, gen->tile_first);
	n = gen->shared_len - gen->tile_first;
	for (i = 0; i <= n; ++i) {
		proj = isl_map_insert_dims(proj, isl_dim_out, 2 * i, 1);
		if (i == pos)
			proj = isl_map_fix_si(proj, isl_dim_out, 2 * i, val);
		else
			proj = isl_map_fix_si(proj, isl_dim_out, 2 * i, 0);
	}

	if (pos < 0)
		return proj;

	proj = isl_map_eliminate(proj, isl_dim_in, gen->tile_first + pos,
				gen->shared_len - (gen->tile_first + pos));
	for (i = pos; i < n; ++i)
		proj = isl_map_fix_si(proj, isl_dim_out, 2 * i + 1, 0);

	return proj;
}

/* Given the AST context schedule "schedule" and the mapping from
 * domains to the shared tile loops "shared_sched", add a schedule
 * for a synchronization operation at position "val" of loop level "pos".
 *
 * schedule is of the form
 *
 *	D -> L
 *
 * (with D the iteration domains and L the already generated loops),
 * while shared_sched is of the form
 *
 *	D -> S
 *
 * We combine them into
 *
 *	L -> S
 *
 * apply a mapping
 *
 *	[s_0,...] -> [0,s_{tile_first},0,..., val, 0, 0, ... 0]
 *
 * and use the result as a schedule for "sync".
 */
static __isl_give isl_union_map *add_sync_schedule(struct gpu_gen *gen,
	__isl_take isl_union_map *res, __isl_keep isl_union_map *schedule,
	__isl_keep isl_union_map *shared_sched, int pos, int val)
{
	isl_space *space;
	isl_map *proj, *map;

	shared_sched = isl_union_map_copy(shared_sched);
	schedule = isl_union_map_copy(schedule);

	space = isl_union_map_get_space(shared_sched);
	schedule = isl_union_map_apply_domain(shared_sched, schedule);
	map = isl_map_from_union_map(schedule);

	proj = insert_even(gen, space, pos, val);
	map = isl_map_apply_range(map, proj);
	map = isl_map_from_range(isl_map_wrap(map));
	map = isl_map_set_tuple_name(map, isl_dim_in, "sync");

	res = isl_union_map_add_map(res, map);

	return res;
}

/* Given a set of wrapped references "ref", return the corresponding
 * access relations based on the tagged access relations "tagged".
 *
 * The elements of "ref" are of the form
 *
 *	[D -> R]
 *
 * with D an iteration domains and R a reference.
 * The elements of "tagged" are of the form
 *
 *	[D -> R] -> A
 *
 * with A an array.
 *
 * Extend "tagged" to include the iteration domain in the range, i.e.,
 *
 *	[D -> R] -> [D -> A]
 *
 * apply the result to "ref" and then unwrap the resulting set
 * to obtain relations of the form
 *
 *	D -> A
 */
static __isl_give isl_union_map *wrapped_reference_to_access(
	__isl_take isl_union_set *ref, __isl_take isl_union_map *tagged)
{
	isl_union_map *tag2access;

	tag2access = isl_union_map_copy(tagged);
	tag2access = isl_union_map_universe(tag2access);
	tag2access = isl_union_set_unwrap(isl_union_map_domain(tag2access));
	tag2access = isl_union_map_domain_map(tag2access);
	tag2access = isl_union_map_range_product(tag2access, tagged);

	ref = isl_union_set_coalesce(ref);
	ref = isl_union_set_apply(ref, tag2access);

	return isl_union_set_unwrap(ref);
}

/* Given an access relation "access" from "group", remove those reads
 * if ("read" is 1) or writes (if "read" is 0) that are only needed to
 * communicate data within the same iteration of the last_shared dimension
 * of the group.
 *
 * If the access is a read then it is necessarily an element of
 *
 *	live_in union (range flow)
 *
 * where live_in and flow may be overapproximations.
 * If the access is a write then it is necessarily an element of
 *
 *	live_out union (domain flow)
 *
 * In both cases, the access relation is also a subset of
 * the group access relation.
 *
 * Essentially, we compute the intersection of "access" with either
 *
 *	live_in union (range non-local-flow)
 *
 * or
 *
 *	live_out union (domain non-local-flow)
 *
 * We first construct a relation "local"
 *
 *	[[D -> R] -> [D' -> R']]
 *
 * of pairs of domain iterations accessing the reference group
 * and references in the group that are scheduled to the same iteration
 * of the last_shared dimension.
 *
 * If this relation does not intersect the dataflow dependences,
 * then there is nothing we can possibly remove and we simply
 * return the input.
 *
 * Otherwise, we remove the "local" dataflow dependences from
 * the set of all dataflow dependences.
 * Note that if the potential dataflow dependences are an overapproximation
 * of the actual dataflow dependences, then the result remains an
 * overapproximation of the non-local dataflow dependences.
 * Copying to/from global memory is only needed for the references
 * in the domain/range of the result or for accesses that are live out/in
 * for the entire scop.
 *
 * We therefore map the domain/range of the "external" relation
 * to the corresponding access relation and take the union with
 * the live out/in relation.
 */
static __isl_give isl_union_map *remove_local_accesses(struct gpu_gen *gen,
	struct gpu_array_ref_group *group, __isl_take isl_union_map *access,
	int read)
{
	int empty;
	isl_union_map *tagger;
	isl_union_set *domain;
	isl_space *space;
	isl_union_map *sched, *local, *tagged, *external;
	isl_union_set *tag_set;
	isl_map *proj;

	if (isl_union_map_is_empty(access))
		return access;

	tagged = group_tagged_access_relation(group);

	sched = isl_union_map_copy(gen->sched);

	space = isl_union_map_get_space(sched);
	proj = projection(space, gen->untiled_len, group->last_shared + 1);
	sched = isl_union_map_apply_range(sched, isl_union_map_from_map(proj));

	tagger = isl_union_map_copy(gen->prog->scop->tagger);
	domain = isl_union_map_domain(isl_union_map_copy(tagged));
	tagger = isl_union_map_intersect_range(tagger, domain);
	sched = isl_union_map_apply_domain(sched, tagger);

	local = isl_union_map_apply_range(sched,
			    isl_union_map_reverse(isl_union_map_copy(sched)));
	local = isl_union_map_intersect(local,
			isl_union_map_copy(gen->prog->scop->tagged_dep_flow));

	empty = isl_union_map_is_empty(local);
	if (empty < 0 || empty) {
		isl_union_map_free(tagged);
		isl_union_map_free(local);
		if (empty < 0)
			return isl_union_map_free(access);
		return access;
	}

	external = isl_union_map_copy(gen->prog->scop->tagged_dep_flow);
	external = isl_union_map_intersect_params(external,
				isl_set_copy(gen->prog->scop->context));
	external = isl_union_map_subtract(external, local);

	if (read) {
		tag_set = isl_union_map_range(external);
		external = wrapped_reference_to_access(tag_set, tagged);
		external = isl_union_map_union(external,
				isl_union_map_copy(gen->prog->scop->live_in));
	} else {
		tag_set = isl_union_map_domain(external);
		external = wrapped_reference_to_access(tag_set, tagged);
		external = isl_union_map_union(external,
				isl_union_map_copy(gen->prog->scop->live_out));
	}

	access = isl_union_map_intersect(access, external);

	return access;
}

/* Given the AST context schedule "schedule" and the mapping from
 * domains to the shared tile loops "shared_sched", add a schedule
 * for copying an array reference group to/from shared/private memory.
 * "read" is set if data should be copied from global memory
 * to shared/private memory.
 * "k" represents the current group
 * "s" is the total number of groups
 *
 * We schedule an operation before or after the innermost loop
 * of "shared_sched" that affects the tile of the array reference group.
 *
 * schedule is of the form
 *
 *	D -> L
 *
 * (with D the iteration domains and L the already generated loops),
 * while shared_sched is of the form
 *
 *	D -> S
 *
 * We first compute the access relation for the reference group
 *
 *	D -> A
 *
 * and remove from this access relation those reads or writes
 * that only needed to communicate data within the same iteration
 * of the last_shared dimension of the group.
 * We then combine what is left with shared_sched into
 *
 *	D -> [S -> A]
 *
 * If this results in an empty relation, no copying needs to be performed
 * at this point.
 * Otherwise, we invert the relation and combine it with "schedule" into
 *
 *	[S -> A] -> L
 *
 * The actual additional piece of the schedule is obtained from combining
 *
 *	[S -> A] -> S
 *
 * with a mapping
 *
 *	[s_0,...] -> [0,s_{tile_first},0,..., val, 0, 0, ... 0]
 *
 * The position of "val" corresponds to the innermost loop that affects
 * the tile and the value indicates where the copying is scheduled
 * with respect to the actual kernel code (at value 0).
 * Reads are schedule before the code, writes to global memory from
 * private memory are scheduled at values 1 to s, writes to global
 * memory from shared memory are scheduled at values s + 2 to 2 * s + 1.
 *
 * If we are scheduling a read from global memory to shared memory,
 * we insert a synchronization before the kernel code (at the innermost
 * level).
 * If we are scheduling a write to global memory, then we add
 * a synchronization after all writes (at value 2 *s + 2).
 * However, there is no need for a synchronization after the outermost loop.
 * A write to global memory from private memory at the innermost level
 * does not require a synchronization, because it is covered by
 * the synchronization after the kernel inserted by body_schedule.
 */
static __isl_give isl_union_map *add_group_schedule(struct gpu_gen *gen,
	__isl_take isl_union_map *res, __isl_keep isl_union_map *schedule,
	__isl_keep isl_union_map *shared_sched,
	struct gpu_array_ref_group *group, int read, int k, int s)
{
	int n;
	int pos, val;
	isl_space *space;
	isl_union_map *access;
	isl_map *map, *proj, *access_map;
	isl_id *id;

	access = group_access_relation(group, read, !read);
	access = remove_local_accesses(gen, group, access, read);
	access = isl_union_map_range_product(isl_union_map_copy(shared_sched),
						access);

	if (isl_union_map_is_empty(access)) {
		isl_union_map_free(access);
		return res;
	}

	access = isl_union_map_reverse(access);
	access = isl_union_map_apply_range(access,
					    isl_union_map_copy(schedule));
	access_map = isl_map_from_union_map(access);

	space = isl_space_copy(group->array->space);
	space = isl_space_from_range(space);
	space = isl_space_add_dims(space, isl_dim_in, gen->shared_len);
	map = isl_map_domain_map(isl_map_universe(space));

	space = isl_union_map_get_space(schedule);
	pos = group->last_shared + 1 - gen->tile_first;
	assert(pos >= 0);
	if (read)
		val = -2 - k;
	else if (group->private_tile)
		val = 1 + k;
	else
		val = 1 + s + 1 + k;
	proj = insert_even(gen, space, pos, val);
	map = isl_map_apply_range(map, proj);

	access_map = isl_map_range_product(access_map, map);

	id = isl_id_alloc(gen->ctx, read ? "read" : "write", group);
	access_map = isl_map_set_tuple_id(access_map, isl_dim_in, id);

	res = isl_union_map_add_map(res, access_map);

	n = gen->shared_len - gen->tile_first;
	if (read) {
		if (!group->private_tile)
			res = add_sync_schedule(gen, res, schedule,
						shared_sched, n, -1);
	} else {
		if (pos == 0)
			return res;
		if (pos == n && group->private_tile)
			return res;
		res = add_sync_schedule(gen, res, schedule, shared_sched,
					pos, 2 * s + 2);
	}

	return res;
}

/* Return a schedule for the shared tile loops based on the current
 * AST context schedule.
 *
 * We create a "shared_sched" that maps the domains to the first
 * shared_len dimensions of the computed schedule, project out the
 * first tile_first dimensions (as these are already covered by
 * the host code) and insert "statement-level" dimensions at even
 * positions so that we can schedule copy blocks and synchronization
 * before/after each level.
 *
 * In particular, copy blocks are inserted inside the innermost
 * level that affect the tile.  For the copying to global memory,
 * those from private memory are scheduled before those from shared
 * memory such that synchronization can be inserted between the two
 * at the innermost level.
 * Synchronization is inserted at the innermost level before the
 * actual kernel code if there is any copying from global memory
 * to shared memory.  It is inserted unconditionally at the innermost
 * level after the actual kernel code and the copying to global memory
 * from private memory (if any).  Finally, it is inserted after
 * any copying to global memory, except at the outermost level
 * and at the innermost level if there is no copying from shared
 * memory.  The copying from private memory is covered by the unconditional
 * synchronization at the innermost level.
 */
static __isl_give isl_union_map *body_schedule(struct gpu_gen *gen,
	__isl_take isl_union_map *schedule)
{
	isl_space *space;
	isl_union_map *res;
	isl_union_map *shared_sched;
	isl_union_map *sched;
	isl_map *proj, *map;
	int i, j, k, s;

	shared_sched = isl_union_map_copy(gen->tiled_sched);
	proj = projection(isl_union_map_get_space(shared_sched),
				gen->tiled_len, gen->shared_len);
	shared_sched = isl_union_map_apply_range(shared_sched,
				isl_union_map_from_map(proj));
	space = isl_union_map_get_space(shared_sched);
	proj = insert_even(gen, space, -1, 0);
	sched = isl_union_map_apply_range(isl_union_map_copy(shared_sched),
				isl_union_map_from_map(proj));

	res = isl_union_map_range_product(isl_union_map_copy(schedule), sched);

	s = 0;
	for (i = 0; i < gen->prog->n_array; ++i)
		s += gen->prog->array[i].n_group;

	k = 0;
	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group;

			group = array->groups[j];
			if (!group->private_tile && !group->shared_tile)
				continue;
			res = add_group_schedule(gen, res, schedule,
						shared_sched, group, 0, k, s);
			res = add_group_schedule(gen, res, schedule,
						shared_sched, group, 1, k, s);
			++k;
		}
	}

	res = add_sync_schedule(gen, res, schedule, shared_sched,
			    gen->shared_len - gen->tile_first, 1 + s);

	isl_union_map_free(shared_sched);
	isl_union_map_free(schedule);

	return res;
}

/* Generate code for "kernel" in the given "context".
 *
 * We first generate code for the shared tile loops (T1T, T1P and T2)
 * in a context that includes the block ids.
 * Within each iteration of these loops an additional code generation
 * is performed (within create_kernel_leaf) for the rest of the schedule
 * in a context that includes the thread ids.
 */
static __isl_give isl_ast_node *generate_kernel(struct gpu_gen *gen,
	__isl_keep isl_ast_build *build, __isl_keep isl_set *host_domain,
	__isl_keep isl_multi_pw_aff *grid_size)
{
	isl_space *space;
	isl_set *set;
	isl_id_list *iterators;
	isl_union_map *schedule;
	isl_ast_node *tree;
	int sched_len;

	schedule = isl_ast_build_get_schedule(build);

	build = isl_ast_build_copy(build);
	build = isl_ast_build_restrict(build, isl_set_copy(host_domain));
	space = isl_ast_build_get_schedule_space(build);
	set = isl_set_universe(isl_space_copy(space));
	set = add_bounded_parameters_dynamic(set, grid_size, "b");
	build = isl_ast_build_restrict(build, set);

	schedule = body_schedule(gen, schedule);

	sched_len = 2 * (gen->shared_len - gen->tile_first) + 1;

	build = set_atomic_and_unroll(build, space, sched_len);
	iterators = generate_names(gen->ctx, sched_len, "g");
	build = isl_ast_build_set_iterators(build, iterators);
	build = isl_ast_build_set_create_leaf(build, &create_kernel_leaf, gen);
	tree = isl_ast_build_ast_from_schedule(build, schedule);
	isl_ast_build_free(build);

	return tree;
}

/* Attach "id" to the given node.
 */
static __isl_give isl_ast_node *attach_id(__isl_take isl_ast_node *node,
	__isl_keep isl_ast_build *build, void *user)
{
	isl_id *id = user;

	node = isl_ast_node_set_annotation(node, id);

	return node;
}

/* Construct an AST node for performing a kernel launch and attach
 * the information about the kernel to that node.
 *
 * The kernel AST has been constructed in the context of the range
 * of "schedule".  In particular, the grid size has been computed
 * in the context.  We therefore still need to make sure that these
 * constraints are expressed in the code.  We do this by creating a schedule
 *
 *	kernel[] -> [S -> []]
 *
 * where S is the schedule domain, i.e., the range of "schedule".
 * The AST generation will then create a single call surrounded by
 * all the condition in "S" that have not been expressed yet.
 *
 * The kernel information is attached to this node in attach_id.
 */
static __isl_give isl_ast_node *construct_launch(
	__isl_take isl_ast_build *build, __isl_take isl_union_map *schedule,
	__isl_take struct ppcg_kernel *kernel)
{
	isl_id *id;
	isl_ctx *ctx;
	isl_union_set *domain;
	isl_set *set;
	isl_map *map;
	isl_ast_node *node;

	ctx = isl_ast_build_get_ctx(build);

	id = isl_id_alloc(ctx, NULL, kernel);
	id = isl_id_set_free_user(id, &ppcg_kernel_free);

	domain = isl_union_map_range(schedule);
	set = isl_set_from_union_set(domain);
	map = isl_map_from_domain(set);
	map = isl_map_from_range(isl_map_wrap(map));
	map = isl_map_set_tuple_name(map, isl_dim_in, "kernel");
	schedule = isl_union_map_from_map(map);

	build = isl_ast_build_set_at_each_domain(build, &attach_id, id);
	node = isl_ast_build_ast_from_schedule(build, schedule);
	isl_ast_build_free(build);

	return node;
}

/* This function is called for each leaf in the AST of the host code.
 * We first specialize the schedule to the site of the leaf, compute
 * the size of shared memory and then construct the body of the host code
 * and the associated kernel.
 *
 * The necessary information for printing the kernel launch is
 * stored in a struct ppcg_kernel and attached to the leaf node
 * created to represent the launch.
 */
static __isl_give isl_ast_node *create_host_leaf(
	__isl_take isl_ast_build *build, void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	isl_id *id;
	isl_ast_node *node;
	struct ppcg_kernel *kernel;
	isl_set *host_domain;
	isl_union_map *schedule;
	isl_union_map *local_sched;
	isl_union_map *access;
	isl_union_set *domain;
	int i;

	schedule = isl_ast_build_get_schedule(build);

	isl_union_map_foreach_map(schedule, &extract_tile_len, gen);
	read_sizes(gen);

	domain = isl_union_map_domain(isl_union_map_copy(schedule));

	local_sched = isl_union_map_copy(gen->sched);
	local_sched = isl_union_map_intersect_domain(local_sched, domain);
	access = isl_union_map_union(isl_union_map_copy(gen->prog->read),
				     isl_union_map_copy(gen->prog->may_write));
	access = isl_union_map_apply_domain(access,
					    isl_union_map_copy(local_sched));

	gen->tiled_sched = tile_schedule(gen, local_sched);
	gen->tiled_sched = parametrize_tiled_schedule(gen, gen->tiled_sched);
	gen->tiled_sched = scale_tile_loops(gen, gen->tiled_sched);

	gen->local_sched = isl_union_map_copy(gen->tiled_sched);
	gen->local_sched = thread_tile_schedule(gen, gen->local_sched);
	gen->local_sched = scale_thread_tile_loops(gen, gen->local_sched);

	kernel = gen->kernel = isl_calloc_type(gen->ctx, struct ppcg_kernel);
	if (!kernel)
		goto error;

	kernel->id = gen->kernel_id++;
	kernel->context = isl_union_map_params(isl_union_map_copy(schedule));
	kernel->grid_size = extract_grid_size(gen, kernel);
	extract_block_size(gen, kernel);
	kernel->arrays = isl_union_map_range(access);
	kernel->arrays = isl_union_set_apply(kernel->arrays,
				isl_union_map_copy(gen->prog->to_outer));
	kernel->space = isl_ast_build_get_schedule_space(build);

	gen->private_access = NULL;
	compute_shared_sched(gen);
	gen->privatization = compute_privatization(gen);
	check_scalar_live_ranges(gen);
	if (group_references(gen) < 0)
		schedule = isl_union_map_free(schedule);
	compute_private_access(gen);
	host_domain = isl_set_from_union_set(isl_union_map_range(
						isl_union_map_copy(schedule)));
	localize_bounds(gen, kernel, host_domain);

	gen->local_sched = interchange_for_unroll(gen, gen->local_sched);
	check_shared_memory_bound(gen);
	compute_group_tilings(gen);

	kernel->tree = generate_kernel(gen, build, host_domain,
					kernel->grid_size);
	create_kernel_vars(gen, kernel);

	free_local_array_info(gen);
	isl_map_free(gen->privatization);
	isl_union_map_free(gen->private_access);
	isl_union_map_free(gen->local_sched);
	isl_union_map_free(gen->tiled_sched);
	isl_union_map_free(gen->shared_sched);
	isl_union_map_free(gen->shared_proj);
	isl_set_free(host_domain);
	free(gen->tile_size);

	node = construct_launch(build, schedule, kernel);

	return node;
error:
	isl_union_map_free(schedule);
	return NULL;
}

/* Use isl to generate code for the outer gen->tile_first loops
 * of the global schedule in gen->sched, resulting in the host code.
 * Within each iteration of this partial schedule, i.e., for each kernel
 * launch, create_host_leaf takes care of generating the kernel code.
 */
static __isl_give isl_ast_node *generate_host_code(struct gpu_gen *gen)
{
	isl_ast_build *build;
	isl_ast_node *tree;
	isl_union_map *sched;
	isl_map *proj;
	isl_id_list *iterators;

	sched = isl_union_map_copy(gen->sched);
	proj = projection(isl_union_map_get_space(sched),
			    gen->untiled_len, gen->tile_first);
	sched = isl_union_map_apply_range(sched, isl_union_map_from_map(proj));

	isl_options_set_ast_build_group_coscheduled(gen->ctx, 1);
	build = isl_ast_build_from_context(isl_set_copy(gen->prog->context));
	iterators = generate_names(gen->ctx, gen->tile_first, "h");
	build = isl_ast_build_set_iterators(build, iterators);
	build = isl_ast_build_set_create_leaf(build, &create_host_leaf, gen);
	tree = isl_ast_build_ast_from_schedule(build, sched);
	isl_ast_build_free(build);

	return tree;
}

__isl_give isl_union_map *extract_sizes_from_str(isl_ctx *ctx, const char *str)
{
	if (!str)
		return NULL;
	return isl_union_map_read_from_str(ctx, str);
}

/* Information about the outermost tilable bands in the forest of bands.
 *
 * tile_len and n_parallel are only sets on band_info structures
 * that correspond to outermost bands.  For other bands (in particular,
 * ancestors of the outermost bands), n_parallal is set to 0.
 *
 * prefix is the (padded) schedule leading up to the outermost tilable bands.
 *
 * tile_first is the number of schedule dimensions in prefix.
 *
 * suffix is the schedule of the outermost tilable bands and their descendants.
 */
struct band_info {
	struct gpu_gen *gen;
	int tile_first;
	int tile_len;
	int n_parallel;
	isl_union_map *prefix;
	isl_union_map *suffix;
};

/* Set tile_len and n_parallel of the statement to that of
 * their outermost band, recorded in the band_info.
 */
static int set_stmt_tile_len(__isl_take isl_map *map, void *user)
{
	struct band_info *info = user;
	struct gpu_stmt *stmt;
	isl_id *id;

	id = isl_map_get_tuple_id(map, isl_dim_in);
	stmt = find_stmt(info->gen->prog, id);
	isl_id_free(id);

	stmt->tile_len = info->tile_len;
	stmt->n_parallel = info->n_parallel;

	isl_map_free(map);

	return 0;
}

static void list_select_outer_band(struct gpu_gen *gen,
	__isl_take isl_band_list *list, int pos, struct band_info *list_info);

/* Check if this band has any parallel loops.  If so, take it as
 * the outermost tilable band.  If not, continue looking for the
 * outermost tilable band in the children of the current band.
 */
static void band_select_outer_band(struct gpu_gen *gen,
	__isl_take isl_band *band, int pos, struct band_info *info)
{
	int n = isl_band_n_member(band);
	int n_parallel;

	for (n_parallel = 0; n_parallel < n; ++n_parallel)
		if (!isl_band_member_is_coincident(band, n_parallel))
			break;

	info->n_parallel = n_parallel;
	if (n_parallel) {
		gen->any_parallelism = 1;
		info->gen = gen;
		info->tile_first = pos;
		info->tile_len = n;
		info->prefix = isl_band_get_prefix_schedule(band);
		info->suffix = isl_union_map_flat_range_product(
				isl_band_get_partial_schedule(band),
				isl_band_get_suffix_schedule(band));
		isl_union_map_foreach_map(info->prefix,
					    &set_stmt_tile_len, info);
	} else if (isl_band_has_children(band)) {
		isl_band_list *children;
		children = isl_band_get_children(band);
		list_select_outer_band(gen, children, pos + n, info);
	} else {
		info->gen = gen;
		info->tile_first = pos + n;
		info->tile_len = 0;
		info->prefix = isl_union_map_flat_range_product(
				isl_band_get_prefix_schedule(band),
				isl_band_get_partial_schedule(band));
		info->suffix = isl_band_get_suffix_schedule(band);
		isl_union_map_foreach_map(info->prefix,
					    &set_stmt_tile_len, info);
	}

	isl_band_free(band);
}

/* Comparison function that returns a non-zero value for band_infos
 * with different tile_len fields or different n_parallel fields.
 */
static int cmp_band(const void *p1, const void *p2)
{
	const struct band_info *info1 = p1;
	const struct band_info *info2 = p2;

	if (info1->tile_len != info2->tile_len)
		return info1->tile_len - info2->tile_len;

	return info1->n_parallel - info2->n_parallel;
}

/* Extend "umap" with coordinates with fixed value "val"
 * to a total length of "dst_len", assuming the original dimension is "src_len".
 */
static __isl_give isl_union_map *extend_range(
	__isl_take isl_union_map *umap, int src_len, int dst_len, int val)
{
	isl_space *dim;
	isl_map *map;
	int i;

	dim = isl_union_map_get_space(umap);
	map = isl_map_reverse(projection(dim, dst_len, src_len));
	for (i = src_len; i < dst_len; ++i)
		map = isl_map_fix_si(map, isl_dim_out, i, val);

	umap = isl_union_map_apply_range(umap, isl_union_map_from_map(map));

	return umap;
}

/* Group bands with the same values for tile_len and n_parallel.
 * The prefix schedule is then extended with a fixed coordinate that
 * is different for each such group.
 * Note that the actual values for this coordinate are not important.
 * The bands have already been effectively separated at a higher level
 * or they are independent and may be executed in parallel.
 * The list of band_info has been sorted before this functions is called.
 */
static void separate_bands(struct band_info *info, int n)
{
	int i;
	int j = 0;

	for (i = 0; i < n; ++i) {
		int l = info[i].tile_first;

		if (i &&
		    (info[i].tile_len != info[i - 1].tile_len ||
		     info[i].n_parallel != info[i - 1].n_parallel))
			j++;

		info[i].prefix = extend_range(info[i].prefix,
						l, l + 1, j);
		info[i].tile_first = l + 1;
	}
}

/* Select the outermost bands in the elements of the list, align
 * their prefix schedules, separate bands with different values
 * for tile_len and/or n_parallel and then combine the resulting
 * prefix and suffix schedules into a single pair of prefix and
 * suffix schedules for the entire list.
 */
static void list_select_outer_band(struct gpu_gen *gen,
	__isl_take isl_band_list *list, int pos, struct band_info *list_info)
{
	isl_band *band;
	int i;
	int n = isl_band_list_n_band(list);
	isl_ctx *ctx = isl_band_list_get_ctx(list);
	struct band_info *info;
	int max_tile_first;
	isl_union_map *prefix;
	isl_union_map *suffix;

	assert(n >= 1);
	info = isl_calloc_array(ctx, struct band_info, n);
	assert(info);

	max_tile_first = 0;
	for (i = 0; i < n; ++i) {
		band = isl_band_list_get_band(list, i);
		band_select_outer_band(gen, band, pos, &info[i]);
		if (info[i].tile_first > max_tile_first)
			max_tile_first = info[i].tile_first;
	}

	for (i = 0; i < n; ++i) {
		if (info[i].tile_first == max_tile_first)
			continue;
		info[i].prefix = extend_range(info[i].prefix,
					info[i].tile_first, max_tile_first, 0);
		info[i].tile_first = max_tile_first;
	}

	qsort(info, n, sizeof(struct band_info), &cmp_band);

	for (i = 0; i < n - 1; ++i)
		if (info[i].tile_len != info[i + 1].tile_len ||
		    info[i].n_parallel != info[i + 1].n_parallel)
			break;

	if (i < n -1)
		separate_bands(info, n);

	prefix = info[0].prefix;
	suffix = info[0].suffix;

	for (i = 1; i < n; ++i) {
		prefix = isl_union_map_union(prefix, info[i].prefix);
		suffix = isl_union_map_union(suffix, info[i].suffix);
	}

	list_info->tile_first = info[0].tile_first;
	list_info->tile_len = -1;
	list_info->prefix = prefix;
	list_info->suffix = suffix;

	isl_band_list_free(list);
	free(info);
}

/* Select the outermost tilable band that (by construction)
 * has at least one parallel loop.
 * The starting position of the aligned band is stored in the pair
 * gen->tile_first.
 * The sizes and number of parallel loops may be different in different
 * parts of the band forest and are therefore stored in the gpu_stmts.
 *
 * Return the complete schedule, with the tilable bands aligned
 * at gen->tile_first and padded with zero, if needed.
 */
static __isl_give isl_union_map *select_outer_tilable_band(struct gpu_gen *gen,
	__isl_keep isl_schedule *schedule)
{
	isl_band_list *list;
	struct band_info info;

	gen->n_parallel = 0;
	gen->tile_len = -1;

	list = isl_schedule_get_band_forest(schedule);

	if (isl_band_list_n_band(list) == 0) {
		isl_band_list_free(list);
		return isl_schedule_get_map(schedule);
	}

	list_select_outer_band(gen, list, 0, &info);

	gen->tile_first = info.tile_first;
	info.suffix = align_range(info.suffix);

	return isl_union_map_flat_range_product(info.prefix, info.suffix);
}

/* Set gen->untiled_len to the number of scheduling dimensions
 * for the schedule of the first domain.
 * We assume here that this number is the same for all domains.
 */
static int set_untiled_len(__isl_take isl_map *map, void *user)
{
	unsigned *untiled_len = user;

	*untiled_len = isl_map_dim(map, isl_dim_out);

	isl_map_free(map);
	return -1;
}

/* Compute an appropriate schedule based on the accesses in
 * gen->read and gen->write.
 *
 * We use the dependences in gen->prog->scop to compute
 * a schedule that has a parallel loop in each tilable band.
 * Finally, we select the outermost tilable band.
 *
 * If live range reordering is allowed, then we need to make sure
 * that live ranges on arrays are not run in parallel since doing
 * so would require array expansion.  We therefore add the array
 * order dependences to the coincidence dependences.  Non-zero array
 * order dependences will then prevent a schedule dimension from being
 * considered parallel.
 * Live ranges derived from scalars are allowed to be run in parallel
 * since we force the scalars to be mapped to private memory in
 * check_scalar_live_ranges.
 * If live range reordering is allowed, then the false dependences
 * are not added to the validity constraints as that would prevent
 * reordering.  Instead, the external false dependences that enforce that reads
 * from potentially live-in data precede any later write and
 * that writes of potentially live-out data follow any other earlier write
 * are added to the validity and the coincidence constraints.
 * The false dependences are still added to the proximity constraints
 * for consistency with the case where live range reordering is not allowed.
 * The coincidence constraints then consist of flow dependences,
 * exernal false dependences and array order dependences.
 * The independences can be filtered out from the first two sets.
 * They have already been filtered out from the array order dependences
 * on a per array basis in collect_order_dependences.
 * There is no need for a per array handling of the other two sets
 * as there should be no flow or external false dependence on local
 * variables that can be filtered out.
 */
static void compute_schedule(struct gpu_gen *gen)
{
	isl_union_set *domain;
	isl_union_map *dep_raw, *dep;
	isl_union_map *validity, *proximity, *coincidence;
	isl_union_map *sched;
	isl_schedule_constraints *sc;
	isl_schedule *schedule;

	domain = isl_union_set_copy(gen->prog->scop->domain);
	domain = isl_union_set_intersect_params(domain,
				isl_set_copy(gen->prog->scop->context));
	sc = isl_schedule_constraints_on_domain(isl_union_set_copy(domain));
	if (gen->options->live_range_reordering) {
		sc = isl_schedule_constraints_set_conditional_validity(sc,
			isl_union_map_copy(gen->prog->scop->tagged_dep_flow),
			isl_union_map_copy(gen->prog->scop->tagged_dep_order));
		proximity = isl_union_map_copy(gen->prog->scop->dep_flow);
		validity = isl_union_map_copy(proximity);
		validity = isl_union_map_union(validity,
			    isl_union_map_copy(gen->prog->scop->dep_external));
		proximity = isl_union_map_union(proximity,
			    isl_union_map_copy(gen->prog->scop->dep_false));
		coincidence = isl_union_map_copy(validity);
		coincidence = isl_union_map_subtract(coincidence,
			isl_union_map_copy(gen->prog->scop->independence));
		coincidence = isl_union_map_union(coincidence,
				isl_union_map_copy(gen->prog->array_order));
	} else {
		dep_raw = isl_union_map_copy(gen->prog->scop->dep_flow);
		dep = isl_union_map_copy(gen->prog->scop->dep_false);
		dep = isl_union_map_union(dep, dep_raw);
		dep = isl_union_map_coalesce(dep);
		proximity = isl_union_map_copy(dep);
		coincidence = isl_union_map_copy(dep);
		validity = dep;
	}
	sc = isl_schedule_constraints_set_validity(sc, validity);
	sc = isl_schedule_constraints_set_coincidence(sc, coincidence);
	sc = isl_schedule_constraints_set_proximity(sc, proximity);

	if (gen->options->debug->dump_schedule_constraints)
		isl_schedule_constraints_dump(sc);
	schedule = isl_schedule_constraints_compute_schedule(sc);
	if (gen->options->debug->dump_schedule)
		isl_schedule_dump(schedule);

	sched = select_outer_tilable_band(gen, schedule);

	isl_union_map_foreach_map(sched, &set_untiled_len, &gen->untiled_len);
	sched = isl_union_map_intersect_domain(sched, domain);
	gen->sched = sched;

	isl_schedule_free(schedule);
}

/* Compute the sets of outer array elements that need to be copied in and out.
 *
 * In particular, for each array that is possibly written anywhere in
 * gen->prog and that is visible outside the corresponding scop,
 * we copy out its entire extent.
 *
 * Any array elements that is read without first being written needs
 * to be copied in. Furthermore, if there are any array elements that
 * are copied out, but that may not be written inside gen->prog, then
 * they also need to be copied in to ensure that the value after execution
 * is the same as the value before execution.
 * In case the array elements are structures, we need to take into
 * account that all members of the structures need to be written
 * by gen->prog before we can avoid copying the data structure in.
 *
 * While computing the set of array elements that are copied out but
 * not necessarily written, we intersect both sets with the context.
 * This helps in those cases where the arrays are declared with a fixed size,
 * while the accesses are parametric and the context assigns a fixed value
 * to the parameters.
 *
 * If an element from a local array is read without first being written,
 * then there is no point in copying it in since it cannot have been
 * written prior to the scop.  Warn about the uninitialized read instead.
 */
static void compute_copy_in_and_out(struct gpu_gen *gen)
{
	int i;
	isl_union_set *local;
	isl_union_set *may_write, *must_write;
	isl_union_set *copy_in, *copy_out;
	isl_union_set *not_written;
	isl_union_map *uninitialized;
	isl_union_map *local_uninitialized;

	must_write = isl_union_map_range(
				isl_union_map_copy(gen->prog->must_write));
	must_write = isl_union_set_intersect_params(must_write,
					    isl_set_copy(gen->prog->context));
	may_write = isl_union_map_range(
				isl_union_map_copy(gen->prog->may_write));
	may_write = isl_union_set_intersect_params(may_write,
					    isl_set_copy(gen->prog->context));
	may_write = isl_union_set_universe(may_write);
	may_write = isl_union_set_apply(may_write,
				    isl_union_map_copy(gen->prog->to_outer));
	copy_out = isl_union_set_empty(isl_union_set_get_space(may_write));
	local = isl_union_set_copy(copy_out);

	for (i = 0; i < gen->prog->n_array; ++i) {
		isl_space *space;
		isl_set *write_i;
		int empty;

		space = isl_space_copy(gen->prog->array[i].space);

		if (gen->prog->array[i].local) {
			isl_set *set;

			set = isl_set_universe(space);
			local = isl_union_set_add_set(local, set);
			continue;
		}

		write_i = isl_union_set_extract_set(may_write, space);
		empty = isl_set_plain_is_empty(write_i);
		isl_set_free(write_i);
		if (empty)
			continue;

		write_i = isl_set_copy(gen->prog->array[i].extent);
		copy_out = isl_union_set_add_set(copy_out, write_i);
	}
	isl_union_set_free(may_write);

	copy_out = isl_union_set_intersect_params(copy_out,
					    isl_set_copy(gen->prog->context));

	gen->prog->copy_out = isl_union_set_copy(copy_out);

	copy_out = isl_union_set_apply(copy_out,
				    isl_union_map_copy(gen->prog->to_inner));
	not_written = isl_union_set_subtract(copy_out, must_write);

	uninitialized = isl_union_map_copy(gen->prog->scop->live_in);
	local_uninitialized = isl_union_map_copy(uninitialized);

	local = isl_union_set_apply(local,
				    isl_union_map_copy(gen->prog->to_inner));
	local_uninitialized = isl_union_map_intersect_range(local_uninitialized,
							    local);
	if (!isl_union_map_is_empty(local_uninitialized)) {
		fprintf(stderr,
			"possibly uninitialized reads (not copied in):\n");
		isl_union_map_dump(local_uninitialized);
	}
	uninitialized = isl_union_map_subtract(uninitialized,
						local_uninitialized);
	copy_in = isl_union_map_range(uninitialized);
	copy_in = isl_union_set_union(copy_in, not_written);
	copy_in = isl_union_set_apply(copy_in,
				    isl_union_map_copy(gen->prog->to_outer));

	gen->prog->copy_in = copy_in;
}

/* Internal data structure for extract_access.
 * "next_access" points to the end of a linked list that is extended
 * by extract_access.
 * "single_expression" is set if the access expressions belong to
 * an expression statement (i.e., a statement without internal control).
 */
struct ppcg_extract_access_data {
	struct gpu_stmt_access **next_access;
	int single_expression;
};

/* Extract a gpu_stmt_access from "expr", append it to the list
 * that ends in *data->next_access and update the end of the list.
 * If the access expression performs a write, then it is considered
 * exact only if it appears in a single expression statement and
 * if its may access relation is equal to its must access relation.
 */
static int extract_access(__isl_keep pet_expr *expr, void *user)
{
	struct ppcg_extract_access_data *data = user;
	isl_map *may;
	struct gpu_stmt_access *access;
	isl_ctx *ctx;

	may = pet_expr_access_get_may_access(expr);
	ctx = isl_map_get_ctx(may);
	access = isl_alloc_type(ctx, struct gpu_stmt_access);
	assert(access);
	access->next = NULL;
	access->read = pet_expr_access_is_read(expr);
	access->write = pet_expr_access_is_write(expr);
	access->access = may;
	access->tagged_access = pet_expr_access_get_tagged_may_access(expr);
	if (!access->write) {
		access->exact_write = 1;
	} else if (!data->single_expression) {
		access->exact_write = 0;
	} else {
		isl_map *must;
		must = pet_expr_access_get_must_access(expr);
		access->exact_write = isl_map_is_equal(must, access->access);
		isl_map_free(must);
	}
	access->ref_id = pet_expr_access_get_ref_id(expr);
	access->group = -1;

	*data->next_access = access;
	data->next_access = &(*data->next_access)->next;

	return 0;
}

/* Construct a linked list of gpu_stmt_access objects,
 * one for each access expression in the statement body.
 */
static void pet_stmt_extract_accesses(struct gpu_stmt *stmt)
{
	struct ppcg_extract_access_data data;

	stmt->accesses = NULL;
	data.next_access = &stmt->accesses;
	data.single_expression =
		pet_tree_get_type(stmt->stmt->body) == pet_tree_expr;
	pet_tree_foreach_access_expr(stmt->stmt->body, &extract_access, &data);
}

/* Return an array of gpu_stmt representing the statements in "scop".
 */
static struct gpu_stmt *extract_stmts(isl_ctx *ctx, struct ppcg_scop *scop,
	__isl_keep isl_set *context)
{
	int i;
	struct gpu_stmt *stmts;

	stmts = isl_calloc_array(ctx, struct gpu_stmt, scop->n_stmt);
	if (!stmts)
		return NULL;

	for (i = 0; i < scop->n_stmt; ++i) {
		struct gpu_stmt *s = &stmts[i];

		s->id = isl_set_get_tuple_id(scop->stmts[i]->domain);
		s->stmt = scop->stmts[i];
		pet_stmt_extract_accesses(s);
	}

	return stmts;
}

/* Callback for ppcg_print_guarded that calls the callback for generate_gpu.
 */
static __isl_give isl_printer *print_gpu(__isl_take isl_printer *p, void *user)
{
	struct gpu_gen *gen = user;

	return gen->print(p, gen->prog, gen->tree, &gen->types,
			    gen->print_user);
}

/* Generate CUDA code for "scop" and print it to "p".
 * After generating an AST for the transformed scop as explained below,
 * we call "gen->print" to print the AST in the desired output format
 * to "p".
 *
 * If it turns out that it does not make sense to generate GPU code,
 * then we generate CPU code instead.
 *
 * The GPU code is generated in a context where at least one
 * statement instance is executed.  The corresponding guard (if any) is printed
 * around the entire generated GPU code, except for the declaration
 * of the arrays that are visible outside of the scop and that therefore
 * cannot be declared inside the body of any possible guard.
 *
 * We first compute a schedule that respects the dependences
 * of the original program and select the outermost band
 * of tilable dimensions that has at least one parallel loop.
 * We then have three blocks of dimensions
 *
 *	H		B			G
 *
 * The tilable band "B" is first tiled according to "tile" sizes, resulting
 * in
 *
 *	H	T		P		G
 *
 * For each iteration of the T loop and for each array, we compute
 * the array elements accessed by that iteration, construct a rectangular
 * box around it and shift it to the origin.  The result is used
 * as shared memory for the array.
 *
 * We then split off at most 2 parallel loops from the T loops and
 * at most 3 parallel loops from the P loops
 *
 *	H	T1	T2	P1	P2	G
 *
 * The T1/P1 loops are then tiled or "wrapped" over the blocks/threads,
 * according to "grid"/"block" sizes.
 *
 *	H	T1T T1P	T2	P1T P1P	P2	G
 *
 * Finally, the T1P and P1P iterators are equated to the block and
 * thread dimensions respectively and so are effectively removed.
 * The H loops are run on the host.  The T1T, T2, P1T, P2 and G loops
 * are run on the GPU.
 *
 * Code is generated in three stages.  We first generate code for the
 * host (the H loops), with iterators h%d.  Then, for each leaf node
 * of the resulting AST, we generate code for the shared loops (up to
 * and including T2), with iterators g%d and after equating the H loops
 * to h%d parameters and the T1P loops to the block dimensions.
 * Finally, we generate code for the remaining loops in a similar fashion.
 */
static __isl_give isl_printer *generate(__isl_take isl_printer *p,
	struct gpu_gen *gen, struct ppcg_scop *scop,
	struct ppcg_options *options)
{
	struct gpu_prog *prog;
	isl_ctx *ctx;
	isl_set *context, *guard;

	if (!scop)
		return isl_printer_free(p);

	ctx = isl_printer_get_ctx(p);
	prog = gpu_prog_alloc(ctx, scop);
	if (!prog)
		return isl_printer_free(p);

	context = isl_set_copy(prog->context);
	guard = isl_union_set_params(isl_union_set_copy(prog->scop->domain));
	prog->context = isl_set_intersect(prog->context, isl_set_copy(guard));

	gen->prog = prog;
	gen->any_parallelism = 0;
	compute_schedule(gen);

	if (!gen->any_parallelism) {
		isl_set_free(context);
		isl_set_free(guard);
		p = print_cpu(p, scop, options);
	} else {
		compute_copy_in_and_out(gen);
		gen->tree = generate_host_code(gen);
		p = ppcg_print_exposed_declarations(p, prog->scop);
		p = ppcg_print_guarded(p, guard, context, &print_gpu, gen);
		isl_ast_node_free(gen->tree);
	}

	isl_union_map_free(gen->sched);

	gpu_prog_free(prog);

	return p;
}

/* Wrapper around generate for use as a ppcg_transform callback.
 */
static __isl_give isl_printer *generate_wrap(__isl_take isl_printer *p,
	struct ppcg_scop *scop, void *user)
{
	struct gpu_gen *gen = user;

	return generate(p, gen, scop, gen->options);
}

/* Transform the code in the file called "input" by replacing
 * all scops by corresponding GPU code and write the results to "out".
 */
int generate_gpu(isl_ctx *ctx, const char *input, FILE *out,
	struct ppcg_options *options,
	__isl_give isl_printer *(*print)(__isl_take isl_printer *p,
		struct gpu_prog *prog, __isl_keep isl_ast_node *tree,
		struct gpu_types *types, void *user), void *user)
{
	struct gpu_gen gen;
	int r;
	int i;

	gen.ctx = ctx;
	gen.sizes = extract_sizes_from_str(ctx, options->sizes);
	gen.options = options;
	gen.kernel_id = 0;
	gen.print = print;
	gen.print_user = user;
	gen.types.n = 0;
	gen.types.name = NULL;

	if (options->debug->dump_sizes) {
		isl_space *space = isl_space_params_alloc(ctx, 0);
		gen.used_sizes = isl_union_map_empty(space);
	}

	r = ppcg_transform(ctx, input, out, options, &generate_wrap, &gen);

	if (options->debug->dump_sizes) {
		isl_union_map_dump(gen.used_sizes);
		isl_union_map_free(gen.used_sizes);
	}

	isl_union_map_free(gen.sizes);
	for (i = 0; i < gen.types.n; ++i)
		free(gen.types.name[i]);
	free(gen.types.name);

	return r;
}

struct gpu_prog *gpu_prog_alloc(isl_ctx *ctx, struct ppcg_scop *scop)
{
	struct gpu_prog *prog;

	if (!scop)
		return NULL;

	prog = isl_calloc_type(ctx, struct gpu_prog);
	assert(prog);

	prog->ctx = ctx;
	prog->scop = scop;
	prog->context = isl_set_copy(scop->context);
	prog->n_stmts = scop->n_stmt;
	prog->stmts = extract_stmts(ctx, scop, prog->context);
	prog->read = isl_union_map_copy(scop->reads);
	prog->may_write = isl_union_map_copy(scop->may_writes);
	prog->must_write = isl_union_map_copy(scop->must_writes);
	prog->to_inner = compute_to_inner(scop);
	prog->to_outer = isl_union_map_copy(prog->to_inner);
	prog->to_outer = isl_union_map_reverse(prog->to_outer);

	if (!prog->stmts)
		return gpu_prog_free(prog);

	if (collect_array_info(prog) < 0)
		return gpu_prog_free(prog);

	return prog;
}

void *gpu_prog_free(struct gpu_prog *prog)
{
	if (!prog)
		return NULL;
	free_array_info(prog);
	free_stmts(prog->stmts, prog->n_stmts);
	isl_union_map_free(prog->to_outer);
	isl_union_map_free(prog->to_inner);
	isl_union_set_free(prog->copy_in);
	isl_union_set_free(prog->copy_out);
	isl_union_map_free(prog->read);
	isl_union_map_free(prog->may_write);
	isl_union_map_free(prog->must_write);
	isl_union_map_free(prog->array_order);
	isl_set_free(prog->context);
	free(prog);
	return NULL;
}
