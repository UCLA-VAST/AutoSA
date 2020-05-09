#ifndef PET_AFF_H
#define PET_AFF_H

#include <pet.h>

#include <isl/aff.h>
#include <isl/union_map.h>

#if defined(__cplusplus)
extern "C" {
#endif

__isl_give isl_union_map *pet_union_map_move_dims(
	__isl_take isl_union_map *umap,
	enum isl_dim_type dst_type, unsigned dst_pos,
	enum isl_dim_type src_type, unsigned src_pos, unsigned n);

__isl_give isl_multi_aff *pet_prefix_projection(__isl_take isl_space *space,
	int n);

__isl_give isl_val *pet_extract_cst(__isl_keep isl_pw_aff *pa);

__isl_give isl_pw_aff *pet_and(__isl_take isl_pw_aff *lhs,
	__isl_take isl_pw_aff *rhs);
__isl_give isl_pw_aff *pet_not(__isl_take isl_pw_aff *pa);
__isl_give isl_pw_aff *pet_to_bool(__isl_take isl_pw_aff *pa);
__isl_give isl_pw_aff *pet_boolean(enum pet_op_type type,
	__isl_take isl_pw_aff *pa1, __isl_take isl_pw_aff *pa2);
__isl_give isl_pw_aff *pet_comparison(enum pet_op_type type,
	__isl_take isl_pw_aff *pa1, __isl_take isl_pw_aff *pa2);

__isl_give isl_aff *pet_wrap_aff(__isl_take isl_aff *aff, unsigned width);
__isl_give isl_pw_aff *pet_wrap_pw_aff(__isl_take isl_pw_aff *pwaff,
	unsigned width);

#if defined(__cplusplus)
}
#endif

#endif
