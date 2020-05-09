#ifndef PET_CONTEXT_H
#define PET_CONTEXT_H

#include <isl/space.h>
#include <isl/set.h>
#include <isl/id_to_pw_aff.h>

#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give isl_set *pet_context_get_domain(__isl_keep pet_context *pc);
__isl_give isl_set *pet_context_get_gist_domain(__isl_keep pet_context *pc);
__isl_give isl_space *pet_context_get_space(__isl_keep pet_context *pc);
unsigned pet_context_dim(__isl_keep pet_context *pc);
__isl_give isl_id_to_pw_aff *pet_context_get_assignments(
	__isl_keep pet_context *pc);
int pet_context_is_assigned(__isl_keep pet_context *pc, __isl_keep isl_id *id);
__isl_give pet_context *pet_context_set_value(__isl_take pet_context *pc,
	__isl_take isl_id *id, isl_pw_aff *value);
__isl_give isl_pw_aff *pet_context_get_value(__isl_keep pet_context *pc,
	__isl_take isl_id *id);
__isl_give pet_context *pet_context_clear_value(__isl_keep pet_context *pc,
	__isl_take isl_id *id);
__isl_give pet_context *pet_context_set_allow_nested(__isl_take pet_context *pc,
	int allow_nested);
int pet_context_allow_nesting(__isl_keep pet_context *pc);

__isl_give pet_context *pet_context_clear_writes_in_expr(
	__isl_take pet_context *pc, __isl_keep pet_expr *expr);
__isl_give pet_context *pet_context_clear_writes_in_tree(
	__isl_take pet_context *pc, __isl_keep pet_tree *tree);
__isl_give pet_context *pet_context_add_parameters(__isl_take pet_context *pc,
	__isl_keep pet_tree *tree,
	__isl_give pet_expr *(*get_array_size)(__isl_keep pet_expr *access,
		void *user), void *user);

__isl_give pet_expr *pet_context_evaluate_expr(__isl_keep pet_context *pc,
	__isl_take pet_expr *expr);
__isl_give pet_tree *pet_context_evaluate_tree(__isl_keep pet_context *pc,
	__isl_take pet_tree *tree);

__isl_give pet_context *pet_context_add_inner_iterator(
	__isl_take pet_context *pc, __isl_take isl_id *id);
__isl_give pet_context *pet_context_add_infinite_loop(
	__isl_take pet_context *pc);
__isl_give pet_context *pet_context_preimage_domain(__isl_take pet_context *pc,
	__isl_keep isl_multi_aff *ma);

__isl_give pet_context *pet_context_intersect_domain(__isl_take pet_context *pc,
	__isl_take isl_set *set);

__isl_give isl_maybe_isl_pw_aff pet_context_get_extracted_affine(
	__isl_keep pet_context *pc, __isl_keep pet_expr *expr);
isl_stat pet_context_set_extracted_affine(__isl_keep pet_context *pc,
	__isl_keep pet_expr *expr, __isl_keep isl_pw_aff *pa);

void pet_context_dump(__isl_keep pet_context *pc);

#if defined(__cplusplus)
}
#endif

#endif
