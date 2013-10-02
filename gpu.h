#ifndef _GPU_H
#define _GPU_H

#include <isl/ast.h>
#include <isl/id_to_ast_expr.h>

#include "ppcg.h"
#include "ppcg_options.h"

/* Represents an outer array possibly accessed by a gpu_prog.
 */
struct gpu_array_info {
	/* The array data space. */
	isl_space *space;
	/* Element type. */
	char *type;
	/* Element size. */
	int size;
	/* Name of the array. */
	char *name;
	/* Extent of the array that needs to be copied. */
	isl_set *extent;
	/* Number of indices. */
	unsigned n_index;
	/* For each index, a bound on "extent" in that direction. */
	isl_pw_aff **bound;

	/* All references to this array; point to elements of a linked list. */
	int n_ref;
	struct gpu_stmt_access **refs;

	/* Is this array accessed at all by the program? */
	int accessed;

	/* Is this a scalar that is read-only within the entire program? */
	int read_only_scalar;

	/* Are the elements of the array structures? */
	int has_compound_element;

	/* Is the array local to the scop? */
	int local;

	/* Should the array be linearized? */
	int linearize;

	/* Order dependences on this array.
	 * Only used if live_range_reordering option is set.
	 * It is set to NULL otherwise.
	 */
	isl_union_map *dep_order;
	/* Should the array (scalar) be forcibly mapped to a register? */
	int force_private;
};

/* Represents an outer array accessed by a ppcg_kernel, localized
 * to the context of this kernel.
 *
 * "array" points to the corresponding array in the gpu_prog.
 * The "n_group" "groups" are the reference groups associated to the array.
 * If the outer array represented by the gpu_local_array_info
 * contains structures, then the references are not
 * collected and the reference groups are not computed.
 * For each index i with 0 <= i < n_index,
 * bound[i] is equal to array->bound[i] specialized to the current kernel.
 */
struct gpu_local_array_info {
	struct gpu_array_info *array;

	int n_group;
	struct gpu_array_ref_group **groups;

	unsigned n_index;
	isl_pw_aff_list *bound;
};

__isl_give isl_ast_expr *gpu_local_array_info_linearize_index(
	struct gpu_local_array_info *array, __isl_take isl_ast_expr *expr);

/* A sequence of "n" names of types.
 */
struct gpu_types {
	int n;
	char **name;
};

/* "read" and "write" contain the original access relations, possibly
 * involving member accesses.
 *
 * The elements of "array", as well as the ranges of "copy_in" and "copy_out"
 * only refer to the outer arrays of any possible member accesses.
 */
struct gpu_prog {
	isl_ctx *ctx;

	struct ppcg_scop *scop;

	/* Set of parameter values */
	isl_set *context;

	/* All potential read accesses in the entire program */
	isl_union_map *read;

	/* All potential write accesses in the entire program */
	isl_union_map *may_write;
	/* All definite write accesses in the entire program */
	isl_union_map *must_write;
	/* All tagged definite kills in the entire program */
	isl_union_map *tagged_must_kill;

	/* The set of inner array elements that may be preserved. */
	isl_union_set *may_persist;

	/* Set of outer array elements that need to be copied in. */
	isl_union_set *copy_in;
	/* Set of outer array elements that need to be copied out. */
	isl_union_set *copy_out;

	/* A mapping from all innermost arrays to their outer arrays. */
	isl_union_map *to_outer;
	/* A mapping from the outer arrays to all corresponding inner arrays. */
	isl_union_map *to_inner;
	/* A mapping from all intermediate arrays to their outer arrays,
	 * including an identity mapping from the anoymous 1D space to itself.
	 */
	isl_union_map *any_to_outer;

	/* Order dependences on non-scalars. */
	isl_union_map *array_order;

	/* Array of statements */
	int n_stmts;
	struct gpu_stmt *stmts;

	int n_array;
	struct gpu_array_info *array;
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

enum ppcg_kernel_access_type {
	ppcg_access_global,
	ppcg_access_shared,
	ppcg_access_private
};

enum ppcg_kernel_stmt_type {
	ppcg_kernel_copy,
	ppcg_kernel_domain,
	ppcg_kernel_sync
};

/* Representation of special statements, in particular copy statements
 * and __syncthreads statements, inside a kernel.
 *
 * type represents the kind of statement
 *
 *
 * for ppcg_kernel_copy statements we have
 *
 * read is set if the statement should copy data from global memory
 * to shared memory or registers.
 *
 * index expresses an access to the array element that needs to be copied
 * local_index expresses the corresponding element in the tile
 *
 * array refers to the original array being copied
 * local_array is a pointer to the appropriate element in the "array"
 *	array of the ppcg_kernel to which this copy access belongs
 *
 *
 * for ppcg_kernel_domain statements we have
 *
 * stmt is the corresponding input statement
 *
 * n_access is the number of accesses in stmt
 * access is an array of local information about the accesses
 */
struct ppcg_kernel_stmt {
	enum ppcg_kernel_stmt_type type;

	union {
		struct {
			int read;
			isl_ast_expr *index;
			isl_ast_expr *local_index;
			struct gpu_array_info *array;
			struct gpu_local_array_info *local_array;
		} c;
		struct {
			struct gpu_stmt *stmt;
			isl_id_to_ast_expr *ref2expr;
		} d;
	} u;
};

/* Representation of a local variable in a kernel.
 */
struct ppcg_kernel_var {
	struct gpu_array_info *array;
	enum ppcg_kernel_access_type type;
	char *name;
	isl_vec *size;
};

/* Representation of a kernel.
 *
 * id is the sequence number of the kernel.
 *
 * block_ids contains the list of block identifiers for this kernel.
 * thread_ids contains the list of thread identifiers for this kernel.
 *
 * the first n_block elements of block_dim represent the effective size
 * of the block.
 *
 * grid_size reflects the effective grid size.
 *
 * context is a parametric set containing the values of the parameters
 * for which this kernel may be run.
 *
 * arrays is the set of possibly accessed outer array elements.
 *
 * space is the schedule space of the AST context.  That is, it represents
 * the loops of the generated host code containing the kernel launch.
 *
 * n_array is the total number of arrays in the input program and also
 * the number of element in the array array.
 * array contains information about each array that is local
 * to the current kernel.  If an array is not used in a kernel,
 * then the corresponding entry does not contain any information.
 */
struct ppcg_kernel {
	isl_ctx *ctx;
	struct ppcg_options *options;

	int id;

	isl_id_list *block_ids;
	isl_id_list *thread_ids;

	int n_block;
	int block_dim[3];

	isl_multi_pw_aff *grid_size;
	isl_set *context;

	isl_union_set *arrays;

	isl_space *space;

	int n_array;
	struct gpu_local_array_info *array;

	int n_var;
	struct ppcg_kernel_var *var;

	isl_ast_node *tree;
};

int gpu_array_is_scalar(struct gpu_array_info *array);
int gpu_array_is_read_only_scalar(struct gpu_array_info *array);
__isl_give isl_set *gpu_array_positive_size_guard(struct gpu_array_info *array);

struct gpu_prog *gpu_prog_alloc(isl_ctx *ctx, struct ppcg_scop *scop);
void *gpu_prog_free(struct gpu_prog *prog);

int generate_gpu(isl_ctx *ctx, const char *input, FILE *out,
	struct ppcg_options *options,
	__isl_give isl_printer *(*print)(__isl_take isl_printer *p,
		struct gpu_prog *prog, __isl_keep isl_ast_node *tree,
		struct gpu_types *types, void *user), void *user);

#endif
