#ifndef PET_EXPR_TO_ISL_PW_AFF_H
#define PET_EXPR_TO_ISL_PW_AFF_H

#define ISL_KEY		pet_expr
#define ISL_VAL		isl_pw_aff
#define ISL_HMAP_SUFFIX	pet_expr_to_isl_pw_aff
#define ISL_HMAP	pet_expr_to_isl_pw_aff
#include <isl/hmap.h>
#undef ISL_KEY
#undef ISL_VAL
#undef ISL_HMAP_SUFFIX
#undef ISL_HMAP

#endif
