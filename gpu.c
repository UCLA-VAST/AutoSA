/*
 * Copyright 2010-2011 INRIA Saclay
 * Copyright 2012      Ecole Normale Superieure
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 * and Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <assert.h>
#include <stdlib.h>

#include <isl/polynomial.h>
#include <isl/union_set.h>
#include <isl/aff.h>
#include <isl/ilp.h>
#include <isl/flow.h>
#include <isl/band.h>
#include <isl/schedule.h>
#include <isl/options.h>
#include <isl/ast_build.h>

#include "gpu.h"
#include "schedule.h"
#include "ppcg_options.h"

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
	isl_int size;
	isl_aff *lb;

	isl_int stride;
	isl_aff *shift;
	isl_basic_map *shift_map;
};

struct gpu_array_info;

/* A group of array references in a kernel that should be handled together.
 * If private_bound is not NULL, then it is mapped to registers.
 * Otherwise, if shared_bound is not NULL, it is mapped to shared memory.
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
	 */
	isl_map *access;
	int write;

	/* For each index, size and offset of piece in shared memory. */
	struct gpu_array_bound *shared_bound;

	/* For each index, size and offset of piece in private memory. */
	struct gpu_array_bound *private_bound;

	/* References in this group; point to elements of a linked list. */
	int n_ref;
	struct gpu_stmt_access **refs;

	/* Last shared memory tile dimension that affects tile of this group. */
	int last_shared;
};

struct gpu_gen {
	isl_ctx *ctx;
	struct ppcg_options *options;

	struct gpu_prog *prog;

	/* tile, grid and block sizes for each kernel */
	isl_union_map *sizes;

	/* Identifier of current kernel. */
	int kernel_id;
	/* Pointer to the current kernel. */
	struct ppcg_kernel *kernel;

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
	/* copy_group->private_bound or copy_group->shared_bound */
	struct gpu_array_bound *copy_bound;

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

	if (group->private_bound)
		p = isl_printer_print_str(p, "private_");
	else if (group->shared_bound)
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
 */
static void collect_references(struct gpu_prog *prog,
	struct gpu_array_info *array)
{
	int i;
	int n;

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

static struct gpu_array_bound *create_bound_list(isl_ctx *ctx, int n_index)
{
	int i;
	struct gpu_array_bound *bound;

	bound = isl_alloc_array(ctx, struct gpu_array_bound, n_index);
	assert(bound);

	for (i = 0; i < n_index; ++i) {
		isl_int_init(bound[i].size);
		bound[i].lb = NULL;
		isl_int_init(bound[i].stride);
		bound[i].shift = NULL;
		bound[i].shift_map = NULL;
	}

	return bound;
}

static void free_bound_list(struct gpu_array_bound *bound, int n_index)
{
	int j;

	if (!bound)
		return;

	for (j = 0; j < n_index; ++j) {
		isl_int_clear(bound[j].size);
		isl_int_clear(bound[j].stride);
		isl_aff_free(bound[j].lb);
		isl_aff_free(bound[j].shift);
		isl_basic_map_free(bound[j].shift_map);
	}
	free(bound);
}

static struct pet_array *find_array(struct pet_scop *scop,
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

/* Compute bounds on the host arrays based on the accessed elements
 * and collect all references to the array.
 *
 * If the array is zero-dimensional, i.e., a scalar, we check
 * whether it is read-only.
 */
static int extract_array_info(__isl_take isl_set *array, void *user)
{
	int i;
	struct gpu_prog *prog = (struct gpu_prog *)user;
	const char *name;
	int n_index;
	isl_pw_aff **bounds;
	struct pet_array *pa;

	n_index = isl_set_dim(array, isl_dim_set);
	name = isl_set_get_tuple_name(array);
	bounds = isl_alloc_array(isl_set_get_ctx(array),
				 isl_pw_aff *, n_index);
	assert(bounds);
	prog->array[prog->n_array].dim = isl_set_get_space(array);
	prog->array[prog->n_array].name = strdup(name);
	prog->array[prog->n_array].n_index = n_index;
	prog->array[prog->n_array].bound = bounds;

	pa = find_array(prog->scop, array);
	assert(pa);

	prog->array[prog->n_array].type = strdup(pa->element_type);
	prog->array[prog->n_array].size = pa->element_size;

	if (n_index == 0) {
		isl_set *space;
		isl_union_map *write;
		int empty;

		write = isl_union_map_copy(prog->write);
		space = isl_set_universe(isl_set_get_space(array));
		write = isl_union_map_intersect_range(write,
				    isl_union_set_from_set(space));
		empty = isl_union_map_is_empty(write);
		isl_union_map_free(write);

		prog->array[prog->n_array].read_only = empty;
	}

	for (i = 0; i < n_index; ++i) {
		isl_set *dom;
		isl_local_space *ls;
		isl_aff *one;
		isl_pw_aff *bound;
		isl_set *size = i == 0 ? array : pa->extent;

		bound = isl_set_dim_max(isl_set_copy(size), i);
		assert(bound);
		dom = isl_pw_aff_domain(isl_pw_aff_copy(bound));
		ls = isl_local_space_from_space(isl_set_get_space(dom));
		one = isl_aff_zero_on_domain(ls);
		one = isl_aff_add_constant_si(one, 1);
		bound = isl_pw_aff_add(bound, isl_pw_aff_alloc(dom, one));
		bound = isl_pw_aff_gist(bound, isl_set_copy(prog->context));

		bounds[i] = bound;
	}

	collect_references(prog, &prog->array[prog->n_array]);

	prog->n_array++;

	isl_set_free(array);
	return 0;
}

void collect_array_info(struct gpu_prog *prog)
{
	isl_union_set *arrays;

	arrays = isl_union_map_range(isl_union_map_copy(prog->read));
	arrays = isl_union_set_union(arrays,
			isl_union_map_range(isl_union_map_copy(prog->write)));
	arrays = isl_union_set_coalesce(arrays);

	prog->n_array = isl_union_set_n_set(arrays);
	prog->array = isl_alloc_array(prog->ctx,
				     struct gpu_array_info, prog->n_array);
	assert(prog->array);
	prog->n_array = 0;
	isl_union_set_foreach_set(arrays, &extract_array_info, prog);
	isl_union_set_free(arrays);
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
		isl_space_free(prog->array[i].dim);
		free(prog->array[i].bound);
		free(prog->array[i].refs);
	}
	free(prog->array);
}

/* Check if a gpu array is a scalar.  A scalar is a value that is not stored
 * as an array or through a pointer reference, but as single data element.  At
 * the moment, scalars are represented as zero dimensional arrays.
 */
int gpu_array_is_scalar(struct gpu_array_info *array)
{
	return (array->n_index == 0);
}

/* Is "array" a read-only scalar?
 */
int gpu_array_is_read_only_scalar(struct gpu_array_info *array)
{
	return gpu_array_is_scalar(array) && array->read_only;
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
	isl_int v;

	if (!set)
		return;

	dim = isl_set_dim(set, isl_dim_set);
	if (dim < *len)
		*len = dim;

	isl_int_init(v);

	for (i = 0; i < *len; ++i) {
		int ok;

		ok = isl_set_plain_is_fixed(set, isl_dim_set, i, &v);
		assert(ok);

		sizes[i] = isl_int_get_si(v);
	}

	isl_int_clear(v);

	isl_set_free(set);
}

/* Extract user specified "tile" sizes from the "sizes" command line option,
 * defaulting to option->tile_size in each dimension.
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

	if (gen->n_parallel > gen->tile_len)
		gen->n_parallel = gen->tile_len;
}

/* Extract user specified "block" sizes from the "sizes" command line option,
 * after filling in some potentially useful defaults.
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
}

/* Extract user specified "grid" sizes from the "sizes" command line option,
 * after filling in some potentially useful defaults.
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

static void free_stmts(struct gpu_stmt *stmts, int n)
{
	int i;

	for (i = 0; i < n; ++i) {
		struct gpu_stmt_access *access, *next;

		for (access = stmts[i].accesses; access; access = next) {
			next = access->next;
			isl_map_free(access->access);
			free(access);
		}

		isl_set_free(stmts[i].domain);
	}
	free(stmts);
}

void clear_gpu_gen(struct gpu_gen *gen)
{
	isl_union_map_free(gen->sizes);
	isl_union_map_free(gen->sched);
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
	isl_int v;
	isl_basic_map *bmap;
	isl_constraint *c;
	isl_local_space *ls;

	isl_int_init(v);

	dim = isl_space_add_dims(dim, isl_dim_in, len);
	dim = isl_space_add_dims(dim, isl_dim_out, len + tile_len);
	bmap = isl_basic_map_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	for (i = 0; i < len - tile_len; ++i) {
		int j = i < first ? i : i + tile_len;
		int k = i < first ? i : i + 2 * tile_len;

		c = isl_equality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, -1);
		isl_constraint_set_coefficient(c, isl_dim_in, j, v);
		isl_int_set_si(v, 1);
		isl_constraint_set_coefficient(c, isl_dim_out, k, v);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	for (i = 0; i < tile_len; ++i) {
		c = isl_equality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, -1);
		isl_constraint_set_coefficient(c, isl_dim_in, first + i, v);
		isl_int_set_si(v, tile_size[i]);
		isl_constraint_set_coefficient(c, isl_dim_out, first + i, v);
		isl_int_set_si(v, 1);
		isl_constraint_set_coefficient(c, isl_dim_out,
						first + i + tile_len, v);
		bmap = isl_basic_map_add_constraint(bmap, c);

		c = isl_inequality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, 1);
		isl_constraint_set_coefficient(c, isl_dim_out,
						first + i + tile_len, v);
		bmap = isl_basic_map_add_constraint(bmap, c);
	
		c = isl_inequality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, -1);
		isl_constraint_set_coefficient(c, isl_dim_out,
						first + i + tile_len, v);
		isl_int_set_si(v, tile_size[i] - 1);
		isl_constraint_set_constant(c, v);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	isl_local_space_free(ls);
	isl_int_clear(v);

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
		isl_constraint_set_coefficient_si(c, isl_dim_in, i, -1);
		isl_constraint_set_coefficient_si(c, isl_dim_out, k, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}

	for (i = 0; i < wrap_len; ++i) {
		c = isl_equality_alloc(isl_local_space_copy(ls));
		isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + i, -1);
		isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + wrap_len + i, 1);
		isl_constraint_set_coefficient_si(c, isl_dim_out,
				    first + 2 * wrap_len + i, wrap_size[i]);
		bmap = isl_basic_map_add_constraint(bmap, c);

		c = isl_inequality_alloc(isl_local_space_copy(ls));
		isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + wrap_len + i, 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	
		c = isl_inequality_alloc(isl_local_space_copy(ls));
		isl_constraint_set_coefficient_si(c, isl_dim_out,
						    first + wrap_len + i, -1);
		isl_constraint_set_constant_si(c, wrap_size[i] - 1);
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
	isl_int v;
	isl_space *dim;
	isl_basic_set *bset;
	isl_constraint *c;
	isl_local_space *ls;

	nparam = isl_set_dim(set, isl_dim_param);

	set = add_params(set, n, prefix);

	dim = isl_set_get_space(set);
	bset = isl_basic_set_universe(isl_space_copy(dim));
	ls = isl_local_space_from_space(dim);

	isl_int_init(v);

	for (i = 0; i < n; ++i) {
		c = isl_equality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, -1);
		isl_constraint_set_coefficient(c, isl_dim_param, nparam + i, v);
		isl_int_set_si(v, 1);
		isl_constraint_set_coefficient(c, isl_dim_set, first + i, v);
		bset = isl_basic_set_add_constraint(bset, c);
	}

	isl_int_clear(v);
	isl_local_space_free(ls);

	return isl_set_intersect(set, isl_set_from_basic_set(bset));
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
		isl_constraint_set_coefficient_si(c, isl_dim_in, i, f);
		isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
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
		isl_constraint_set_coefficient_si(c, isl_dim_in, i, f);
		isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
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
			f = gen->block_dim[i - first];

		c = isl_equality_alloc(isl_local_space_copy(ls));
		isl_constraint_set_coefficient_si(c, isl_dim_in, i, f);
		isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
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
	isl_int v;
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

	isl_int_init(v);

	for (i = 0; i < len; ++i) {
		c = isl_inequality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, 1);
		isl_constraint_set_coefficient(c, isl_dim_param, nparam + i, v);
		bset = isl_basic_set_add_constraint(bset, c);
	
		c = isl_inequality_alloc(isl_local_space_copy(ls));
		isl_int_set_si(v, -1);
		isl_constraint_set_coefficient(c, isl_dim_param, nparam + i, v);
		isl_int_set_si(v, size[i] - 1);
		isl_constraint_set_constant(c, v);
		bset = isl_basic_set_add_constraint(bset, c);
	}

	isl_int_clear(v);
	isl_local_space_free(ls);

	return isl_set_intersect(set, isl_set_from_basic_set(bset));
}

/* Add "len" parameters p[i] called prefix%d,
 * with bounds to 0 <= p[i] < size[i].
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
		set = isl_set_intersect_params(set, bound);

		zero = isl_pw_aff_zero_on_domain(isl_local_space_copy(ls));
		bound = isl_pw_aff_ge_set(param, zero);
		set = isl_set_intersect_params(set, bound);
	}
	isl_local_space_free(ls);

	return set;
}

/* Given a mapping "sched" of the form
 *
 *	[D -> A] -> [D -> T(A)]
 *
 * apply the mapping encoded in bounds[i].shift_map to the range of "sched".
 * The mappings in bounds[i].shift_map are of the form
 *
 *	[D -> a] -> [D -> s(D,a)]
 *
 * We first compose them with a mapping
 *
 *	[D -> v] -> v
 *
 * (If bounds[i].shift_map is not set, then it is assumed to be
 * an identity mapping and then we use this second mapping instead.)
 * This results in
 *
 *	[D -> a] -> s(D,a)
 *
 * We precompose them with a projection on the i th dimension to obtain
 *
 *	[D -> T] -> s(D,T)
 *
 * and collect these into
 *
 *	[D -> T] -> S(D,T)
 *
 * Introducing D in the range yields
 *
 *	[D -> T] -> [D -> S(D,T)]
 *
 * and application to "sched" yields
 *
 *	[D -> A] -> [D -> S(D,T(A))]
 */
static __isl_give isl_map *pre_shift(__isl_take isl_map *sched,
	int n_index, struct gpu_array_bound *bounds)
{
	int i;
	isl_ctx *ctx = isl_map_get_ctx(sched);
	isl_space *space, *space2;
	isl_basic_map *def;
	isl_map *map, *id, *pre_shift;

	space = isl_space_range(isl_map_get_space(sched));
	space2 = isl_space_from_domain(isl_space_copy(space));
	pre_shift = isl_map_universe(space2);
	space = isl_space_domain(isl_space_unwrap(space));
	id = isl_map_identity(isl_space_map_from_set(isl_space_copy(space)));
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, 1);
	def = isl_basic_map_range_map(isl_basic_map_universe(space));

