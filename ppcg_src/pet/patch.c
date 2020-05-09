/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2014      Ecole Normale Superieure. All rights reserved.
 * Copyright 2015      Sven Verdoolaege. All rights reserved.
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

#include "array.h"
#include "expr.h"
#include "patch.h"

/* Given the data space "space1" of an index expression passed
 * to a function and the data space "space2" of the corresponding
 * array accessed in the function, construct and return the complete
 * data space from the perspective of the function call.
 * If add is set, then it is not the index expression with space "space1" itself
 * that is passed to the function, but its address.
 *
 * In the simplest case, no member accesses are involved and "add" is not set.
 * Let "space1" be of the form A[x] and "space2" of the form B[y].
 * Then the returned space is A[x,y].
 * That is, the dimension is the sum of the dimensions and the name
 * is that of "space1".
 * If "add" is set, then the final dimension of "space1" is the same
 * as the initial dimension of "space2" and the dimension of the result
 * is one less that the sum.  This also applies when the dimension
 * of "space1" is zero.  The dimension of "space2" can never be zero
 * when "add" is set since a pointer value is passed to the function,
 * which is treated as an array of dimension at least 1.
 *
 * If "space1" involves any member accesses, then it is the innermost
 * array space of "space1" that needs to be extended with "space2".
 * This innermost array space appears in the range of the wrapped
 * relation in "space1".
 *
 * If "space2" involves any member accesses, then it is the outermost
 * array space of "space2" that needs to be combined with innermost
 * array space of "space1".  This outermost array space appears
 * in the deepest nesting of the domains and is therefore handled
 * recursively.
 *
 * For example, if "space1" is of the form
 *
 *	s_a[s[x] -> a[y]]
 *
 * and "space2" is of the form
 *
 *	d_b_c[d_b[d[z] -> b[u]] -> c[v]]
 *
 * then the resulting space is
 *
 *	s_a_b_c[s_a_b[s_a[s[x] -> a[y,z]] -> b[u]] -> c[v]]
 */
static __isl_give isl_space *patch_space(__isl_take isl_space *space1,
	__isl_take isl_space *space2, int add)
{
	int dim;
	isl_id *id;

	if (!space1 || !space2)
		goto error;

	if (isl_space_is_wrapping(space2)) {
		isl_ctx *ctx;
		isl_space *dom;
		const char *name1, *name2;
		char *name;

		ctx = isl_space_get_ctx(space1);
		space2 = isl_space_unwrap(space2);
		dom = isl_space_domain(isl_space_copy(space2));
		space1 = patch_space(space1, dom, add);
		space2 = isl_space_range(space2);
		name1 = isl_space_get_tuple_name(space1, isl_dim_set);
		name2 = isl_space_get_tuple_name(space2, isl_dim_set);
		name = pet_array_member_access_name(ctx, name1, name2);
		space1 = isl_space_product(space1, space2);
		space1 = isl_space_set_tuple_name(space1, isl_dim_set, name);
		free(name);
		return space1;
	}

	dim = isl_space_dim(space2, isl_dim_set) - add;
	id = isl_space_get_tuple_id(space1, isl_dim_set);
	if (isl_space_is_wrapping(space1)) {
		isl_id *id;

		space1 = isl_space_unwrap(space1);
		id = isl_space_get_tuple_id(space1, isl_dim_out);
		space1 = isl_space_add_dims(space1, isl_dim_out, dim);
		space1 = isl_space_set_tuple_id(space1, isl_dim_out, id);
		space1 = isl_space_wrap(space1);
	} else {
		space1 = isl_space_add_dims(space1, isl_dim_out, dim);
	}
	space1 = isl_space_set_tuple_id(space1, isl_dim_set, id);

	isl_space_free(space2);
	return space1;
error:
	isl_space_free(space1);
	isl_space_free(space2);
	return NULL;
}

/* Drop the initial dimension of "map", assuming that it is equal to zero.
 * If it turns out not to be equal to zero, then drop the initial dimension
 * of "map" after setting the value to zero and print a warning (if "warn"
 * is set).
 */
static __isl_give isl_map *drop_initial_zero(__isl_take isl_map *map,
	__isl_keep isl_map *prefix, int warn)
{
	isl_map *zeroed;

	zeroed = isl_map_copy(map);
	zeroed = isl_map_fix_si(zeroed, isl_dim_out, 0, 0);
	map = isl_map_subtract(map, isl_map_copy(zeroed));
	if (warn && !isl_map_is_empty(map)) {
		fprintf(stderr, "possible out-of-bounds accesses\n");
		isl_map_dump(map);
		fprintf(stderr, "when passing\n");
		isl_map_dump(prefix);
	}
	isl_map_free(map);
	map = zeroed;
	map = isl_map_project_out(map, isl_dim_out, 0, 1);
	return map;
}

/* Drop the initial dimension of "mpa", assuming that it is equal to zero.
 */
