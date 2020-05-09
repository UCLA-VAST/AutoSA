#ifndef ISL_ID_TO_PET_EXPR_H
#define ISL_ID_TO_PET_EXPR_H

#include <isl/id.h>
#include "expr.h"
#include "maybe_pet_expr.h"

#define ISL_KEY		isl_id
#define ISL_VAL		pet_expr
#define ISL_HMAP_SUFFIX	isl_id_to_pet_expr
#define ISL_HMAP	isl_id_to_pet_expr
#include <isl/hmap.h>
#undef ISL_KEY
#undef ISL_VAL
#undef ISL_HMAP_SUFFIX
#undef ISL_HMAP

#endif