	for (i = 0; i < n_index; ++i) {
		isl_basic_map *bmap, *drop;
		isl_map *proj;

		space = isl_space_alloc(ctx, 0, n_index, n_index);
		proj = isl_map_identity(space);
		proj = isl_map_project_out(proj, isl_dim_out,
						i + 1, n_index - (i + 1));
		proj = isl_map_project_out(proj, isl_dim_out, 0, i);
		proj = isl_map_product(isl_map_copy(id), proj);

		if (!bounds[i].shift_map)
			bmap = isl_basic_map_copy(def);
		else {
			bmap = isl_basic_map_copy(bounds[i].shift_map);
			bmap = isl_basic_map_apply_range(bmap,
						isl_basic_map_copy(def));
		}

		map = isl_map_from_basic_map(bmap);
		map = isl_map_apply_range(proj, map);
		pre_shift = isl_map_flat_range_product(pre_shift, map);
	}

	isl_map_free(id);
	isl_basic_map_free(def);

	space = isl_space_domain(isl_map_get_space(pre_shift));
	map = isl_map_domain_map(isl_map_universe(isl_space_unwrap(space)));
	pre_shift = isl_map_range_product(map, pre_shift);

	sched = isl_map_apply_range(sched, pre_shift);

	return sched;
}

/* Given an access relation to a tile of an array, construct a map that
 * maps each element in the space of the access relation
 * to a copy of the tile shifted to the origin
 * (based on the lower bounds in group->private_bound or group->shared_bound).
 * If any of the indices is strided, then {private,shared}_bound[i].shift_map
 * is applied to the index first.
 * The domain space of the resulting map is that of access "access",
 * while the range space is anonymous.
 * The resulting map only encodes the mapping to the shift tile and
 * not the constraints of "access".
 *
 * Let the space of the access relation be
 *
 *	D -> A
 *
 * We first construct an identity relation on a wrapped copy of this space,
 * except that it strips off the name of array
 *
 *	[D -> A] -> [D -> T(A)]					(1)
 *
 * The bounds in bounds[i].lb are of the form
 *
 *	D -> b(D)
 *
 * We collect them into
 *
 *	D -> B(D)
 *
 * and then transform them into
 *
 *	[D -> T] -> T - B(D)					(2)
 *
 * Combining those two mappings (1) and (2) yields
 *
 *	[D -> A] -> T(A) - B(D)
 *
 * If there are any strides, then (1) is first transformed into (1')
 *
 *	[D -> A] -> [D -> T'(A)]				(1')
 *
 * by a call to pre_shift.
 */
static __isl_give isl_map *shift_access(__isl_take isl_map *access,
	struct gpu_array_ref_group *group)
{
	int i;
	isl_space *space;
	isl_map *id1, *id2;
	isl_map *map;
	isl_map *shift;
	isl_map *sched;
	struct gpu_array_bound *bounds;
	int n_index = group->array->n_index;