static __isl_give isl_multi_pw_aff *mpa_drop_initial_zero(
	__isl_take isl_multi_pw_aff *mpa)
{
	isl_pw_aff *pa;
	isl_set *cond;

	pa = isl_multi_pw_aff_get_pw_aff(mpa, 0);
	cond = isl_pw_aff_zero_set(pa);
	mpa = isl_multi_pw_aff_drop_dims(mpa, isl_dim_out, 0, 1);
	mpa = isl_multi_pw_aff_intersect_domain(mpa, cond);

	return mpa;
}

/* Construct an isl_multi_aff of the form
 *
 *	[i_0, ..., i_pos, i_{pos+1}, i_{pos+2}, ...] ->
 *		[i_0, ..., i_pos + i_{pos+1}, i_{pos+2}, ...]
 *
 * "space" prescribes the domain of this function.
 */
static __isl_give isl_multi_aff *patch_add(__isl_take isl_space *space,
	int pos)
{
	isl_multi_aff *ma;
	isl_aff *aff1, *aff2;

	ma = isl_multi_aff_identity(isl_space_map_from_set(space));
	aff1 = isl_multi_aff_get_aff(ma, pos);
	aff2 = isl_multi_aff_get_aff(ma, pos + 1);
	aff1 = isl_aff_add(aff1, aff2);
	ma = isl_multi_aff_set_aff(ma, pos, aff1);
	ma = isl_multi_aff_drop_dims(ma, isl_dim_out, pos + 1, 1);

	return ma;
}

/* Given an identity mapping "id" that adds structure to
 * the range of "map" with dimensions "pos" and "pos + 1" replaced
 * by their sum, adjust "id" to apply to the range of "map" directly.
 * That is, plug in
 *
 *	[i_0, ..., i_pos, i_{pos+1}, i_{pos+2}, ...] ->
 *		[i_0, ..., i_pos + i_{pos+1}, i_{pos+2}, ...]
 *
 * in "id", where the domain of this mapping corresponds to the range
 * of "map" and the range of this mapping corresponds to the original
 * domain of "id".
 */
static __isl_give isl_map *patch_map_add(__isl_take isl_map *id,
	__isl_keep isl_map *map, int pos)
{
	isl_space *space;
	isl_multi_aff *ma;

	space = isl_space_range(isl_map_get_space(map));
	ma = patch_add(space, pos);
	id = isl_map_preimage_domain_multi_aff(id, ma);

	return id;
}

/* Given an identity mapping "id" that adds structure to
 * the range of "mpa" with dimensions "pos" and "pos + 1" replaced
 * by their sum, adjust "id" to apply to the range of "mpa" directly.
 * That is, plug in
 *
 *	[i_0, ..., i_pos, i_{pos+1}, i_{pos+2}, ...] ->
 *		[i_0, ..., i_pos + i_{pos+1}, i_{pos+2}, ...]
 *
 * in "id", where the domain of this mapping corresponds to the range
 * of "mpa" and the range of this mapping corresponds to the original
 * domain of "id".
 */
static __isl_give isl_multi_pw_aff *patch_mpa_add(
	__isl_take isl_multi_pw_aff *id, __isl_keep isl_multi_pw_aff *mpa,
	int pos)
{
	isl_space *space;
	isl_multi_aff *ma;

	space = isl_space_range(isl_multi_pw_aff_get_space(mpa));
	ma = patch_add(space, pos);
	id = isl_multi_pw_aff_pullback_multi_aff(id, ma);

	return id;
}

/* Return the dimension of the innermost array in the data space "space".
 * If "space" is not a wrapping space, then it does not involve any
 * member accesses and the innermost array is simply the accessed
 * array itself.
 * Otherwise, the innermost array is encoded in the range of the
 * wrapped space.
 */
static int innermost_dim(__isl_keep isl_space *space)
{
	int dim;

	if (!isl_space_is_wrapping(space))
		return isl_space_dim(space, isl_dim_set);

	space = isl_space_copy(space);
	space = isl_space_unwrap(space);
	dim = isl_space_dim(space, isl_dim_out);
	isl_space_free(space);

	return dim;
}

/* Internal data structure for patch_map.
 *
 * "prefix" is the index expression passed to the function
 * "add" is set if it is the address of "prefix" that is passed to the function.
 * "warn" is set if a warning should be printed when an initial index
 * expression is not (obviously) zero when it should be.
 * "res" collects the results.
 */
struct pet_patch_map_data {
	isl_map *prefix;
	int add;
	int warn;

	isl_union_map *res;
};

