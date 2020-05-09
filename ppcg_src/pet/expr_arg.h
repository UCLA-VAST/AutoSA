#ifndef PET_EXPR_ARG_H
#define PET_EXPR_ARG_H

#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give pet_expr *pet_expr_remove_duplicate_args(__isl_take pet_expr *expr);
__isl_give pet_expr *pet_expr_insert_arg(__isl_take pet_expr *expr, int pos,
	__isl_take pet_expr *arg);
__isl_give pet_expr *pet_expr_access_project_out_arg(__isl_take pet_expr *expr,
	int dim, int pos);

__isl_give pet_expr *pet_expr_access_plug_in_args(__isl_take pet_expr *expr,
	__isl_keep pet_context *pc);
__isl_give pet_expr *pet_expr_plug_in_args(__isl_take pet_expr *expr,
	__isl_keep pet_context *pc);

#if defined(__cplusplus)
}
#endif

#endif
