#ifndef PET_SUMMARY_H
#define PET_SUMMARY_H

#include <isl/map.h>

#include "expr_access_type.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum pet_arg_type {
	pet_arg_int,
	pet_arg_array,
	pet_arg_other
};

struct pet_function_summary;
typedef struct pet_function_summary pet_function_summary;

__isl_give pet_function_summary *pet_function_summary_alloc(isl_ctx *ctx,
	unsigned n_arg);
__isl_give pet_function_summary *pet_function_summary_copy(
	__isl_keep pet_function_summary *summary);
__isl_null pet_function_summary *pet_function_summary_free(
	__isl_take pet_function_summary *summary);

isl_ctx *pet_function_summary_get_ctx(__isl_keep pet_function_summary *summary);

int pet_function_summary_get_n_arg(__isl_keep pet_function_summary *summary);

__isl_give pet_function_summary *pet_function_summary_set_int(
	__isl_take pet_function_summary *summary, int pos,
	__isl_take isl_id *id);
__isl_give pet_function_summary *pet_function_summary_set_array(
	__isl_take pet_function_summary *summary, int pos,
	__isl_take isl_union_set *may_read, __isl_take isl_union_set *may_write,
	__isl_take isl_union_set *must_write);

int pet_function_summary_arg_is_int(__isl_keep pet_function_summary *summary,
	int pos);
int pet_function_summary_arg_is_array(__isl_keep pet_function_summary *summary,
	int pos);
__isl_give isl_union_map *pet_function_summary_arg_get_access(
	__isl_keep pet_function_summary *summary, int pos,
	enum pet_expr_access_type type);

__isl_give isl_printer *pet_function_summary_print(
	__isl_keep pet_function_summary *summary, __isl_take isl_printer *p);
void pet_function_summary_dump(__isl_keep pet_function_summary *summary);

#if defined(__cplusplus)
}
#endif

#endif
