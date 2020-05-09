/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2012-2014 Ecole Normale Superieure. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY LEIDEN UNIVERSITY ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LEIDEN UNIVERSITY OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as
 * representing official policies, either expressed or implied, of
 * Leiden University.
 */

#include <isl/val.h>
#include <isl/space.h>
#include <isl/local_space.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_map.h>

#include "aff.h"

/* Internal data structure for pet_union_map_move_dims.
 *
 * dst_type, dst_pos, src_type, src_pos and n are the arguments
 * to the isl_map_move_dims calls
 * res collects the results.
 */
struct pet_union_map_move_dims_data {
	enum isl_dim_type dst_type;
	unsigned dst_pos;
	enum isl_dim_type src_type;
	unsigned src_pos;
	unsigned n;

	isl_union_map *res;
};

/* Call isl_map_move_dims on "map" using the arguments in "data" and
 * add the result to data->res.
 */
static isl_stat map_move_dims(__isl_take isl_map *map, void *user)
{
	struct pet_union_map_move_dims_data *data = user;

	map = isl_map_move_dims(map, data->dst_type, data->dst_pos,
				data->src_type, data->src_pos, data->n);
	data->res = isl_union_map_add_map(data->res, map);

	return isl_stat_ok;
}

/* Call isl_map_move_dims with the given arguments on each of the maps
 * in "umap" and return the union of the results.
 *
 * This function can only meaningfully be called on a union map
 * where all maps have the same space for both dst_type and src_type.
 * One of these should then be isl_dim_param as otherwise the union map
 * could only contain a single map.
 */
__isl_give isl_union_map *pet_union_map_move_dims(
	__isl_take isl_union_map *umap,
	enum isl_dim_type dst_type, unsigned dst_pos,
	enum isl_dim_type src_type, unsigned src_pos, unsigned n)
{
	isl_space *space;
	struct pet_union_map_move_dims_data data =
		{ dst_type, dst_pos, src_type, src_pos, n };

	space = isl_union_map_get_space(umap);
	if (src_type == isl_dim_param)
		space = isl_space_drop_dims(space, src_type, src_pos, n);
	data.res = isl_union_map_empty(space);
	if (isl_union_map_foreach_map(umap, &map_move_dims, &data) < 0)
		data.res = isl_union_map_free(data.res);

	isl_union_map_free(umap);
	return data.res;
}

/* Return a function that projects "space" onto its first "n" dimensions,
 * with anonymous target space.
 */
__isl_give isl_multi_aff *pet_prefix_projection(__isl_take isl_space *space,
	int n)
{
	int dim;

	dim = isl_space_dim(space, isl_dim_set);
	return isl_multi_aff_project_out_map(space, isl_dim_set, n, dim - n);
}

/* If the isl_pw_aff on which isl_pw_aff_foreach_piece is called
 * has a constant expression on its only domain, then replace
 * the isl_val in *user by this constant.
 * The caller is assumed to have checked that this function will
 * be called exactly once.
 */
static isl_stat extract_cst(__isl_take isl_set *set, __isl_take isl_aff *aff,
	void *user)
{
	isl_val **inc = (isl_val **)user;

	if (isl_aff_is_cst(aff)) {
		isl_val_free(*inc);
		*inc = isl_aff_get_constant_val(aff);
	}

	isl_set_free(set);
	isl_aff_free(aff);

	return isl_stat_ok;
}

/* If "pa" represents a constant value over a single domain,
 * then return this constant.
 * Otherwise return NaN.
 */
__isl_give isl_val *pet_extract_cst(__isl_keep isl_pw_aff *pa)
{
	isl_val *v;

	if (!pa)
		return NULL;
	v = isl_val_nan(isl_pw_aff_get_ctx(pa));
	if (isl_pw_aff_n_piece(pa) != 1)
		return v;
	if (isl_pw_aff_foreach_piece(pa, &extract_cst, &v) < 0)
		v = isl_val_free(v);
	return v;
}

