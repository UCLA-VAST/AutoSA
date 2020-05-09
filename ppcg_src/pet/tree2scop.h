#ifndef PET_TREE2SCOP_H
#define PET_TREE2SCOP_H

#include "tree.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct pet_scop *pet_scop_from_pet_tree(__isl_take pet_tree *tree, int int_size,
	struct pet_array *(*extract_array)(__isl_keep pet_expr *iterator,
		__isl_keep pet_context *pc, void *user), void *user,
	__isl_keep pet_context *pc);

#if defined(__cplusplus)
}
#endif

#endif
