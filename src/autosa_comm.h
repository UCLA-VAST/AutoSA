#ifndef _AUTOSA_COMM_H
#define _AUTOSA_COMM_H

#include "autosa_common.h"

#if defined(__cplusplus)
extern "C" {
#endif   

isl_stat sa_io_construct_optimize(struct autosa_kernel *kernel, struct autosa_gen *gen);
enum autosa_group_access_type autosa_array_ref_group_type(
	struct autosa_array_ref_group *group);
enum autosa_group_access_type autosa_cpu_array_ref_group_type(
	struct autosa_array_ref_group *group);	
struct autosa_array_tile *autosa_array_ref_group_tile(
	struct autosa_array_ref_group *group);  
__isl_give isl_printer *autosa_array_ref_group_print_name(
	struct autosa_array_ref_group *group, __isl_take isl_printer *p);
__isl_give isl_union_map *autosa_io_group_ref_access_relation(
  struct autosa_array_ref_group *group,
  struct autosa_stmt_access *ref,
  int read, int write);
__isl_give isl_union_map *autosa_array_ref_group_access_relation(
	struct autosa_array_ref_group *group, int read, int write);	
__isl_give isl_union_map *autosa_io_group_access_relation(
  struct autosa_array_ref_group *group, 
  struct autosa_kernel *kernel,
  int read, int write);
__isl_give isl_union_map *autosa_drain_group_ref_access_relation(
  struct autosa_array_ref_group *group,
  struct autosa_stmt_access *ref,
  int read, int write, __isl_keep isl_union_set *domain);	
__isl_give isl_union_map *group_tagged_access_relation(
	struct autosa_array_ref_group *group);
__isl_give isl_union_map *remove_local_accesses_flow(
	struct autosa_prog *prog, __isl_take isl_union_map *tagged,
	__isl_take isl_union_map *access, __isl_take isl_union_map *sched,
	int read);	
__isl_give isl_union_map *wrapped_reference_to_access(
	__isl_take isl_union_set *ref, __isl_take isl_union_map *tagged);	
__isl_give isl_union_map *remove_local_accesses(
	struct autosa_prog *prog, __isl_take isl_union_map *tagged,
	__isl_take isl_union_map *access, __isl_take isl_union_map *sched,
	int read);	
__isl_give isl_union_map *remove_local_accesses_group_flow(
	struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
	__isl_take isl_union_map *access, __isl_keep isl_union_map *prefix,
	int read);
__isl_give isl_union_map *remove_local_accesses_group(
	struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
	__isl_take isl_union_map *access, __isl_keep isl_union_map *prefix,
	int read);	
__isl_give isl_union_map *io_comm_access_ref(
  struct autosa_kernel *kernel, __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group, 
  struct autosa_stmt_access *ref,
  int read);	
__isl_give isl_union_map *io_comm_access(
  struct autosa_kernel *kernel, __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group, int read);	
void free_group_pair(void *user);
struct autosa_array_tile *create_register_tiling(
  isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_stmt_access *ref);
__isl_give isl_map *group_tile(struct autosa_array_ref_group *group);	
__isl_give isl_map *group_tile_buffer(struct autosa_array_ref_group *group,
  struct autosa_array_tile *tile);
int get_io_group_n_lane(struct autosa_hw_module *module, 
  struct autosa_pe_dummy_module *dummy_module,
  struct autosa_array_ref_group *group);
__isl_give isl_multi_aff *autosa_array_ref_group_recompute_tiling(
  struct autosa_array_tile *tile,
  struct autosa_array_ref_group *group,
  int depth);  
isl_bool is_io_module_valid(
  __isl_keep isl_schedule_node *node,  
  struct autosa_kernel *kernel, 
  struct autosa_array_ref_group *group, int read);  
void print_io_grouping_info(FILE *fp, struct autosa_kernel *kernel);

#if defined(__cplusplus)
}
#endif 

#endif