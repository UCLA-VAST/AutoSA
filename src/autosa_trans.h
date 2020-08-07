/* Defines functions for computation management in AutoSA, including:
 * - space-time transformation
 * - array partitionining
 * - latency hiding
 * - SIMD vectorization
 */

#ifndef _AUTOSA_TRANS_H
#define _AUTOSA_TRANS_H

#include <isl/constraint.h>

#include "cpu.h"

#include "autosa_common.h"

/* Internal structure for loop tiling in PE optimization.
 */
struct autosa_pe_opt_tile_data
{
    int n_tiled_loop;
    int n_touched_loop;
    int tile_len;
    int *tile_size;
    struct autosa_kernel *sa;
};

int generate_sa(isl_ctx *ctx, const char *input, FILE *out,
                struct ppcg_options *options,
                __isl_give isl_printer *(*print)(__isl_take isl_printer *p,
                                                 struct autosa_prog *prog, __isl_keep isl_ast_node *tree,
                                                 struct autosa_hw_module **modules, int n_modules,
                                                 struct autosa_hw_top_module *top_module,
                                                 struct autosa_drain_merge_func **drain_merge_funcs, int n_drain_merge_funcs,
                                                 struct autosa_types *types, void *user),
                void *user);
__isl_give isl_schedule *sa_map_to_device(struct autosa_gen *gen,
                                          __isl_take isl_schedule *schedule);
isl_bool sa_legality_check(__isl_keep isl_schedule *schedule, struct ppcg_scop *scop);

/* Space-Time transformation */
struct autosa_kernel **sa_space_time_transform_at_dim_async(
    __isl_keep isl_schedule *schedule, struct ppcg_scop *scop,
    isl_size dim, isl_size *num_sa);
struct autosa_kernel **sa_space_time_transform_at_dim_sync(
    __isl_keep isl_schedule *schedule, struct ppcg_scop *scop,
    isl_size dim, isl_size *num_sa);
struct autosa_kernel **sa_space_time_transform_at_dim(
    __isl_keep isl_schedule *schedule, struct ppcg_scop *scop,
    isl_size dim, isl_size *num_sa);
struct autosa_kernel *sa_candidates_smart_pick(
    struct autosa_kernel **sa_list, __isl_keep isl_size num_sa);
struct autosa_kernel *sa_candidates_manual_pick(
    struct autosa_kernel **sa_list, isl_size num_sa, int sa_id);
struct autosa_kernel **sa_space_time_transform(
    __isl_take isl_schedule *schedule, struct ppcg_scop *scop, isl_size *num_sa);

/* PE Optimization */
isl_stat sa_array_partitioning_optimize(
    struct autosa_kernel *sa, bool en, char *mode, bool L2_en, char *L2_mode);
isl_stat sa_latency_hiding_optimize(
    struct autosa_kernel *sa, bool en, char *mode);
isl_stat sa_simd_vectorization_optimize(
    struct autosa_kernel *sa, char *mode);
isl_stat sa_pe_optimize(
    struct autosa_kernel *sa, bool pass_en[], char *pass_mode[]);

isl_stat sa_loop_init(struct autosa_kernel *sa);
isl_stat sa_space_time_loop_setup(struct autosa_kernel *sa);

void extract_sa_dims_from_node(__isl_keep isl_schedule_node *node, int *sa_dims, int n_sa_dim);

#endif