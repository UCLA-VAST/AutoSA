#ifndef _CUDA_H
#define _CUDA_H

#include <pet.h>
#include "cuda_common.h"
#include "gpucode.h"
#include "ppcg_options.h"

struct cuda_gen {
	struct cuda_info cuda;
	struct gpucode_info code;
	struct gpucode_info kernel_code;
	struct gpucode_info stmt_code;

	isl_ctx *ctx;
	struct ppcg_options *options;
	CloogState *state;

	struct pet_scop *scop;

	/* Set of parameter values */
	isl_set *context;

	/* Uninitialized data elements (or an overapproximation) */
	isl_union_set *copy_in;

	/* All read accesses in the entire program */
	isl_union_map *read;

	/* All write accesses in the entire program */
	isl_union_map *write;

	/* Array of statements */
	int n_stmts;
	struct cuda_stmt *stmts;

	int n_array;
	struct cuda_array_info *array;

	/* Identifier of current kernel. */
	int kernel_id;

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
	/* Domain of the current statement (within print_statement). */
	isl_set *stmt_domain;

	/* Position of first parameter corresponding to shared tile loop
	 * in shared_sched.
	 */
	unsigned first_shared;
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

	/* The schedule for the current private access
	 * (within print_private_access).
	 */
	isl_map *private_sched;
	/* The array reference group corresponding to private_sched. */
	struct cuda_array_ref_group *private_group;

	/* First loop to unroll (or -1 if none). */
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

__isl_give isl_set *add_context_from_str(__isl_take isl_set *set,
	const char *str);
void collect_array_info(struct cuda_gen *gen);
void print_host_code(struct cuda_gen *gen);
void clear_cuda_gen(struct cuda_gen *gen);

int cuda_pet(isl_ctx *ctx, struct pet_scop *scop, struct ppcg_options *options,
	const char *input);

#endif
