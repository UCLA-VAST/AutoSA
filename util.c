/*
 * Copyright 2012      Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d'Ulm, 75230 Paris, France
 */

#include <isl/space.h>
#include <isl/val.h>

#include "util.h"

/* Construct an isl_multi_val living in "space" with all values equal to "val".
 */
__isl_give isl_multi_val *ppcg_multi_val_from_int(__isl_take isl_space *space,
	int val)
{
	int i, n;
	isl_ctx *ctx;
	isl_val *v;
	isl_multi_val *mv;

	if (!space)
		return NULL;

	ctx = isl_space_get_ctx(space);
	n = isl_space_dim(space, isl_dim_set);
	mv = isl_multi_val_zero(space);
	v = isl_val_int_from_si(ctx, val);
	for (i = 0; i < n; ++i)
		mv = isl_multi_val_set_val(mv, i, isl_val_copy(v));
	isl_val_free(v);

	return mv;
}
