#ifndef PET_ARRAY_H
#define PET_ARRAY_H

#include <pet.h>

#include <isl/aff.h>

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give isl_multi_pw_aff *pet_array_subscript(
	__isl_take isl_multi_pw_aff *base, __isl_take isl_pw_aff *index);
char *pet_array_member_access_name(isl_ctx *ctx, const char *base,
	const char *field);
__isl_give isl_multi_pw_aff *pet_array_member(
	__isl_take isl_multi_pw_aff *base, __isl_take isl_multi_pw_aff *field);

#if defined(__cplusplus)
}
#endif

#endif
