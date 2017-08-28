#ifndef _CONSECUTIVITY_H
#define _CONSECUTIVITY_H

#include <isl/space_type.h>
#include <isl/set_type.h>
#include <isl/union_map_type.h>
#include <isl/schedule.h>

struct ppcg_consecutive;
typedef struct ppcg_consecutive ppcg_consecutive;

__isl_give ppcg_consecutive *ppcg_consecutive_from_array_list(
	__isl_take isl_space_list *array_list);
__isl_give ppcg_consecutive *ppcg_consecutive_from_extent_list(
	__isl_take isl_set_list *extent_list);
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_reads(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *reads);
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_writes(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *writes);
__isl_null ppcg_consecutive *ppcg_consecutive_free(
	__isl_take ppcg_consecutive *c);
isl_bool ppcg_consecutive_is_empty(__isl_keep ppcg_consecutive *c);

__isl_give isl_schedule_constraints *
ppcg_consecutive_add_consecutivity_constraints(
	__isl_keep ppcg_consecutive *c,
	__isl_take isl_schedule_constraints *sc);

#endif