	bounds = group->private_bound;
	if (!bounds)
		bounds = group->shared_bound;

	space = isl_space_domain(isl_map_get_space(access));
	space = isl_space_map_from_set(space);
	id1 = isl_map_identity(space);
	space = isl_space_range(isl_map_get_space(access));
	space = isl_space_map_from_set(space);
	space = isl_space_set_tuple_name(space, isl_dim_out, NULL);
	id2 = isl_map_identity(space);
	sched = isl_map_product(id1, id2);

	space = isl_space_unwrap(isl_space_range(isl_map_get_space(sched)));
	space = isl_space_from_domain(isl_space_domain(space));
	shift = isl_map_universe(space);
	for (i = 0; i < n_index; ++i) {
		map = isl_map_from_aff(isl_aff_copy(bounds[i].lb));
		shift = isl_map_flat_range_product(shift, map);
	}

	space = isl_space_unwrap(isl_space_range(isl_map_get_space(sched)));
	map = isl_map_universe(space);
	id1 = isl_map_range_map(isl_map_copy(map));
	map = isl_map_domain_map(map);
	shift = isl_map_neg(shift);
	shift = isl_map_apply_range(map, shift);
	shift = isl_map_sum(id1, shift);

	for (i = 0; i < n_index; ++i)
		if (bounds[i].shift_map)
			break;

	if (i < n_index)
		sched = pre_shift(sched, n_index, bounds);

	sched = isl_map_apply_range(sched, shift);

	isl_map_free(access);

	return sched;
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

	n_tile = gen->n_block;
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
		if (!isl_map_plain_is_fixed(sched, isl_dim_out,
						first + n_tile - 1, NULL))
			break;

	dim = isl_map_get_space(sched);
	dim = isl_space_params(dim);
	if (gen->options->wrap)
		tiling = wrap(isl_space_copy(dim), nvar, first,
				n_tile, gen->block_dim);
	else
		tiling = tile(isl_space_copy(dim), nvar, first,
				n_tile, gen->block_dim);
	sched = isl_map_apply_range(sched, tiling);

	par = parametrization(dim, nvar + n_tile, first + n_tile, n_tile, "t");
	sched = isl_map_intersect_range(sched, par);

	usched = isl_union_map_from_map(sched);
	usched = scale_access_tile_loops(gen, usched, nvar + n_tile,
					 first, n_tile);
	sched = isl_map_from_union_map(usched);

	return sched;
}

/* Given an index expression "pa" into a tile of an array, adjust the expression
 * to a shift of the tile to the origin
 * (based on the lower bounds in "bound".
 * If the index is strided, then we first add
 * bound->shift and divide by bound->stride.
 * In the end, we compute the gist with respect to "domain".
 *
 * All of the input expression "pa", the set "domain" and
 * the output are expressed in terms of the AST schedule domain.
 * The expressions in "bound" are expressed
 * in terms of the first shared_len dimensions of the schedule computed by PPCG.
 * The mapping "sched2shared" maps the former domain to the latter domain.
 */
static __isl_give isl_pw_aff *shift_index(__isl_take isl_pw_aff *pa,
	struct gpu_array_info *array,
	struct gpu_array_bound *bound, __isl_take isl_set *domain,
	__isl_take isl_map *sched2shared)
{
	isl_map *map;
	isl_pw_aff *tmp;
	isl_pw_multi_aff *pma;

	if (bound->shift) {
		map = isl_map_from_aff(isl_aff_copy(bound->shift));
		map = isl_map_apply_range(isl_map_copy(sched2shared), map);
		pma = isl_pw_multi_aff_from_map(map);
		tmp = isl_pw_multi_aff_get_pw_aff(pma, 0);
		isl_pw_multi_aff_free(pma);
		pa = isl_pw_aff_add(pa, tmp);
		pa = isl_pw_aff_scale_down(pa, bound->stride);
	}


	map = isl_map_from_aff(isl_aff_copy(bound->lb));
	map = isl_map_apply_range(sched2shared, map);
	pma = isl_pw_multi_aff_from_map(map);
	tmp = isl_pw_multi_aff_get_pw_aff(pma, 0);
	isl_pw_multi_aff_free(pma);
	pa = isl_pw_aff_sub(pa, tmp);
	pa = isl_pw_aff_coalesce(pa);
	pa = isl_pw_aff_gist(pa, domain);

	return pa;
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

/* Return a map from the first shared_len dimensions of the computed
 * schedule to the values of the given index "i"
 * of the elements in the array tile in global memory that corresponds
 * to the shared memory copy.
 * In particular, if a is the index, then the range of the map
 *
 *	{ D -> [a] }
 *
 * is constrained as follows
 *
 *	tile_offset(D) <= a <= tile_offset(D) + tile_size - 1		(1)
 *
 * and
 *
 *	0 <= a <= array_size - 1					(2)
 *
 *
 * Note that if some stride has been detected (i.e., when
 * group->shared_bound[i].shift is set), then offset and size (i.e.,
 * constraints (1)) apply to the shifted and scaled down copy of the tile.
 * These constraints therefore have to be mapped back to the original
 * array space using the inverse of the shift_map.
 */
static __isl_give isl_map *group_tile_dim(struct gpu_array_ref_group *group,
	int i)
{
	isl_aff *aff;
	isl_space *space;
	isl_map *map, *tile, *gt;
	isl_set *bound;

	map = isl_map_from_aff(isl_aff_copy(group->shared_bound[i].lb));
	space = isl_space_range(isl_map_get_space(map));
	map = isl_map_apply_range(map, isl_map_lex_le(isl_space_copy(space)));
	tile = map;

	aff = isl_aff_copy(group->shared_bound[i].lb);
	aff = isl_aff_add_constant(aff, group->shared_bound[i].size);
	map = isl_map_from_aff(aff);
	gt = isl_map_lex_gt(space);
	map = isl_map_apply_range(map, isl_map_copy(gt));
	tile = isl_map_intersect(tile, map);

	if (group->shared_bound[i].shift) {
		isl_basic_map *shift;
		shift = isl_basic_map_copy(group->shared_bound[i].shift_map);
		shift = isl_basic_map_reverse(shift);
		tile = isl_set_unwrap(isl_set_apply(isl_map_wrap(tile),
					isl_map_from_basic_map(shift)));
	}

	tile = isl_map_lower_bound_si(tile, isl_dim_out, 0, 0);

	bound = isl_set_from_pw_aff(isl_pw_aff_copy(group->array->bound[i]));
	bound = isl_set_apply(bound, gt);
	tile = isl_map_intersect_range(tile, bound);

	return tile;
}

/* Return a map from the first shared_len dimensions of the computed
 * schedule to the array tile in
 * global memory that corresponds to the shared memory copy.
 */
static __isl_give isl_map *group_tile(struct gpu_array_ref_group *group)
{
	int i;
	int n_index = group->array->n_index;
	isl_map *tile;

	tile = group_tile_dim(group, 0);
	for (i = 1; i < n_index; ++i) {
		isl_map *tile_i;

		tile_i = group_tile_dim(group, i);
		tile = isl_map_flat_range_product(tile, tile_i);
	}

	tile = isl_map_set_tuple_name(tile, isl_dim_out, group->array->name);

	return tile;
}

/* Given a mapping "sched" from the AST schedule to a domain,
 * return the corresponding mapping from the AST schedule to
 * to the first shared_len dimensions of the schedule computed by PPCG.
 */
static __isl_give isl_map *compute_sched_to_shared(struct gpu_gen *gen,
	__isl_take isl_map *sched)
{
	isl_union_map *umap;
	isl_space *space;
	isl_map *map;

	space = isl_space_range(isl_map_get_space(sched));
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, gen->shared_len);

	umap = isl_union_map_copy(gen->shared_sched);
	umap = isl_union_map_apply_range(umap,
			isl_union_map_copy(gen->shared_proj));
	map = isl_union_map_extract_map(umap, space);
	isl_union_map_free(umap);

	sched = isl_map_apply_range(sched, map);
	sched = isl_map_detect_equalities(sched);

	return sched;
}

/* Set unroll[j] if the input dimension j is involved in
 * the index expression represented by bmap.
 */
static int check_unroll(__isl_take isl_basic_map *bmap, void *user)
{
	int i, j;
	int n_in = isl_basic_map_dim(bmap, isl_dim_in);
	int n_out = isl_basic_map_dim(bmap, isl_dim_out);
	int *unroll = user;

	for (i = 0; i < n_out; ++i) {
		isl_constraint *c;
		int ok;

		ok = isl_basic_map_has_defining_equality(bmap,
							isl_dim_out, i, &c);
		assert(ok);
		for (j = 0; j < n_in; ++j)
			if (isl_constraint_involves_dims(c, isl_dim_in, j, 1))
				unroll[j] = 1;
		isl_constraint_free(c);
	}

	isl_basic_map_free(bmap);
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
		isl_constraint_set_coefficient_si(c, isl_dim_in, i, -1);
		isl_constraint_set_coefficient_si(c, isl_dim_out, pos[i], 1);
		bmap = isl_basic_map_add_constraint(bmap, c);
	}
	isl_local_space_free(ls);

