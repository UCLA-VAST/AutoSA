#ifndef PET_LOC_H
#define PET_LOC_H

#include <isl/ctx.h>

#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

extern pet_loc pet_loc_dummy;

__isl_give pet_loc *pet_loc_alloc(isl_ctx *ctx,
	unsigned start, unsigned end, int line, __isl_take char *indent);
__isl_give pet_loc *pet_loc_update_start_end(__isl_take pet_loc *loc,
	unsigned start, unsigned end);
__isl_give pet_loc *pet_loc_update_start_end_from_loc(__isl_take pet_loc *loc,
	__isl_keep pet_loc *loc2);
__isl_give pet_loc *pet_loc_set_indent(__isl_take pet_loc *loc,
	__isl_take char *indent);

#if defined(__cplusplus)
}
#endif

#endif