/* Return the piecewise affine expression "set ? 1 : 0" defined on "dom".
 */
static __isl_give isl_pw_aff *indicator_function(__isl_take isl_set *set,
	__isl_take isl_set *dom)
{
	isl_pw_aff *pa;
	pa = isl_set_indicator_function(set);
	pa = isl_pw_aff_intersect_domain(pa, isl_set_coalesce(dom));
	return pa;
}

/* Return "lhs && rhs", defined on the shared definition domain.
 */
__isl_give isl_pw_aff *pet_and(__isl_take isl_pw_aff *lhs,
	__isl_take isl_pw_aff *rhs)
{
	isl_set *cond;
	isl_set *dom;

	dom = isl_set_intersect(isl_pw_aff_domain(isl_pw_aff_copy(lhs)),
				 isl_pw_aff_domain(isl_pw_aff_copy(rhs)));
	cond = isl_set_intersect(isl_pw_aff_non_zero_set(lhs),
				 isl_pw_aff_non_zero_set(rhs));
	return indicator_function(cond, dom);
}

/* Return "!pa", defined on the domain of "pa".
 *
 * If "pa" involves any NaN, then return NaN.
 */
__isl_give isl_pw_aff *pet_not(__isl_take isl_pw_aff *pa)
{
	isl_set *cond, *dom;

	if (!pa)
		return NULL;
	if (isl_pw_aff_involves_nan(pa)) {
		isl_space *space = isl_pw_aff_get_domain_space(pa);
		isl_local_space *ls = isl_local_space_from_space(space);
		isl_pw_aff_free(pa);
		return isl_pw_aff_nan_on_domain(ls);
	}

	dom = isl_pw_aff_domain(isl_pw_aff_copy(pa));
	cond = isl_pw_aff_zero_set(pa);
	pa = indicator_function(cond, dom);

	return pa;
}

/* Return "!!pa", i.e., a function that is equal to 1 when "pa"
 * is non-zero and equal to 0 when "pa" is equal to zero,
 * on the domain of "pa".
 *
 * If "pa" involves any NaN, then return NaN.
 */
__isl_give isl_pw_aff *pet_to_bool(__isl_take isl_pw_aff *pa)
{
	isl_set *cond, *dom;

	if (!pa)
		return NULL;
	if (isl_pw_aff_involves_nan(pa)) {
		isl_space *space = isl_pw_aff_get_domain_space(pa);
		isl_local_space *ls = isl_local_space_from_space(space);
		isl_pw_aff_free(pa);
		return isl_pw_aff_nan_on_domain(ls);
	}

	dom = isl_pw_aff_domain(isl_pw_aff_copy(pa));
	cond = isl_pw_aff_non_zero_set(pa);
	pa = indicator_function(cond, dom);

	return pa;
}

/* Return the result of applying the comparison operator "type"
 * to "pa1" and "pa2".
 *
 * In particular, construct an isl_pw_aff that is equal to 1
 * on the subset of the shared domain of "pa1" and "pa2" where
 * the comparison holds and 0 on the other part of the shared domain.
 *
 * If "pa1" or "pa2" involve any NaN, then return NaN.
 */