	return isl_map_from_basic_map(bmap);
}

/* Find all loops involved in any of the index expressions for any of
 * the private accesses, move them innermost and then mark them as
 * requiring unrolling by setting gen->first_unroll.
 * The loops involved should all be parallel because of the checks
 * we performed in check_private_group_access.  Moving them innermost
 * is therefore a valid transformation.
 *
 * Loops up to gen->shared_len are generated before the mapping to
 * threads is applied.  They should therefore be ignored.
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

	for (i = 0; i < gen->thread_tiled_len; ++i)
		unroll[i] = 0;
	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			isl_union_map *access;
			isl_map *acc;

			if (!array->groups[j]->private_bound)
				continue;

			access = group_access_relation(array->groups[j], 1, 1);
			access = isl_union_map_apply_domain(access,
						isl_union_map_copy(sched));

			acc = isl_map_from_union_map(access);
			isl_map_foreach_basic_map(acc, &check_unroll, unroll);

			isl_map_free(acc);
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
	struct gpu_array_bound *bound, isl_int stride, int sign)
{
	int i;
	isl_int v;
	isl_space *space;
	unsigned nparam;
	unsigned nvar;
	isl_aff *aff;

	isl_int_set(bound->stride, stride);

	space = isl_constraint_get_space(c);
	space = isl_space_domain(space);

	nparam = isl_space_dim(space, isl_dim_param);
	nvar = isl_space_dim(space, isl_dim_set);

	isl_int_init(v);

	isl_constraint_get_constant(c, &v);
	if (sign < 0)
		isl_int_neg(v, v);
	aff = isl_aff_zero_on_domain(isl_local_space_from_space(space));
	aff = isl_aff_set_constant(aff, v);

	for (i = 0; i < nparam; ++i) {
		isl_constraint_get_coefficient(c, isl_dim_param, i, &v);
		if (isl_int_is_zero(v))
			continue;
		if (sign < 0)
			isl_int_neg(v, v);
		aff = isl_aff_add_coefficient(aff, isl_dim_param, i, v);
	}

	for (i = 0; i < nvar; ++i) {
		isl_constraint_get_coefficient(c, isl_dim_in, i, &v);
		if (isl_int_is_zero(v))
			continue;
		if (sign < 0)
			isl_int_neg(v, v);
		aff = isl_aff_add_coefficient(aff, isl_dim_in, i, v);
	}

	isl_int_clear(v);

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
	isl_int v, stride;
	unsigned n_div;
	struct gpu_array_bound *bound = user;

	isl_int_init(v);
	isl_int_init(stride);

	n_div = isl_constraint_dim(c, isl_dim_div);
	isl_constraint_get_coefficient(c, isl_dim_out, 0, &v);

	if (n_div && (isl_int_is_one(v) || isl_int_is_negone(v))) {
		int s = isl_int_sgn(v);
		isl_int_set_si(stride, 0);
		for (i = 0; i < n_div; ++i) {
			isl_constraint_get_coefficient(c, isl_dim_div, i, &v);
			isl_int_gcd(stride, stride, v);
		}
		if (!isl_int_is_zero(stride) &&
		    isl_int_gt(stride, bound->stride))
			extract_stride(c, bound, stride, s);
	}

	isl_int_clear(stride);
	isl_int_clear(v);

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

	isl_int_set_si(bound->stride, -1);

	hull = isl_basic_map_affine_hull(isl_basic_map_copy(bounds));

	isl_basic_map_foreach_constraint(hull, &check_stride_constraint, bound);

	isl_basic_map_free(hull);

	if (isl_int_is_neg(bound->stride))
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
	aff = isl_aff_scale_down(aff, bound->stride);
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
	isl_int v;

	nparam = isl_basic_set_dim(size->bset, isl_dim_param);
	n_div = isl_constraint_dim(c, isl_dim_div);

	if (isl_constraint_involves_dims(c, isl_dim_div, 0, n_div)) {
		isl_constraint_free(c);
		return 0;
	}

	isl_int_init(v);

	isl_constraint_get_coefficient(c, isl_dim_set, size->pos, &v);

	if (isl_int_is_pos(v)) {
		isl_aff *aff;
		isl_aff *lb;
		enum isl_lp_result res;

		aff = isl_constraint_get_bound(c, isl_dim_set, size->pos);
		aff = isl_aff_ceil(aff);

		lb = isl_aff_copy(aff);

		aff = isl_aff_neg(aff);
		aff = isl_aff_add_coefficient_si(aff, isl_dim_in, size->pos, 1);

		res = isl_basic_set_max(size->bset, aff, &v);
		isl_aff_free(aff);

		if (res == isl_lp_ok) {
			isl_int_add_ui(v, v, 1);
			if (isl_int_is_neg(size->bound->size) ||
			    isl_int_lt(v, size->bound->size)) {
				isl_int_set(size->bound->size, v);
				lb = isl_aff_drop_dims(lb, isl_dim_in,
							size->pos, 1);
				isl_aff_free(size->bound->lb);
				size->bound->lb = isl_aff_copy(lb);
			}
		}
		isl_aff_free(lb);
	}

	isl_int_clear(v);
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

	isl_int_set_si(bound->size, -1);
	bound->lb = NULL;

	size.bound = bound;
	size.pos = isl_basic_map_dim(bounds, isl_dim_in);
	size.bset = isl_basic_map_wrap(bounds);
	size.bset = isl_basic_set_flatten(size.bset);
	size.bset = isl_set_simple_hull(isl_basic_set_compute_divs(size.bset));
	isl_basic_set_foreach_constraint(size.bset, &compute_size_in_direction,
					&size);
	isl_basic_set_free(size.bset);

	return isl_int_is_nonneg(bound->size) ? 0 : -1;
}

/* Check if we can find a shared memory tile for the given array
 * based on the given accesses, and if so, put the results
 * in array->shared_bound.
 *
 * We project the accesses on each index in turn and look for a parametric
 * offset such that the size is constant.
 */
