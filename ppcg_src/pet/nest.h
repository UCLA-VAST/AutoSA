#ifndef PET_NEST_H
#define PET_NEST_H

#include <isl/aff.h>
#include <isl/id.h>
#include <isl/space.h>
#include <isl/set.h>
#include <isl/map.h>

#include "pet.h"

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give isl_id *pet_nested_pet_expr(__isl_take pet_expr *expr);
__isl_give pet_expr *pet_nested_extract_expr(__isl_keep isl_id *id);

int pet_nested_in_id(__isl_keep isl_id *id);
int pet_nested_in_map(__isl_keep isl_map *map, int pos);
int pet_nested_any_in_space(__isl_keep isl_space *space);
int pet_nested_any_in_pw_aff(__isl_keep isl_pw_aff *pa);
int pet_nested_n_in_space(__isl_keep isl_space *space);
int pet_nested_n_in_set(__isl_keep isl_set *set);
int pet_nested_n_in_map(__isl_keep isl_map *map);

__isl_give isl_space *pet_nested_remove_from_space(__isl_take isl_space *space);
__isl_give isl_set *pet_nested_remove_from_set(__isl_take isl_set *set);

struct pet_stmt *pet_stmt_remove_nested_parameters(struct pet_stmt *stmt);

int pet_extract_nested_from_space(__isl_keep isl_space *space,
	int n_arg, __isl_give pet_expr **args, int *param2pos);

__isl_give pet_expr *pet_expr_resolve_nested(__isl_take pet_expr *expr,
	__isl_keep isl_space *domain);
__isl_give pet_tree *pet_tree_resolve_nested(__isl_take pet_tree *tree,
	__isl_keep isl_space *space);
struct pet_scop *pet_scop_resolve_nested(struct pet_scop *scop);

#if defined(__cplusplus)
}
#endif

#endif