__isl_give isl_pw_aff *pet_comparison(enum pet_op_type type,
	__isl_take isl_pw_aff *pa1, __isl_take isl_pw_aff *pa2)
{
	isl_set *dom;
	isl_set *cond;
	isl_pw_aff *res;

	if (!pa1 || !pa2)
		goto error;
	if (isl_pw_aff_involves_nan(pa1) || isl_pw_aff_involves_nan(pa2)) {
		isl_space *space = isl_pw_aff_get_domain_space(pa1);
		isl_local_space *ls = isl_local_space_from_space(space);
		isl_pw_aff_free(pa1);
		isl_pw_aff_free(pa2);
		return isl_pw_aff_nan_on_domain(ls);
	}

	dom = isl_pw_aff_domain(isl_pw_aff_copy(pa1));
	dom = isl_set_intersect(dom, isl_pw_aff_domain(isl_pw_aff_copy(pa2)));

	switch (type) {
	case pet_op_lt:
		cond = isl_pw_aff_lt_set(pa1, pa2);
		break;
	case pet_op_le:
		cond = isl_pw_aff_le_set(pa1, pa2);
		break;
	case pet_op_gt:
		cond = isl_pw_aff_gt_set(pa1, pa2);
		break;
	case pet_op_ge:
		cond = isl_pw_aff_ge_set(pa1, pa2);
		break;
	case pet_op_eq:
		cond = isl_pw_aff_eq_set(pa1, pa2);
		break;
	case pet_op_ne:
		cond = isl_pw_aff_ne_set(pa1, pa2);
		break;
	default:
		isl_die(isl_pw_aff_get_ctx(pa1), isl_error_internal,
			"not a comparison operator", cond = NULL);
		isl_pw_aff_free(pa1);
		isl_pw_aff_free(pa2);
	}

	cond = isl_set_coalesce(cond);
	res = indicator_function(cond, dom);

	return res;
error:
	isl_pw_aff_free(pa1);
	isl_pw_aff_free(pa2);
	return NULL;
}

/* Return "lhs && rhs", with shortcut semantics.
 * That is, if lhs is false, then the result is defined even if rhs is not.
 * In practice, we compute lhs ? rhs : lhs.
 */
static __isl_give isl_pw_aff *pw_aff_and_then(__isl_take isl_pw_aff *lhs,
	__isl_take isl_pw_aff *rhs)
{
	return isl_pw_aff_cond(isl_pw_aff_copy(lhs), rhs, lhs);
}

/* Return "lhs || rhs", with shortcut semantics.
 * That is, if lhs is true, then the result is defined even if rhs is not.
 * In practice, we compute lhs ? lhs : rhs.
 */
static __isl_give isl_pw_aff *pw_aff_or_else(__isl_take isl_pw_aff *lhs,
	__isl_take isl_pw_aff *rhs)
{
	return isl_pw_aff_cond(isl_pw_aff_copy(lhs), lhs, rhs);
}

/* Return the result of applying the boolean operator "type"
 * to "pa1" and "pa2".
 */
__isl_give isl_pw_aff *pet_boolean(enum pet_op_type type,
	__isl_take isl_pw_aff *pa1, __isl_take isl_pw_aff *pa2)
{
	isl_ctx *ctx;

	switch (type) {
	case pet_op_land:
		return pw_aff_and_then(pa1, pa2);
	case pet_op_lor:
		return pw_aff_or_else(pa1, pa2);
	default:
		ctx = isl_pw_aff_get_ctx(pa1);
		isl_pw_aff_free(pa1);
		isl_pw_aff_free(pa2);
		isl_die(ctx, isl_error_internal,
			"not a boolean operator", return NULL);
	}
}

/* Return an isl_val equal to
 *
 *	2^width
 */
static __isl_give isl_val *wrap_mod(isl_ctx *ctx, unsigned width)
{
	return isl_val_2exp(isl_val_int_from_ui(ctx, width));
}

/* Compute
 *
 *	aff mod 2^width
 */
__isl_give isl_aff *pet_wrap_aff(__isl_take isl_aff *aff, unsigned width)
{
	isl_val *mod;

	mod = wrap_mod(isl_aff_get_ctx(aff), width);
	aff = isl_aff_mod_val(aff, mod);

	return aff;
}

/* Compute
 *
 *	pwaff mod 2^width
 */
__isl_give isl_pw_aff *pet_wrap_pw_aff(__isl_take isl_pw_aff *pwaff,
	unsigned width)
{
	isl_val *mod;

	mod = wrap_mod(isl_pw_aff_get_ctx(pwaff), width);
	pwaff = isl_pw_aff_mod_val(pwaff, mod);

	return pwaff;
}
