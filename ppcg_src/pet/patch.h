#ifndef PET_PATCH_H
#define PET_PATCH_H

#include <isl/aff.h>
#include <isl/union_map.h>

__isl_give isl_multi_pw_aff *pet_patch_multi_pw_aff(
	__isl_take isl_multi_pw_aff *prefix, __isl_take isl_multi_pw_aff *mpa,
	int add);
__isl_give isl_union_map *pet_patch_union_map(
	__isl_take isl_multi_pw_aff *prefix, __isl_take isl_union_map *umap,
	int add, int warn);

#endif