static int can_tile_for_shared_memory(struct gpu_array_info *array,
	__isl_keep isl_map *access, struct gpu_array_bound *bounds)
{
	int i;

	for (i = 0; i < array->n_index; ++i) {
		isl_map *access_i;
		isl_basic_map *hull;

		access_i = isl_map_copy(access);
		access_i = isl_map_project_out(access_i, isl_dim_out, 0, i);
		access_i = isl_map_project_out(access_i, isl_dim_out,
					    1, array->n_index - (i + 1));
		access_i = isl_map_compute_divs(access_i);
		hull = isl_map_simple_hull(access_i);
		if (compute_array_dim_size(&bounds[i], hull) < 0)
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
		isl_constraint_set_coefficient_si(c, isl_dim_in, i, 1);
		isl_constraint_set_coefficient_si(c, isl_dim_out, i, -1);
		if (i == pos)
			isl_constraint_set_constant_si(c, 1);
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

/* For the given array reference group, check whether the access is private
 * to the thread.  That is, check that any given array element
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
 * If it turns out we can use registers, we compute the private memory
 * tile size using can_tile_for_shared_memory, after introducing a dependence
 * on the thread indices.
 *
 * Before performing any of the above computations, we first check
 * if there is any reuse on the reference group.  If not, we simply
 * return.  If, moreover, the access is coalesced then we also remove
 * the shared memory tiling since we should just use global memory instead.
 */
static void check_private_group_access(struct gpu_gen *gen,
	struct gpu_array_ref_group *group)
{
	isl_map *acc;
	isl_union_map *access;
	int n_index = group->array->n_index;

	access = group_access_relation(group, 1, 1);
	if (isl_union_map_is_injective(access)) {
		if (group->shared_bound && access_is_coalesced(gen, access)) {
			free_bound_list(group->shared_bound, n_index);
			group->shared_bound = NULL;
		}
		isl_union_map_free(access);
		return;
	}
	access = isl_union_map_apply_domain(access,
					isl_union_map_copy(gen->shared_sched));

	acc = isl_map_from_union_map(access);

	if (!access_is_bijective(gen, acc)) {
		isl_map_free(acc);
		return;
	}

	group->private_bound = create_bound_list(gen->ctx, n_index);
	acc = isl_map_apply_domain(acc, isl_map_copy(gen->privatization));
	if (!can_tile_for_shared_memory(group->array, acc,
					group->private_bound)) {
		free_bound_list(group->private_bound, n_index);
		group->private_bound = NULL;
	}

	isl_map_free(acc);
}

/* Look for the last shared tile loop that affects the offset of the
 * shared or private tile and store the result in array->last_shared.
 * If there is no such loop, then array->last_shared is set to a value
 * before the first shared tile loop, in particular gen->tile_first - 1.
 */
static void set_last_shared(struct gpu_gen *gen,
	struct gpu_array_ref_group *group)
{
	int i, j;
	struct gpu_array_bound *bounds;
	int n_index = group->array->n_index;

	bounds = group->private_bound;
	if (!bounds)
		bounds = group->shared_bound;
	if (!bounds)
		return;

	for (j = gen->shared_len - 1; j >= gen->tile_first; --j) {
		for (i = 0; i < n_index; ++i) {
			isl_aff *lb;
			isl_aff *shift;

			lb = bounds[i].lb;
			if (isl_aff_involves_dims(lb, isl_dim_in, j, 1))
				break;

			shift = bounds[i].shift;
			if (!shift)
				continue;
			if (isl_aff_involves_dims(shift, isl_dim_in, j, 1))
				break;
		}
		if (i < n_index)
			break;
	}
	group->last_shared = j;
}

/* Compute the sizes of all private arrays for the current kernel,
 * as well as the offsets of the private pieces in the original arrays.
 * If we cannot or don't want to privatize a given array group,
 * we use the shared memory tile sizes computed in
 * compute_group_shared_bound instead.
 *
 * If we have been able to find a private or shared tile,
 * we also look for the last shared tile loop that affects the offset
 * (and therefore the group tile) and store the result in group->last_shared.
 *
 * A privatized copy of all access relations from reference groups that
 * are mapped to private memory is stored in gen->privatization.
 */
static void compute_private_size(struct gpu_gen *gen)
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

		for (j = 0; j < array->n_group; ++j) {
			check_private_group_access(gen, array->groups[j]);

			if (!array->groups[j]->private_bound)
				continue;

			private = isl_union_map_union(private,
				group_access_relation(array->groups[j], 1, 1));
		}

		for (j = 0; j < array->n_group; ++j) {
			array->groups[j]->last_shared = gen->shared_len - 1;
			set_last_shared(gen, array->groups[j]);
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

/* Compute the size of the tile specified by the list "bound" of n_index
 * gpu_array_bounds in number of elements and put the result in *size.
 */
static void tile_size(unsigned n_index, struct gpu_array_bound *bound,
	isl_int *size)
{
	int i;

	isl_int_set_si(*size, 1);

	for (i = 0; i < n_index; ++i)
		isl_int_mul(*size, *size, bound[i].size);
}

/* If max_shared_memory is not set to infinity (-1), then make
 * sure that the total amount of shared memory required by the
 * array reference groups mapped to shared memory is no larger
 * than this maximum.
 *
 * We apply a greedy approach and discard (keep in global memory)
 * those groups that would result in a total memory size that
 * is larger than the maximum.
 */
static void check_shared_memory_bound(struct gpu_gen *gen)
{
	int i, j;
	isl_int left, size;

	if (gen->options->max_shared_memory < 0)
		return;

	isl_int_init(left);
	isl_int_init(size);
	isl_int_set_si(left, gen->options->max_shared_memory);

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group;

			group = array->groups[j];
			if (!group->shared_bound)
				continue;

			tile_size(array->n_index, group->shared_bound, &size);
			isl_int_mul_ui(size, size, array->size);

			if (isl_int_le(size, left)) {
				isl_int_sub(left, left, size);
				continue;
			}

			free_bound_list(group->shared_bound, array->n_index);
			group->shared_bound = NULL;
		}
	}

	isl_int_clear(size);
	isl_int_clear(left);
}

/* Fill up the groups array with singleton groups, i.e., one group
 * per reference, initializing the array, access, write and refs fields.
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
		group->refs = &array->refs[i];

		groups[n++] = group;
	}

	return n;
}

static void free_array_ref_group(struct gpu_array_ref_group *group,
	int n_index)
{
	if (!group)
		return;
	free_bound_list(group->shared_bound, n_index);
	free_bound_list(group->private_bound, n_index);
	isl_map_free(group->access);
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
		if (!isl_map_plain_is_fixed(access, isl_dim_in, i, NULL))
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

/* If two groups have overlapping access relations (within the innermost
 * loop) and if one of them involves a write, then merge the two groups
 * into one.
 *
 * We keep track of the grouping in "leader".  leader[j] points to
 * an earlier group array element that belongs to the same group,
 * or the array element j itself if this element is the first in the group.
 *
 * Return the number of group leaders.
 */
static int group_overlapping_writes(int n,
	struct gpu_array_ref_group **groups, int *leader)
{
	int i, j;
	int n_group = n;

	for (i = 0; i < n; ++i) {
		int l = i;
		groups[l]->n_ref = 1;
		for (j = i - 1; j >= 0; --j) {
			if (leader[j] != j)
				continue;
			if (!groups[l]->write && !groups[j]->write)
				continue;

			if (!accesses_overlap(groups[l], groups[j]))
				continue;

			groups[j]->access = isl_map_union(groups[j]->access,
							groups[l]->access);
			groups[j]->write = 1;
			groups[l]->access = NULL;
			groups[j]->n_ref += groups[l]->n_ref;
			l = leader[l] = j;
			n_group--;
		}
		leader[i] = l;
	}

	return n_group;
}

/* Compute the size of the shared array corresponding to the given
 * array reference group, based on the accesses from the current kernel,
 * as well as the offset of the shared piece in the original array.
 */
static void compute_group_shared_bound(struct gpu_gen *gen,
	struct gpu_array_info *array, struct gpu_array_ref_group *group)
{
	isl_ctx *ctx = isl_space_get_ctx(array->dim);

	if (!gen->options->use_shared_memory)
		return;
	if (gpu_array_is_read_only_scalar(array))
		return;

	group->shared_bound = create_bound_list(ctx, array->n_index);
	if (!can_tile_for_shared_memory(array, group->access,
					group->shared_bound)) {
		free_bound_list(group->shared_bound, array->n_index);
		group->shared_bound = NULL;
	}
}

/* Is the size of the tile specified by "bound" smaller than the sum of
 * the sizes of the tiles specified by "bound1" and "bound2"?
 */
static int smaller_tile(unsigned n_index, struct gpu_array_bound *bound,
	struct gpu_array_bound *bound1, struct gpu_array_bound *bound2)
{
	int smaller;
	isl_int size, size1, size2;

	isl_int_init(size);
	isl_int_init(size1);
	isl_int_init(size2);

	tile_size(n_index, bound, &size);
	tile_size(n_index, bound1, &size1);
	tile_size(n_index, bound2, &size2);

	isl_int_sub(size, size, size1);
	isl_int_sub(size, size, size2);
	smaller = isl_int_is_neg(size);

	isl_int_clear(size2);
	isl_int_clear(size1);
	isl_int_clear(size);

	return smaller;
}

/* Given an initial grouping of array references and shared memory tiles
 * for each group that allows for a shared memory tile, merge two groups
 * if both have a shared memory tile, the merged group also has
 * a shared memory tile and the size of the tile for the merge group
 * is smaller than the sum of the tile sizes of the individual groups.
 *
 * Return the number of group leaders after merging.
 */
static int group_common_shared_memory_tile(struct gpu_array_info *array, int n,
	struct gpu_array_ref_group **groups, int *leader, int n_group)
{
	int i, j;
	isl_ctx *ctx = isl_space_get_ctx(array->dim);

	for (i = 0; n_group > 1 && i < n; ++i) {
		int l = i;
		if (leader[i] != i)
			continue;
		if (!groups[i]->shared_bound)
			continue;
		for (j = i - 1; j >= 0; --j) {
			isl_map *map;
			int empty;
			struct gpu_array_bound *shared_bound;

			if (leader[j] != j)
				continue;
			if (!groups[j]->shared_bound)
				continue;

			map = isl_map_intersect(isl_map_copy(groups[l]->access),
					    isl_map_copy(groups[j]->access));
			empty = isl_map_is_empty(map);
			isl_map_free(map);

			if (empty)
				continue;

			map = isl_map_union(isl_map_copy(groups[l]->access),
					    isl_map_copy(groups[j]->access));
			shared_bound = create_bound_list(ctx, array->n_index);
			if (!can_tile_for_shared_memory(array, map,
							shared_bound) ||
			    !smaller_tile(array->n_index, shared_bound,
					groups[l]->shared_bound,
					groups[j]->shared_bound)) {
				isl_map_free(map);
				free_bound_list(shared_bound, array->n_index);
				continue;
			}

			free_bound_list(groups[j]->shared_bound,
					array->n_index);
			groups[j]->shared_bound = shared_bound;
			isl_map_free(groups[j]->access);
			groups[j]->access = map;
			groups[j]->n_ref += groups[l]->n_ref;
			l = leader[l] = j;
			n_group--;
		}
	}

	return n_group;
}

/* Extract an array of array reference groups from the array of references
 * and the grouping information in "leader".
 *
 * Store the results in array->n_group and array->groups.
 */
static void extract_array_groups(isl_ctx *ctx, struct gpu_array_info *array,
	int n, struct gpu_array_ref_group **groups, int *leader, int n_group)
{
	int i, j;

	for (i = 2; i < n; ++i)
		leader[i] = leader[leader[i]];

	array->n_group = n_group;
	array->groups = isl_alloc_array(ctx, struct gpu_array_ref_group *,
					n_group);
	assert(array->groups);

	j = 0;
	for (i = 0; i < n; ++i) {
		int k, l;
		struct gpu_stmt_access **refs;

		if (leader[i] != i) {
			groups[i]->refs = NULL;
			free_array_ref_group(groups[i], array->n_index);
			continue;
		}

		refs = isl_alloc_array(ctx, struct gpu_stmt_access *,
					groups[i]->n_ref);
		assert(refs);
		l = 0;
		for (k = i; k < n; ++k)
			if (leader[k] == i) {
				refs[l++] = *groups[k]->refs;
				(*groups[k]->refs)->group = j;
			}

		groups[i]->refs = refs;
		groups[i]->nr = j;
		array->groups[j++] = groups[i];
	}
}

/* Group array references that should be considered together when
 * deciding whether to access them from private, shared or global memory.
 *
 * In particular, if two array references overlap and if one of them
 * is a write, then the two references are grouped together.
 * Furthermore, if two groups admit a shared memory tile and if the
 * combination of the two also admits a shared memory tile, we merge
 * the two groups.
 *
 * During the construction the group->refs field points to a single
 * array reference inside the array of array references, while
 * group->n_ref contains the number of element in leader that
 * (directly or indirectly) point to this group, provided the group
 * is a leader.
 */
static void group_array_references(struct gpu_gen *gen,
	struct gpu_array_info *array, __isl_keep isl_union_map *sched)
{
	int i;
	int n, n_group;
	isl_ctx *ctx = isl_union_map_get_ctx(sched);
	struct gpu_array_ref_group **groups;
	int *leader;

	groups = isl_calloc_array(ctx, struct gpu_array_ref_group *,
					array->n_ref);
	assert(groups);

	n = populate_array_references(array, sched, groups);

	leader = isl_alloc_array(ctx, int, n);
	assert(leader);

	n_group = group_overlapping_writes(n, groups, leader);

	for (i = 0; i < n; ++i)
		if (leader[i] == i)
			compute_group_shared_bound(gen, array, groups[i]);

	n_group = group_common_shared_memory_tile(array, n, groups,
						  leader, n_group);

	extract_array_groups(ctx, array, n, groups, leader, n_group);

	free(leader);
	free(groups);
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

/* Group references of all arrays in the program.
 */
static void group_references(struct gpu_gen *gen)
{
	int i;
	isl_union_map *sched;

	sched = isl_union_map_apply_range(isl_union_map_copy(gen->shared_sched),
					  isl_union_map_copy(gen->shared_proj));

	for (i = 0; i < gen->prog->n_array; ++i)
		group_array_references(gen, &gen->prog->array[i], sched);

	isl_union_map_free(sched);
}

/* Free all array information that is local to the current kernel.
 */
static void free_local_array_info(struct gpu_gen *gen)
{
	int i, j;

	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j)
			free_array_ref_group(array->groups[j], array->n_index);
		free(array->groups);
	}
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
	isl_multi_pw_aff *mpa;

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

	mpa = isl_multi_pw_aff_zero(isl_set_get_space(grid));
	for (i = 0; i < gen->n_grid; ++i) {
		isl_space *space;
		isl_aff *one;
		isl_pw_aff *bound;

		bound = isl_set_dim_max(isl_set_copy(grid), i);
		bound = isl_pw_aff_coalesce(bound);
		bound = isl_pw_aff_gist(bound, isl_set_copy(kernel->context));

		space = isl_pw_aff_get_domain_space(bound);
		one = isl_aff_zero_on_domain(isl_local_space_from_space(space));
		one = isl_aff_add_constant_si(one, 1);
		bound = isl_pw_aff_add(bound, isl_pw_aff_from_aff(one));
		mpa = isl_multi_pw_aff_set_pw_aff(mpa, i, bound);
	}
	isl_set_free(grid);

	return mpa;
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
	struct gpu_array_bound *bounds;
	isl_printer *p;
	char *name;

	var->array = group->array;

	bounds = group->private_bound;
	var->type = ppcg_access_private;
	if (!bounds) {
		bounds = group->shared_bound;
		var->type = ppcg_access_shared;
	}

	p = isl_printer_to_str(ctx);
	p = print_array_name(p, group);
	var->name = isl_printer_get_str(p);
	isl_printer_free(p);

	var->size = isl_vec_alloc(ctx, group->array->n_index);

	for (j = 0; j < group->array->n_index; ++j)
		var->size = isl_vec_set_element(var->size, j, bounds[j].size);
}

