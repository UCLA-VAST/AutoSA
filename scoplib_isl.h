#ifndef SCOPLIB_ISL_H
#define SCOPLIB_ISL_H

#include <isl/dim.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_map.h>

#include "scoplib/statement.h"

__isl_give isl_dim *set_dim_names(__isl_take isl_dim *dim,
        enum isl_dim_type type, char **names);
__isl_give isl_set *scoplib_matrix_to_isl_set(scoplib_matrix_p matrix,
        __isl_take isl_dim *dim);
__isl_give isl_set *scoplib_matrix_list_to_isl_set(
        scoplib_matrix_list_p list, __isl_take isl_dim *dim);
__isl_give isl_union_map *scoplib_access_to_isl_union_map(
        scoplib_matrix_p access, __isl_take isl_set *dom, char **arrays);
__isl_give isl_map *scoplib_schedule_to_isl_map(
        scoplib_matrix_p schedule, __isl_take isl_dim *dim);

#endif
