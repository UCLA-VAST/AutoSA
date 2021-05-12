#ifndef _AUTOSA_CODEGEN_H
#define _AUTOSA_CODEGEN_H

#include "print.h"
#include "util.h"

#include "autosa_common.h"

void generate_hw_modules(__isl_take isl_schedule *schedule,
                         struct autosa_gen *gen, struct autosa_kernel *kernel);

__isl_give isl_schedule_node *sa_add_to_from_device(
    __isl_take isl_schedule_node *node, __isl_take isl_union_set *domain,
    __isl_take isl_union_map *prefix, struct autosa_prog *prog);
__isl_give isl_schedule_node *sa_add_init_clear_device(
    __isl_take isl_schedule_node *node, struct autosa_kernel *kernel);
__isl_give isl_schedule_node *sa_add_drain_merge(
    __isl_take isl_schedule_node *node, struct autosa_gen *gen);

__isl_give isl_ast_node *sa_generate_code(struct autosa_gen *gen,
                                          __isl_take isl_schedule *schedule);
isl_stat sa_filter_buffer_io_module_generate_code(struct autosa_gen *gen,
                                                  struct autosa_hw_module *module);
isl_stat sa_module_generate_code(struct autosa_gen *gen,
                                 struct autosa_hw_module *module);
isl_stat sa_top_module_generate_code(struct autosa_gen *gen);
isl_stat sa_drain_merge_generate_code(struct autosa_gen *gen,
                                      struct autosa_drain_merge_func *func);
isl_stat sa_host_serialize_generate_code(struct autosa_gen *gen,
                                         struct autosa_hw_module *module);                                      

int autosa_array_requires_device_allocation(struct autosa_array_info *array);

__isl_give isl_schedule_node *insert_io_group_domain(
  __isl_take isl_schedule_node *node, 
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  struct autosa_gen *gen,
  int read);

void print_code(struct autosa_gen *gen, __isl_take isl_schedule *schedule, const char *output_f);
void dump_intermediate_code(
  struct autosa_gen *gen, __isl_take isl_schedule *schedule, const char *stage);

#endif