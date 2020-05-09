#ifndef PET_EXPR_ACCESS_TYPE_H
#define PET_EXPR_ACCESS_TYPE_H

#if defined(__cplusplus)
extern "C" {
#endif

enum pet_expr_access_type {
	pet_expr_access_may_read,
	pet_expr_access_begin = pet_expr_access_may_read,
	pet_expr_access_fake_killed = pet_expr_access_may_read,
	pet_expr_access_may_write,
	pet_expr_access_must_write,
	pet_expr_access_end,
	pet_expr_access_killed
};

#if defined(__cplusplus)
}
#endif

#endif