/* Combine the index expression data->prefix with the subaccess relation "map".
 * If data->add is set, then it is not the index expression data->prefix itself
 * that is passed to the function, but its address.
 *
 * If data->add is not set, then we essentially need to concatenate
 * data->prefix with map, except that we need to make sure that
 * the target space is set correctly.  This target space is computed
 * by the function patch_space.  We then simply compute the flat
 * range product and subsequently reset the target space.
 *
 * If data->add is set then the outer dimension of "map" is an offset
 * with respect to the inner dimension of data->prefix and we therefore
 * need to add these two dimensions rather than simply concatenating them.
 * This computation is performed in patch_map_add.
 * If however, the innermost accessed array of data->prefix is
 * zero-dimensional, then there is no innermost dimension of data->prefix
 * to add to the outermost dimension of "map",  Instead, we are passing
 * a pointer to a scalar member, meaning that the outermost dimension
 * of "map" needs to be zero and that this zero needs to be removed
 * from the concatenation.  This computation is performed in drop_initial_zero.
 */
static isl_stat patch_map(__isl_take isl_map *map, void *user)
{
	struct pet_patch_map_data *data = user;
	isl_space *space;
	isl_map *id;
	int pos, dim;

	space = isl_space_range(isl_map_get_space(data->prefix));
	dim = innermost_dim(space);
	pos = isl_space_dim(space, isl_dim_set) - dim;
	space = patch_space(space, isl_space_range(isl_map_get_space(map)),
				data->add);
	if (data->add && dim == 0)
		map = drop_initial_zero(map, data->prefix, data->warn);
	map = isl_map_flat_range_product(isl_map_copy(data->prefix), map);
	space = isl_space_map_from_set(space);
	space = isl_space_add_dims(space, isl_dim_in, 0);
	id = isl_map_identity(space);
	if (data->add && dim != 0)
		id = patch_map_add(id, map, pos + dim - 1);
	map = isl_map_apply_range(map, id);
	data->res = isl_union_map_add_map(data->res, map);

	return isl_stat_ok;
}

/* Combine the index expression "prefix" with the index expression "mpa".
 * If add is set, then it is not the index expression "prefix" itself
 * that is passed to the function, but its address.
 *
 * If add is not set, then we essentially need to concatenate
 * "prefix" with "mpa", except that we need to make sure that
 * the target space is set correctly.  This target space is computed
 * by the function patch_space.  We then simply compute the flat
 * range product and subsequently reset the target space.
 *
 * If add is set then the outer dimension of "mpa" is an offset
 * with respect to the inner dimension of "prefix" and we therefore
 * need to add these two dimensions rather than simply concatenating them.
 * This computation is performed in patch_mpa_add.
 * If however, the innermost accessed array of "prefix" is
 * zero-dimensional, then there is no innermost dimension of "prefix"
 * to add to the outermost dimension of "mpa",  Instead, we are passing
 * a pointer to a scalar member, meaning that the outermost dimension
 * of "mpa" needs to be zero and that this zero needs to be removed
 * from the concatenation.  This computation is performed in
 * mpa_drop_initial_zero.
 */
__isl_give isl_multi_pw_aff *pet_patch_multi_pw_aff(
	__isl_take isl_multi_pw_aff *prefix, __isl_take isl_multi_pw_aff *mpa,
	int add)
{
	isl_space *space;
	int pos, dim;
	isl_multi_pw_aff *id;

	space = isl_space_range(isl_multi_pw_aff_get_space(prefix));
	dim = innermost_dim(space);
	pos = isl_space_dim(space, isl_dim_set) - dim;
	space = patch_space(space,
			isl_space_range(isl_multi_pw_aff_get_space(mpa)), add);
	if (add && dim == 0)
		mpa = mpa_drop_initial_zero(mpa);
	mpa = isl_multi_pw_aff_flat_range_product(prefix, mpa);
	space = isl_space_map_from_set(space);
	space = isl_space_add_dims(space, isl_dim_in, 0);
	id = isl_multi_pw_aff_identity(space);
	if (add && dim != 0)
		id = patch_mpa_add(id, mpa, pos + dim - 1);
	mpa = isl_multi_pw_aff_pullback_multi_pw_aff(id, mpa);

	return mpa;
}

/* Combine the index expression "prefix" with the subaccess relation "umap".
 * If "add" is set, then it is not the index expression "prefix" itself
 * that was passed to the function, but its address.
 * If "warn" is set, then a warning is printed when an initial index
 * expression is not (obviously) zero when it should be.
 *
 * We call patch_map on each map in "umap" and return the combined results.
 */
__isl_give isl_union_map *pet_patch_union_map(
	__isl_take isl_multi_pw_aff *prefix, __isl_take isl_union_map *umap,
	int add, int warn)
{
	struct pet_patch_map_data data;
	isl_map *map;

	map = isl_map_from_multi_pw_aff(prefix);
	map = isl_map_align_params(map, isl_union_map_get_space(umap));
	umap = isl_union_map_align_params(umap, isl_map_get_space(map));
	data.prefix = map;
	data.add = add;
	data.warn = warn;
	data.res = isl_union_map_empty(isl_union_map_get_space(umap));
	if (isl_union_map_foreach_map(umap, &patch_map, &data) < 0)
		data.res = isl_union_map_free(data.res);
	isl_union_map_free(umap);
	isl_map_free(data.prefix);

	return data.res;
}
