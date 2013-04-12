#ifndef _GPU_H
#define _GPU_H

#include <isl/ast.h>

#include "ppcg.h"
#include "ppcg_options.h"

struct gpu_array_info {
	isl_space *dim;
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

	/* The reference groups associated to this array. */
	int n_group;
	struct gpu_array_ref_group **groups;

	/* For scalars, is this scalar read-only within the entire program? */
	int read_only;

	/* Is the array local to the scop? */
	int local;
};

/* For each index i, array->bound[i] specialized to the current kernel. */
struct gpu_local_array_info {
	isl_pw_aff_list *bound;
};

struct gpu_prog {
	isl_ctx *ctx;

	struct ppcg_scop *scop;

	/* Set of parameter values */
	isl_set *context;

	/* All read accesses in the entire program */
	isl_union_map *read;

	/* All write accesses in the entire program */
	isl_union_map *write;

	/* Set of array elements that need to be copied in. */
	isl_union_set *copy_in;
	/* Set of array elements that need to be copied out. */
	isl_union_set *copy_out;

	/* Array of statements */
	int n_stmts;
	struct gpu_stmt *stmts;

	int n_array;
	struct gpu_array_info *array;
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

/* Instance specific information about an access inside a kernel statement.
 *
 * type indicates whether it is a global, shared or private access
 * array is the original array information and may be NULL in case
 *	of an affine expression
 * local_array is a pointer to the appropriate element in the "array"
 *	array of the ppcg_kernel to which this access belongs.  It is
 *	NULL whenever array is NULL.
 * local_name is the name of the array or its local copy
 * index is the sequence of local index expressions
 */
struct ppcg_kernel_access {
	enum ppcg_kernel_access_type type;
	struct gpu_array_info *array;
	struct gpu_local_array_info *local_array;
	char *local_name;
	isl_ast_expr_list *index;
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
 * index expresses the array element that needs to be copied as a "call"
 * to the array
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

			int n_access;
			struct ppcg_kernel_access *access;
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
 * the first n_block elements of block_dim represent the effective size
 * of the block.
 *
 * grid_size reflects the effective grid size.
 *
 * context is a parametric set containing the values of the parameters
 * for which this kernel may be run.
 *
 * arrays is the set of accessed array elements.
 *
 * space is the schedule space of the AST context.  That is, it represents
 * the loops of the generated host code containing the kernel launch.
 *
 * n_array is the total number of arrays in the input program and also
 * the number of element in the array array.
 * array contains information about each array that is local
 * to the current kernel.  If an array is not ussed in a kernel,
 * then the corresponding entry does not contain any information.
 */
struct ppcg_kernel {
	int id;

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

struct gpu_prog *gpu_prog_alloc(isl_ctx *ctx, struct ppcg_scop *scop);
void gpu_prog_free(struct gpu_prog *prog);

__isl_give isl_ast_node *generate_gpu(isl_ctx *ctx, struct gpu_prog *prog,
	struct ppcg_options *options);

#endif