static void create_kernel_vars(struct gpu_gen *gen, struct ppcg_kernel *kernel)
{
	int i, j, n;

	n = 0;
	for (i = 0; i < gen->prog->n_array; ++i) {
		struct gpu_array_info *array = &gen->prog->array[i];

		for (j = 0; j < array->n_group; ++j) {
			struct gpu_array_ref_group *group = array->groups[j];
			if (group->private_bound || group->shared_bound)
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
			if (!group->private_bound && !group->shared_bound)
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

		if (array->n_group == 0)
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
		isl_id *id_i;

		id_i = isl_set_get_tuple_id(prog->stmts[i].domain);
		isl_id_free(id_i);
		if (id == id_i)
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
		isl_set_free(stmt->u.c.domain);
		isl_pw_multi_aff_free(stmt->u.c.index);
		isl_pw_multi_aff_free(stmt->u.c.local_index);
		break;
	case ppcg_kernel_domain:
		for (i = 0; i < stmt->u.d.n_access; ++i) {
			isl_ast_expr_list_free(stmt->u.d.access[i].index);
			free(stmt->u.d.access[i].local_name);
		}
		free(stmt->u.d.access);
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

/* This function is called for each access to an array in each instance
 * in the kernel of some statement in the original code.
 * Replace that access by an access to global, shared or private memory
 * and store the results in *kernel_access.
 *
 * Since the array in shared or private memory is just
 * a shifted copy of part of the original array, we simply need
 * to subtract the lower bound, which was computed
 * in can_tile_for_shared_memory.
 * If any of the indices is strided, then we first add
 * shared_bound[i].shift and divide by shared_bound[i].stride.
 *
 * If the given array is accessed directly from global memory,
 * we don't need to perform any shifting and simply simplify
 * the expression in the context of the domain instead.
 *
 * If the array space (range of access) has no name, then we are
 * accessing an iterator in the original program.
 *
 * The input stmt_access->access relation maps the iteration domain
 * of the current statement to an array element.
 * The first step is to reformulate
 * this access relation in terms of the loop iterators of the generated
 * code through precomposition with gen->stmt_it.
 *
 * The expressions in "bounds" are formulated in terms of the first
 * gen->shared_len dimensions of the computed schedule using the mapping
 * sched2shared which maps the loop iterators to these dimensions.
 */
static void compute_index_expression(struct gpu_gen *gen,
	struct ppcg_kernel_access *kernel_access,
	struct gpu_stmt_access *stmt_access, __isl_keep isl_map *stmt_it,
	__isl_keep isl_map *sched2shared, __isl_keep isl_ast_build *build)
{
	isl_map *access;
	isl_pw_multi_aff *pma;
	int i;
	unsigned n_index;
	struct gpu_array_bound *bounds = NULL;

	if (isl_map_has_tuple_name(stmt_access->access, isl_dim_out)) {
		int i;
		const char *name;
		struct gpu_array_ref_group *group;
		isl_printer *p;

		name = isl_map_get_tuple_name(stmt_access->access, isl_dim_out);

		for (i = 0; i < gen->prog->n_array; ++i) {
			if (strcmp(name, gen->prog->array[i].name))
				continue;
			kernel_access->array = &gen->prog->array[i];
			kernel_access->local_array = &gen->kernel->array[i];
		}
		assert(kernel_access->array);
		group = kernel_access->array->groups[stmt_access->group];
		p = isl_printer_to_str(gen->ctx);
		p = print_array_name(p, group);
		kernel_access->local_name = isl_printer_get_str(p);
		isl_printer_free(p);
		bounds = group->private_bound;
		kernel_access->type = ppcg_access_private;
		if (!bounds) {
			bounds = group->shared_bound;
			kernel_access->type = ppcg_access_shared;
		}
	}
	if (!bounds)
		kernel_access->type = ppcg_access_global;

	n_index = isl_map_dim(stmt_access->access, isl_dim_out);
	kernel_access->index = isl_ast_expr_list_alloc(gen->ctx, n_index);

	if (n_index == 0)
		return;

	access = isl_map_copy(stmt_access->access);
	access = isl_map_apply_range(isl_map_copy(stmt_it), access);
	pma = isl_pw_multi_aff_from_map(access);
	pma = isl_pw_multi_aff_coalesce(pma);

	for (i = 0; i < n_index; ++i) {
		isl_set *domain;
		isl_pw_aff *index;
		isl_ast_expr *expr;

		index = isl_pw_multi_aff_get_pw_aff(pma, i);

		if (!kernel_access->array) {
		} else if (!bounds) {
			domain = isl_map_domain(isl_map_copy(stmt_it));
			index = isl_pw_aff_coalesce(index);
			index = isl_pw_aff_gist(index, domain);
		} else {
			domain = isl_map_domain(isl_map_copy(stmt_it));
			index = shift_index(index, kernel_access->array,
				&bounds[i], domain, isl_map_copy(sched2shared));
		}

		expr = isl_ast_build_expr_from_pw_aff(build, index);

		kernel_access->index = isl_ast_expr_list_add(
			kernel_access->index, expr);
	}

	isl_pw_multi_aff_free(pma);
}

/* This function is called for each instance of a user statement
 * in the kernel.
 *
 * We attach a struct ppcg_kernel_stmt to the "node", containing
 * local information about the accesses.
 * This information is computed from stmt_it, which expresses the domain
 * elements in terms of the generated loops, and sched2shared,
 * which expresses the first shared_len dimensions of the schedule
 * computed by PPCG in terms of the generated loops.
 */
static __isl_give isl_ast_node *at_each_domain(__isl_take isl_ast_node *node,
	__isl_keep isl_ast_build *build, void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	struct ppcg_kernel_stmt *stmt;
	isl_id *id;
	isl_map *stmt_it, *sched2shared;
	isl_ast_expr *expr, *arg;
	isl_union_map *schedule;
	int i, n;
	struct gpu_stmt_access *access;

	stmt = isl_calloc_type(gen->ctx, struct ppcg_kernel_stmt);
	if (!stmt)
		return isl_ast_node_free(node);

	expr = isl_ast_node_user_get_expr(node);
	arg = isl_ast_expr_get_op_arg(expr, 0);
	id = isl_ast_expr_get_id(arg);

	schedule = isl_ast_build_get_schedule(build);
	stmt_it = isl_map_reverse(isl_map_from_union_map(schedule));
	sched2shared = compute_sched_to_shared(gen, isl_map_copy(stmt_it));

	stmt->type = ppcg_kernel_domain;
	stmt->u.d.stmt = find_stmt(gen->prog, id);
	if (!stmt->u.d.stmt)
		goto error;

	n = 0;
	for (access = stmt->u.d.stmt->accesses; access; access = access->next)
		++n;

	stmt->u.d.access = isl_calloc_array(gen->ctx,
						struct ppcg_kernel_access, n);
	if (!stmt->u.d.access)
		goto error;

	stmt->u.d.n_access = n;

	access = stmt->u.d.stmt->accesses;
	for (i = 0; i < n; ++i, access = access->next) {
		compute_index_expression(gen, &stmt->u.d.access[i], access,
					    stmt_it, sched2shared, build);
	}

	isl_id_free(id);
	isl_map_free(stmt_it);
	isl_map_free(sched2shared);
	isl_ast_expr_free(arg);
	isl_ast_expr_free(expr);

	id = isl_id_alloc(gen->ctx, NULL, stmt);
	id = isl_id_set_free_user(id, &ppcg_kernel_stmt_free);
	return isl_ast_node_set_annotation(node, id);
error:
	isl_id_free(id);
	isl_map_free(stmt_it);
	ppcg_kernel_stmt_free(stmt);
	isl_map_free(sched2shared);
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
	set = add_bounded_parameters(set, gen->n_block, gen->block_dim, "t");
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

/* Add parameters corresponding to the dimensions in the schedule
 * space of "context" and equate them to the dimensions in the range
 * of "map".
 */
static __isl_give isl_map *parametrize_iterators(__isl_take isl_map *map,
	__isl_keep isl_ast_build *build)
{
	int i, n, n_param;
	isl_space *space;

	space = isl_ast_build_get_schedule_space(build);
	n = isl_map_dim(map, isl_dim_out);
	n_param = isl_map_dim(map, isl_dim_param);
	map = isl_map_add_dims(map, isl_dim_param, n);
	for (i = 0; i < n; ++i) {
		isl_id *id;

		id = isl_space_get_dim_id(space, isl_dim_set, i);
		map = isl_map_set_dim_id(map, isl_dim_param, n_param + i, id);
		map = isl_map_equate(map, isl_dim_param, n_param + i,
					isl_dim_out, i);
	}

	isl_space_free(space);

	return map;
}

/* This function is called for each leaf in the AST of the code
 * for copying to or from shared/private memory.
 * The statement name is {read,write}_{shared,private}_<array>.
 *
 * The schedule is of the form
 *
 *	[A -> T] -> L
 *
 * where A refers to a piece of an array and T to the corresponding
 * shifted tile.  We first turn the iterators in L into parameters
 * and then store A in stmt->index and T in stmt->local_index,
 * where stmt represents the copy statement.
 */
static __isl_give isl_ast_node *create_copy_leaf(
	__isl_take isl_ast_build *build, void *user)
{
	struct gpu_gen *gen = (struct gpu_gen *) user;
	struct ppcg_kernel_stmt *stmt;
	isl_id *id;
	isl_ast_expr *expr;
	isl_ast_node *node;
	isl_space *space;
	isl_map *access;
	isl_set *local_access;
	const char *name;
	int array_index;

	stmt = isl_calloc_type(gen->ctx, struct ppcg_kernel_stmt);
	if (!stmt)
		return isl_ast_build_free(build);

	access = isl_map_from_union_map(isl_ast_build_get_schedule(build));
	name = isl_map_get_tuple_name(access, isl_dim_in);
	stmt->u.c.read = !strncmp(name, "read", 4);
	access = parametrize_iterators(access, build);
	access = isl_set_unwrap(isl_map_domain(access));

	local_access = isl_map_range(isl_map_copy(access));

	stmt->u.c.domain = isl_map_params(isl_map_copy(access));
	stmt->u.c.index = isl_pw_multi_aff_from_set(isl_map_domain(access));
	stmt->u.c.local_index = isl_pw_multi_aff_from_set(local_access);
	stmt->u.c.array = gen->copy_group->array;
	array_index = stmt->u.c.array - gen->prog->array;
	stmt->u.c.local_array = &gen->kernel->array[array_index];
	stmt->type = ppcg_kernel_copy;

	space = isl_ast_build_get_schedule_space(build);
	space = isl_space_from_domain(space);
	space = isl_space_set_tuple_name(space, isl_dim_out, name);
	expr = isl_ast_build_call_from_pw_multi_aff(build,
		    isl_pw_multi_aff_from_multi_aff(isl_multi_aff_zero(space)));
	node = isl_ast_node_alloc_user(expr);
	isl_ast_build_free(build);

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
 * indicating where the copying the array elements that need to be copied,
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
	const char *array_name;
	const char *mem = private ? "private" : "shared";
	char *name;
	isl_space *space;
	isl_ast_node *tree;
	isl_map *schedule, *shift, *map;
	isl_set *set;
	isl_id_list *iterators;
	int n;

	shift = isl_set_unwrap(isl_map_domain(isl_map_copy(sched)));
	array_name = isl_map_get_tuple_name(shift, isl_dim_out);
	shift = shift_access(shift, group);

	schedule = isl_map_copy(shift);
	if (!private)
		schedule = tile_access_schedule(gen, schedule);

	n = isl_map_dim(schedule, isl_dim_out);
	set = isl_set_universe(isl_ast_build_get_schedule_space(build));
	set = add_bounded_parameters(set, gen->n_block, gen->block_dim, "t");

	schedule = isl_map_range_product(sched, schedule);

	assert(array_name);
	name = isl_alloc_array(gen->ctx, char,
		strlen(type) + sizeof("_private_") + strlen(array_name) + 20);
	if (group->array->n_group > 1)
		sprintf(name, "%s_%s_%s_%d", type, mem, array_name, group->nr);
	else
		sprintf(name, "%s_%s_%s", type, mem, array_name);
	shift = isl_map_set_tuple_name(shift,
					isl_dim_out, name + strlen(type) + 1);

	space = isl_space_domain(isl_map_get_space(shift));
	map = isl_map_range_map(isl_map_universe(isl_space_unwrap(space)));
	map = isl_map_range_product(map, shift);

	schedule = isl_map_apply_domain(schedule, map);

	schedule = isl_map_set_tuple_name(schedule, isl_dim_in, name);
	free(name);

	build = isl_ast_build_restrict(build, set);

	gen->copy_group = group;
	gen->copy_bound = group->shared_bound;

	if (private) {
		space = isl_space_range(isl_map_get_space(schedule));
		space = isl_space_range(isl_space_unwrap(space));
		build = set_unroll(build, space, 0);
	}
	iterators = generate_names(gen->ctx, n, "c");
	build = isl_ast_build_set_iterators(build, iterators);
	build = isl_ast_build_set_create_leaf(build, &create_copy_leaf, gen);
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

	if (read && group->array->n_index > 0) {
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

	if (group->private_bound)
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
 * and combine it with shared_sched into
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

	space = isl_space_copy(group->array->dim);
	space = isl_space_from_range(space);
	space = isl_space_add_dims(space, isl_dim_in, gen->shared_len);
	map = isl_map_domain_map(isl_map_universe(space));

	space = isl_union_map_get_space(schedule);
	pos = group->last_shared + 1 - gen->tile_first;
	if (read)
		val = -2 - k;
	else if (group->private_bound)
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
		if (!group->private_bound)
			res = add_sync_schedule(gen, res, schedule,
						shared_sched, n, -1);
	} else {
		if (pos == 0)
			return res;
		if (pos == n && group->private_bound)
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
			if (!group->private_bound && !group->shared_bound)
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
 * the size of shared memory and then construct the body of host code
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
				     isl_union_map_copy(gen->prog->write));
	access = isl_union_map_apply_domain(access,
					    isl_union_map_copy(local_sched));

	gen->tiled_sched = tile_schedule(gen, local_sched);
	gen->tiled_sched = parametrize_tiled_schedule(gen, gen->tiled_sched);
	gen->tiled_sched = scale_tile_loops(gen, gen->tiled_sched);

	kernel = gen->kernel = isl_calloc_type(gen->ctx, struct ppcg_kernel);
	if (!kernel)
		goto error;

	kernel->id = gen->kernel_id++;
	kernel->n_block = gen->n_block;
	for (i = 0; i < gen->n_block; ++i)
		kernel->block_dim[i] = gen->block_dim[i];
	kernel->n_grid = gen->n_grid;
	for (i = 0; i < gen->n_grid; ++i)
		kernel->grid_dim[i] = gen->grid_dim[i];
	kernel->context = isl_union_map_params(isl_union_map_copy(schedule));
	kernel->grid_size = extract_grid_size(gen, kernel);
	kernel->arrays = isl_union_map_range(access);
	kernel->space = isl_ast_build_get_schedule_space(build);

	gen->local_sched = isl_union_map_copy(gen->tiled_sched);

	gen->local_sched = thread_tile_schedule(gen, gen->local_sched);
	gen->local_sched = scale_thread_tile_loops(gen, gen->local_sched);

	gen->private_access = NULL;
	compute_shared_sched(gen);
	gen->privatization = compute_privatization(gen);
	group_references(gen);
	compute_private_size(gen);
	check_shared_memory_bound(gen);
	host_domain = isl_set_from_union_set(isl_union_map_range(
						isl_union_map_copy(schedule)));
	localize_bounds(gen, kernel, host_domain);

	gen->local_sched = interchange_for_unroll(gen, gen->local_sched);

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

__isl_give isl_set *add_context_from_str(__isl_take isl_set *set,
	const char *str)
{
	isl_ctx *ctx;
	isl_set *context;

	if (!str)
		return set;

	ctx = isl_set_get_ctx(set);
	context = isl_set_read_from_str(ctx, str);
	context = isl_set_align_params(context, isl_set_get_space(set));
	set = isl_set_intersect(set, context);

	return set;
}

__isl_give isl_union_map *extract_sizes_from_str(isl_ctx *ctx, const char *str)
{
	if (!str)
		return NULL;
	return isl_union_map_read_from_str(ctx, str);
}

/* Return the union of all iteration domains of the prog->stmts[i].
 */
static __isl_give isl_union_set *extract_domain(struct gpu_prog *prog)
{
	int i;
	isl_union_set *domain;

	domain = isl_union_set_empty(isl_set_get_space(prog->context));
	for (i = 0; i < prog->n_stmts; ++i) {
		isl_set *domain_i;

		domain_i = isl_set_copy(prog->stmts[i].domain);
		domain = isl_union_set_union(domain,
					     isl_union_set_from_set(domain_i));
	}

	return domain;
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
		if (!isl_band_member_is_zero_distance(band, n_parallel))
			break;

	info->n_parallel = n_parallel;
	if (n_parallel) {
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
 * We first compute dependences and then use those to compute
 * a schedule that has a parallel loop in each tilable band.
 * Finally, we select the outermost tilable band.
 */
static void compute_schedule(struct gpu_gen *gen,
	__isl_take isl_union_map *sched)
{
	isl_union_set *domain;
	isl_union_map *empty;
	isl_union_map *dep_raw, *dep2, *dep3, *dep;
	isl_union_map *uninitialized;
	isl_schedule *schedule;

	empty = isl_union_map_empty(isl_union_map_get_space(sched));

        isl_union_map_compute_flow(isl_union_map_copy(gen->prog->read),
                            isl_union_map_copy(gen->prog->write), empty,
                            isl_union_map_copy(sched),
                            &dep_raw, NULL, &uninitialized, NULL);
        isl_union_map_compute_flow(isl_union_map_copy(gen->prog->write),
                            isl_union_map_copy(gen->prog->write),
                            isl_union_map_copy(gen->prog->read),
                            isl_union_map_copy(sched),
                            &dep2, &dep3, NULL, NULL);
	isl_union_map_free(sched);

	gen->prog->copy_in = isl_union_map_range(uninitialized);

	dep = isl_union_map_union(dep2, dep3);
	dep = isl_union_map_union(dep, dep_raw);
	dep = isl_union_map_coalesce(dep);

	domain = extract_domain(gen->prog);
	schedule = isl_union_set_compute_schedule(isl_union_set_copy(domain),
				isl_union_map_copy(dep), dep);

	sched = select_outer_tilable_band(gen, schedule);

	isl_union_map_foreach_map(sched, &set_untiled_len, &gen->untiled_len);
	sched = isl_union_map_intersect_domain(sched, domain);
	gen->sched = sched;

	isl_schedule_free(schedule);
}

static struct gpu_stmt_access **expr_extract_access(struct pet_expr *expr,
	struct gpu_stmt_access **next_access)
{
	struct gpu_stmt_access *access;
	isl_ctx *ctx = isl_map_get_ctx(expr->acc.access);

	access = isl_alloc_type(ctx, struct gpu_stmt_access);
	assert(access);
	access->next = NULL;
	access->read = expr->acc.read;
	access->write = expr->acc.write;
	access->access = isl_map_copy(expr->acc.access);

	*next_access = access;
	next_access = &(*next_access)->next;
	return next_access;
}

static struct gpu_stmt_access **expr_extract_accesses(struct pet_expr *expr,
	struct gpu_stmt_access **next_access)
{
	int i;

	for (i = 0; i < expr->n_arg; ++i)
		next_access = expr_extract_accesses(expr->args[i],
							next_access);

	if (expr->type == pet_expr_access)
		next_access = expr_extract_access(expr, next_access);

	return next_access;
}

static void pet_stmt_extract_accesses(struct gpu_stmt *stmt)
{
	struct gpu_stmt_access **next_access = &stmt->accesses;

	stmt->accesses = NULL;
	expr_extract_accesses(stmt->body, next_access);
}

/* Return an array of gpu_stmt representing the statements in "scop".
 */
static struct gpu_stmt *extract_stmts(isl_ctx *ctx, struct pet_scop *scop,
	__isl_keep isl_set *context)
{
	int i;
	struct gpu_stmt *stmts;

	stmts = isl_calloc_array(ctx, struct gpu_stmt, scop->n_stmt);
	assert(stmts);

	for (i = 0; i < scop->n_stmt; ++i) {
		struct gpu_stmt *s = &stmts[i];

		s->domain = isl_set_copy(scop->stmts[i]->domain);
		s->domain = isl_set_intersect_params(s->domain,
							isl_set_copy(context));
		s->body = scop->stmts[i]->body;
		pet_stmt_extract_accesses(s);
	}

	return stmts;
}

/* Replace the scop in the "input" file by equivalent code
 * that uses the GPU.  "scop" is assumed to correspond to this scop.
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
__isl_give isl_ast_node *generate_gpu(isl_ctx *ctx, struct gpu_prog *prog,
	struct ppcg_options *options)
{
	isl_union_map *sched;
	struct gpu_gen gen;
	isl_ast_node *tree;

	if (!prog)
		return NULL;

	gen.ctx = ctx;
	gen.prog = prog;
	gen.sizes = extract_sizes_from_str(ctx, options->sizes);
	gen.options = options;

	sched = pet_scop_collect_schedule(prog->scop);

	compute_schedule(&gen, sched);

	gen.kernel_id = 0;
	tree = generate_host_code(&gen);

	clear_gpu_gen(&gen);

	return tree;
}

struct gpu_prog *gpu_prog_alloc(isl_ctx *ctx, struct pet_scop *scop)
{
	struct gpu_prog *prog;

	if (!scop)
		return NULL;

	scop = pet_scop_align_params(scop);

	prog = isl_calloc_type(ctx, struct gpu_prog);
	assert(prog);

	prog->ctx = ctx;
	prog->scop = scop;
	prog->context = isl_set_copy(scop->context);
	prog->n_stmts = scop->n_stmt;
	prog->stmts = extract_stmts(ctx, scop, prog->context);
	prog->read = pet_scop_collect_reads(scop);
	prog->write = pet_scop_collect_writes(scop);

	collect_array_info(prog);

	return prog;
}

void gpu_prog_free(struct gpu_prog *prog)
{
	if (!prog)
		return;
	free_array_info(prog);
	free_stmts(prog->stmts, prog->n_stmts);
	isl_union_set_free(prog->copy_in);
	isl_union_map_free(prog->read);
	isl_union_map_free(prog->write);
	isl_set_free(prog->context);
	free(prog);
}
