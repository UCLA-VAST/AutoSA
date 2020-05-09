#ifndef PET_VALUE_BOUNDS_H
#define PET_VALUE_BOUNDS_H

#include <isl/set.h>
#include <isl/union_map.h>

#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give isl_set *pet_value_bounds_apply(__isl_take isl_set *domain,
	unsigned n_arg, __isl_keep pet_expr **args,
	__isl_keep isl_union_map *value_bounds);

#if defined(__cplusplus)
}
#endif

#endif
