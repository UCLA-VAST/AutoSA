#ifndef PET_FILTER_H
#define PET_FILTER_H

#include <isl/id.h>
#include <isl/space.h>
#include <isl/aff.h>

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give isl_pw_multi_aff *pet_filter_insert_pma(__isl_take isl_space *space,
	__isl_take isl_id *id, int satisfied);

#if defined(__cplusplus)
}
#endif

#endif
