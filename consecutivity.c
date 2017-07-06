/*
 * Copyright 2017      Sven Verdoolaege
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege.
 */

#include <isl/mat.h>
#include <isl/space.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_set.h>
#include <isl/union_map.h>

#define ISL_TYPE	isl_multi_aff
#include <isl/maybe_templ.h>
#undef ISL_TYPE

#include "consecutivity.h"

/* A ppcg_consecutive object contains the input to
 * ppcg_consecutive_add_consecutivity_constraints and mainly consists
 * of a list of consecutive arrays and
 * lists of tagged read and write accesses.
 *
 * "array_list" is a list of arrays.
 * Each array is represented by a universe set.
 * The order of the arrays in the list determines the priority of the arrays.
 * "arrays" is the union of the elements in "array_list".
 * This field may be NULL if it has not been computed yet.
 *
 * "reads" are the tagged read accesses.
 * This field may be NULL if it has not been set yet.
 * "write" are the tagged write accesses.
 * This field may be NULL if it has not been set yet.
 * "accesses" is the union of "reads" and "writes".
 * This field may be NULL if it has not been (re)computed yet.
 */
struct ppcg_consecutive {
	isl_set_list *array_list;
	isl_union_set *arrays;

	isl_union_map *reads;
	isl_union_map *writes;
	isl_union_map *accesses;
};

/* Initialize a ppcg_consecutive object from a list of array spaces
 * for the arrays that should be considered consecutive.
 * The order of the arrays in the list determines the priority of the arrays.
 * For ease of use, the list of spaces is converted to
 * a list of universe sets.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_from_array_list(
	__isl_take isl_space_list *array_list)
{
	int i, n;
	isl_ctx *ctx;
	ppcg_consecutive *c;

	if (!array_list)
		return NULL;
	ctx = isl_space_list_get_ctx(array_list);
	c = isl_calloc_type(ctx, struct ppcg_consecutive);
	if (!c)
		goto error;
	n = isl_space_list_n_space(array_list);
	c->array_list = isl_set_list_alloc(ctx, n);
	for (i = 0; i < n; ++i) {
		isl_set *set;

		set = isl_set_universe(isl_space_list_get_space(array_list, i));
		c->array_list = isl_set_list_add(c->array_list, set);
	}
	isl_space_list_free(array_list);
	if (!c->array_list)
		return ppcg_consecutive_free(c);
	return c;
error:
	isl_space_list_free(array_list);
	return NULL;
}

/* Free "c" and return NULL.
 */
__isl_null ppcg_consecutive *ppcg_consecutive_free(
	__isl_take ppcg_consecutive *c)
{
	if (!c)
		return NULL;

	isl_set_list_free(c->array_list);
	isl_union_set_free(c->arrays);
	isl_union_map_free(c->reads);
	isl_union_map_free(c->writes);
	isl_union_map_free(c->accesses);
	free(c);

	return NULL;
}

/* Replace the tagged read access relation of "c" by "reads".
 * Each domain element needs to uniquely identify a reference to an array.
 * The "accesses" field is derived from "reads" (and "writes") and
 * therefore needs to be reset in case it had already been computed.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_reads(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *reads)
{
	if (!c || !reads)
		goto error;
	c->accesses = isl_union_map_free(c->accesses);
	isl_union_map_free(c->reads);
	c->reads = reads;
	return c;
error:
	ppcg_consecutive_free(c);
	isl_union_map_free(reads);
	return NULL;
}

/* Replace the tagged write access relation of "c" by "writes".
 * Each domain element needs to uniquely identify a reference to an array.
 * The "accesses" field is derived from "writes" (and "reads") and
 * therefore needs to be reset in case it had already been computed.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_writes(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *writes)
{
	if (!c || !writes)
		goto error;
	c->accesses = isl_union_map_free(c->accesses);
	isl_union_map_free(c->writes);
	c->writes = writes;
	return c;
error:
	ppcg_consecutive_free(c);
	isl_union_map_free(writes);
	return NULL;
}

/* Return the isl_ctx to which "c" belongs.
 */
isl_ctx *ppcg_consecutive_get_ctx(__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;

	return isl_set_list_get_ctx(c->array_list);
}

/* Is the list of consecutive arrays of "c" empty?
 */
isl_bool ppcg_consecutive_is_empty(__isl_keep ppcg_consecutive *c)
{
	int n;

	if (!c)
		return isl_bool_error;
	n = isl_set_list_n_set(c->array_list);
	if (n < 0)
		return isl_bool_error;
	return n == 0 ? isl_bool_true : isl_bool_false;
}

/* Return the list of arrays of "c", each represented by a universe set.
 */
__isl_give isl_set_list *ppcg_consecutive_get_array_list(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	return isl_set_list_copy(c->array_list);
}

/* isl_set_list_foreach callback for adding "set"
 * to the isl_union_set *uset.
 */
static isl_stat add_set(__isl_take isl_set *set, void *user)
{
	isl_union_set **uset = user;

	*uset = isl_union_set_add_set(*uset, set);
	if (!*uset)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Return the union of the elements of "list" as an isl_union_set.
 */
static __isl_give isl_union_set *set_list_union(__isl_take isl_set_list *list)
{
	isl_ctx *ctx;
	isl_space *space;
	isl_union_set *uset;

	ctx = isl_set_list_get_ctx(list);
	space = isl_space_params_alloc(ctx, 0);
	uset = isl_union_set_empty(space);
	if (isl_set_list_foreach(list, &add_set, &uset) < 0)
		uset = isl_union_set_free(uset);
	isl_set_list_free(list);
	return uset;
}

/* Return the union of the arrays of "c", where
 * each array is represented by a universe set.
 * Keep a copy of the result in c->arrays for later reuse.
 */
__isl_give isl_union_set *ppcg_consecutive_get_arrays(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	if (c->arrays)
		return isl_union_set_copy(c->arrays);

	c->arrays = set_list_union(ppcg_consecutive_get_array_list(c));
	return isl_union_set_copy(c->arrays);
}

/* If "umap" has not been initialized yet (i.e., is NULL), then initialize
 * it to an empty set within the context of "c".
 */
static __isl_give isl_union_map *init_union_map(__isl_keep ppcg_consecutive *c,
	__isl_take isl_union_map *umap)
{
	isl_ctx *ctx;
	isl_space *space;

	if (umap)
		return umap;

	ctx = ppcg_consecutive_get_ctx(c);
	space = isl_space_params_alloc(ctx, 0);
	return isl_union_map_empty(space);
}

/* Return the tagged access relation of "c', the union of its read and write
 * access relations.
 * Take into account that the read and/or write access relation
 * may not have been set (yet).
 * Keep a copy of the result in c->accesses for later reuse.
 */
__isl_give isl_union_map *ppcg_consecutive_get_accesses(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	if (c->accesses)
		return isl_union_map_copy(c->accesses);

	c->accesses = init_union_map(c, c->accesses);
	if (c->reads)
		c->accesses = isl_union_map_union(c->accesses,
						isl_union_map_copy(c->reads));
	if (c->writes)
		c->accesses = isl_union_map_union(c->accesses,
						isl_union_map_copy(c->writes));

	return isl_union_map_copy(c->accesses);
}

/* isl_pw_multi_aff_foreach_piece callback for extracting
 * the isl_multi_aff expression.
 */
static isl_stat set_multi_aff(__isl_take isl_set *set,
	__isl_take isl_multi_aff *ma, void *user)
{
	isl_multi_aff **p = user;

	*p = ma;
	isl_set_free(set);

	return isl_stat_ok;
}

/* Does any of the affine expressions in "ma" involve
 * any local variables or a non-trivial denominator?
 */
static isl_bool any_local_or_denom(__isl_keep isl_multi_aff *ma)
{
	int i, n;

	if (!ma)
		return isl_bool_error;
	n = isl_multi_aff_dim(ma, isl_dim_out);
	for (i = 0; i < n; ++i) {
		isl_bool trivial;
		isl_aff *aff;
		isl_val *d;
		unsigned dim;

		aff = isl_multi_aff_get_aff(ma, i);
		if (!aff)
			return isl_bool_error;
		dim = isl_aff_dim(aff, isl_dim_div);
		d = isl_aff_get_denominator_val(aff);
		trivial = isl_val_is_one(d);
		isl_val_free(d);
		isl_aff_free(aff);
		if (trivial < 0)
			return isl_bool_error;
		if (dim > 0 || !trivial)
			return isl_bool_true;
	}
	return isl_bool_false;
}

/* Check if "map" represents an integral affine function and, if so,
 * return it as an isl_multi_aff.
 * A function is considered affine here if it is not piecewise and
 * if the single possibly quasi-affine expression does not involve any
 * local variables.
 *
 * First check if "map" represents a function,
 * then convert it to an isl_pw_multi_aff, check if it consists
 * of a single (quasi-)affine expression and finally check
 * that this affine expression does not involve any local variables or
 * non-trivial denominator.
 */
static __isl_give isl_maybe_isl_multi_aff map_try_extract_affine(
	__isl_keep isl_map *map)
{
	isl_maybe_isl_multi_aff m;
	isl_pw_multi_aff *pma;

	m.valid = isl_map_is_single_valued(map);
	if (m.valid < 0 || !m.valid)
		return m;
	pma = isl_pw_multi_aff_from_map(isl_map_copy(map));
	if (!pma) {
		m.valid = isl_bool_error;
		return m;
	}
	if (isl_pw_multi_aff_n_piece(pma) != 1) {
		m.valid = isl_bool_false;
		isl_pw_multi_aff_free(pma);
		return m;
	}
	if (isl_pw_multi_aff_foreach_piece(pma, &set_multi_aff, &m.value) < 0)
		m.value = isl_multi_aff_free(m.value);
	isl_pw_multi_aff_free(pma);
	m.valid = isl_bool_not(any_local_or_denom(m.value));
	if (m.valid < 0 || !m.valid)
		m.value = isl_multi_aff_free(m.value);

	return m;
}

/* isl_union_map_every_map callback that checks if "access"
 * represents an affine index expression and, if so,
 * adds this affine index expression to "list".
 */
static isl_bool add_affine_access(__isl_keep isl_map *access, void *user)
{
	isl_multi_aff_list **list = user;
	isl_maybe_isl_multi_aff m;

	m = map_try_extract_affine(access);
	if (m.valid >= 0 && m.valid)
		*list = isl_multi_aff_list_add(*list, m.value);
	if (!*list)
		return isl_bool_error;
	return m.valid;
}

/* Given a sequence of accesses to a given array,
 * extract the corresponding affine index expressions
 * as an isl_multi_aff_list.
 * If any of the index expressions is not affine,
 * then return an empty list.
 */
static __isl_give isl_multi_aff_list *extract_array_affine(
	__isl_take isl_union_map *accesses)
{
	isl_bool all;
	isl_ctx *ctx;
	isl_multi_aff_list *affine;

	ctx = isl_union_map_get_ctx(accesses);
	affine = isl_multi_aff_list_alloc(ctx, 0);
	all = isl_union_map_every_map(accesses, &add_affine_access, &affine);
	isl_union_map_free(accesses);
	if (all < 0)
		return isl_multi_aff_list_free(affine);
	if (all)
		return affine;
	isl_multi_aff_list_free(affine);
	return isl_multi_aff_list_alloc(ctx, 0);
}

/* Extract the linear part, i.e., the coefficients of the input variables,
 * from the integral affine expression "ma".
 */
static __isl_give isl_mat *extract_linear(__isl_take isl_multi_aff *ma)
{
	int i, j, n, n_var;
	isl_ctx *ctx;
	isl_mat *lin;

	ctx = isl_multi_aff_get_ctx(ma);
	n = isl_multi_aff_dim(ma, isl_dim_out);
	n_var = isl_multi_aff_dim(ma, isl_dim_in);

	lin = isl_mat_alloc(ctx, n, n_var);
	for (i = 0; i < n; ++i) {
		isl_aff *aff = isl_multi_aff_get_aff(ma, i);
		for (j = 0; j < n_var; ++j) {
			isl_val *v;

			v = isl_aff_get_coefficient_val(aff, isl_dim_in, j);
			lin = isl_mat_set_element_val(lin, i, j, v);
		}
		isl_aff_free(aff);
	}

	isl_multi_aff_free(ma);
	return lin;
}

/* Construct an isl_multi_aff in space "space" with linear part
 * (i.e., the coefficients of the input variables)
 * specified by "lin".
 */
static __isl_give isl_multi_aff *multi_aff_from_linear(
	__isl_take isl_space *space, __isl_take isl_mat *lin)
{
	int i, j, n, n_var;
	isl_multi_aff *ma;

	ma = isl_multi_aff_zero(space);
	n = isl_multi_aff_dim(ma, isl_dim_out);
	n_var = isl_multi_aff_dim(ma, isl_dim_in);

	for (i = 0; i < n; ++i) {
		isl_aff *aff = isl_multi_aff_get_aff(ma, i);
		for (j = 0; j < n_var; ++j) {
			isl_val *v;

			v = isl_mat_get_element_val(lin, i, j);
			aff = isl_aff_set_coefficient_val(aff,
							    isl_dim_in, j, v);
		}
		ma = isl_multi_aff_set_aff(ma, i, aff);
	}

	isl_mat_free(lin);
	return ma;
}

/* Construct a space with as domain "space" and as range
 * a product of a space of dimension "n_outer" and
 * a space of dimension "n_inner".
 */
static __isl_give isl_space *construct_split_space(__isl_take isl_space *space,
	int n_outer, int n_inner)
{
	isl_space *space2;

	space = isl_space_from_domain(space);
	space2 = isl_space_copy(space);
	space = isl_space_add_dims(space, isl_dim_out, n_outer);
	space2 = isl_space_add_dims(space2, isl_dim_out, n_inner);
	space = isl_space_range_product(space, space2);

	return space;
}

/* Construct an affine expression on domain "space"
 * with linear affine expressions "outer" and "inner".
 * The result is the range product of affine expressions
 * corresponding to "outer" and "inner".
 */
static __isl_give isl_multi_aff *construct_multi_aff(
	__isl_take isl_space *space, __isl_take isl_mat *outer,
	__isl_take isl_mat *inner)
{
	int n1, n2;
	isl_mat *mat;

	n1 = isl_mat_rows(outer);
	n2 = isl_mat_rows(inner);
	space = construct_split_space(space, n1, n2);

	mat = isl_mat_concat(outer, inner);

	return multi_aff_from_linear(space, mat);
}

/* Representation of an index expression split in an outer and
 * an inner part, where the inner part is expected to be
 * linearly independent of the outer part.
 *
 * "rows" is the concatenation of "outer" and "inner" or NULL
 * if it has not been computed yet.
 */
struct ppcg_split_index {
	isl_mat *outer;
	isl_mat *inner;
	isl_mat *rows;
};
typedef struct ppcg_split_index ppcg_split_index;

/* Free "si" and return NULL.
 */
static __isl_null ppcg_split_index *ppcg_split_index_free(
	__isl_take ppcg_split_index *si)
{
	if (!si)
		return NULL;
	isl_mat_free(si->outer);
	isl_mat_free(si->inner);
	isl_mat_free(si->rows);
	free(si);
	return NULL;
}

/* Create a ppcg_split_index with the given outer and inner parts.
 */
static __isl_give ppcg_split_index *ppcg_split_index_alloc(
	__isl_take isl_mat *outer, __isl_take isl_mat *inner)
{
	ppcg_split_index *si;

	if (!outer || !inner)
		goto error;

	si = isl_calloc_type(isl_mat_get_ctx(outer), struct ppcg_split_index);
	if (!si)
		goto error;
	si->outer = outer;
	si->inner = inner;
	return si;
error:
	isl_mat_free(outer);
	isl_mat_free(inner);
	return NULL;
}

/* Extract a ppcg_split_index from the linear part of the integer affine
 * expression "ma" with "n_inner" inner expressions and the remaining
 * expressions (if any) outer expressions.
 */
static __isl_give ppcg_split_index *ppcg_split_index_from_multi_aff(
	__isl_take isl_multi_aff *ma, int n_inner)
{
	int n;
	isl_mat *outer;
	isl_mat *inner;
	isl_mat *lin;

	lin = extract_linear(ma);
	if (!lin)
		return NULL;
	n = isl_mat_rows(lin);
	if (n_inner > n)
		n_inner = n;
	outer = isl_mat_drop_rows(isl_mat_copy(lin), n - n_inner, n_inner);
	inner = isl_mat_drop_rows(lin, 0, n - n_inner);
	return ppcg_split_index_alloc(outer, inner);
}

/* Return the concatenation of the outer and inner expressions of "si".
 * Keep a copy of the result in si->rows for later reuse.
 */
static __isl_give isl_mat *ppcg_split_index_get_rows(
	__isl_keep ppcg_split_index *si)
{
	if (!si)
		return NULL;
	if (si->rows)
		return isl_mat_copy(si->rows);
	si->rows = isl_mat_copy(si->outer);
	si->rows = isl_mat_concat(si->rows, isl_mat_copy(si->inner));
	return isl_mat_copy(si->rows);
}

/* Is the inner part of "si" linearly independent of the outer part?
 */
static isl_bool ppcg_split_index_is_independent(__isl_keep ppcg_split_index *si)
{
	if (!si)
		return isl_bool_error;
	return isl_mat_has_linearly_independent_rows(si->outer, si->inner);
}

/* Do "si1" and "si2" have the same outer and inner parts?
 */
static isl_bool ppcg_split_index_is_equal(__isl_keep ppcg_split_index *si1,
	__isl_keep ppcg_split_index *si2)
{
	isl_bool equal;

	if (!si1 || !si2)
		return isl_bool_error;
	equal = isl_mat_is_equal(si1->outer, si2->outer);
	if (equal < 0 || !equal)
		return equal;
	return isl_mat_is_equal(si1->inner, si2->inner);
}

/* Given two split index expressions G1 -> H1 and G2 -> H2,
 * can they be combined into an expression of the form G -> H1;H2
 * while maintaining the property that the inner part is linearly
 * independent of the outer part?
 * A sufficient condition is that H2 is linearly independent of
 * the matrix with rows G1, H1 and G2.
 */
static isl_bool ppcg_split_index_can_append(__isl_keep ppcg_split_index *si1,
	__isl_keep ppcg_split_index *si2)
{
	isl_mat *t;
	isl_bool ok;

	if (!si2 || !si1)
		return isl_bool_error;

	t = ppcg_split_index_get_rows(si1);
	t = isl_mat_concat(t, isl_mat_copy(si2->outer));
	ok = isl_mat_has_linearly_independent_rows(t, si2->inner);
	isl_mat_free(t);

	return ok;
}

/* Given two split index expressions G1 -> H1 and G2 -> H2,
 * combine them into an expression of the form G -> H1;H2.
 * G is constructed in such a way that G;H1 spans the same space
 * as G1;H1;G2.  In particular, it combines the rows of G1
 * with a basis extension for G1;H1 that covers G2.
 */
static __isl_give ppcg_split_index *ppcg_split_index_append(
	__isl_keep ppcg_split_index *si1, __isl_keep ppcg_split_index *si2)
{
	isl_mat *outer;
	isl_mat *inner;
	isl_mat *t;

	if (!si2 || !si1)
		return NULL;
	outer = isl_mat_copy(si2->outer);
	t = ppcg_split_index_get_rows(si1);
	outer = isl_mat_row_basis_extension(t, outer);
	outer = isl_mat_concat(isl_mat_copy(si1->outer), outer);
	inner = isl_mat_copy(si1->inner);
	inner = isl_mat_concat(inner, isl_mat_copy(si2->inner));
	return ppcg_split_index_alloc(outer, inner);
}

/* Do the given split index expressions have the same inner part and
 * can their outer parts be combined without introducing
 * a linear dependence with the inner part?
 */
static isl_bool ppcg_split_index_can_combine(__isl_keep ppcg_split_index *si1,
	__isl_keep ppcg_split_index *si2)
{
	isl_bool ok;
	isl_mat *t;

	if (!si1 || !si2)
		return isl_bool_error;
	ok = isl_mat_is_equal(si1->inner, si2->inner);
	if (ok < 0 || !ok)
		return ok;
	t = isl_mat_concat(isl_mat_copy(si1->outer), isl_mat_copy(si2->outer));
	ok = isl_mat_has_linearly_independent_rows(t, si1->inner);
	isl_mat_free(t);

	return ok;
}

/* Given two split index expressions that have the same inner part,
 * construct a combined split index expression with the same inner part
 * and as outer part the concatenation of the outer parts
 * of the two inputs.
 * The concatenation of the outer parts is replaced by a basis
 * in order to remove redundant rows.
 * In particular, if the two inputs are identical, then the output
 * is equal to the inputs as well.  Without the basis computation,
 * the output would have duplicate rows in its outer part.
 */
static __isl_give ppcg_split_index *ppcg_split_index_combine(
	__isl_keep ppcg_split_index *si1, __isl_keep ppcg_split_index *si2)
{
	isl_mat *outer;
	isl_mat *inner;

	outer = isl_mat_copy(si1->outer);
	outer = isl_mat_concat(outer, isl_mat_copy(si2->outer));
	outer = isl_mat_row_basis(outer);
	inner = isl_mat_copy(si1->inner);
	return ppcg_split_index_alloc(outer, inner);
}

/* Construct an affine expression on domain "space"
 * with linear affine expressions defined by "si".
 * The result is the range product of affine expressions
 * corresponding to outer and inner parts of "si".
 */
static __isl_give isl_multi_aff *ppcg_split_index_construct_multi_aff(
	__isl_take ppcg_split_index *si, __isl_take isl_space *space)
{
	isl_multi_aff *ma;

	if (!si)
		goto error;

	ma = construct_multi_aff(space, isl_mat_copy(si->outer),
				isl_mat_copy(si->inner));
	ppcg_split_index_free(si);
	return ma;
error:
	ppcg_split_index_free(si);
	isl_space_free(space);
	return NULL;
}

/* A list of ppcg_split_index objects.
 *
 * "n" is the number of elements in the list.
 * "size" is the allocated size of "p".
 */
struct ppcg_split_index_list {
	isl_ctx *ctx;
	int n;
	int size;
	ppcg_split_index **p;
};
typedef struct ppcg_split_index_list ppcg_split_index_list;

/* Remove all the elements from "list".
 */
static __isl_give ppcg_split_index_list *ppcg_split_index_list_clear(
	__isl_take ppcg_split_index_list *list)
{
	int i;

	if (!list)
		return NULL;
	for (i = 0; i < list->n; ++i)
		ppcg_split_index_free(list->p[i]);
	list->n = 0;
	return list;
}

/* Free "list" and return NULL.
 */
static __isl_null ppcg_split_index_list *ppcg_split_index_list_free(
	__isl_take ppcg_split_index_list *list)
{
	list = ppcg_split_index_list_clear(list);
	if (!list)
		return NULL;
	free(list->p);
	free(list);
	return NULL;
}

/* Construct an empty ppcg_split_index_list with room for "size" elements.
 */
static __isl_give ppcg_split_index_list *ppcg_split_index_list_alloc(
	isl_ctx *ctx, int size)
{
	ppcg_split_index_list *list;

	list = isl_calloc_type(ctx, struct ppcg_split_index_list);
	if (!list)
		return NULL;
	list->ctx = ctx;
	list->p = isl_calloc_array(ctx, ppcg_split_index *, size);
	if (!list->p)
		return ppcg_split_index_list_free(list);
	list->size = size;
	return list;
}

/* Return the isl_ctx to which "list" belongs.
 */
static isl_ctx *ppcg_split_index_list_get_ctx(
	__isl_keep ppcg_split_index_list *list)
{
	return list ? list->ctx : NULL;
}

/* Return the number of elements in "list", or -1 on error.
 */
static int ppcg_split_index_list_n(__isl_keep ppcg_split_index_list *list)
{
	return list ? list->n : -1;
}

/* Make room for at least "n" more elements in "list".
 */
static ppcg_split_index_list *ppcg_split_index_list_grow(
	__isl_take ppcg_split_index_list *list, int n)
{
	int size;
	isl_ctx *ctx;
	ppcg_split_index **p;

	if (!list)
		return NULL;
	if (list->n + n <= list->size)
		return list;
	ctx = ppcg_split_index_list_get_ctx(list);
	size = ((list->n + n + 1) * 3) / 2;
	p = isl_realloc_array(ctx, list->p, ppcg_split_index *, size);
	if (!p)
		return ppcg_split_index_list_free(list);
	list->p = p;
	list->size = size;
	return list;
}

/* Add "si" to the end of "list".
 */
static ppcg_split_index_list *ppcg_split_index_list_add(
	__isl_take ppcg_split_index_list *list, __isl_take ppcg_split_index *si)
{
	list = ppcg_split_index_list_grow(list, 1);
	if (!list || !si)
		goto error;
	list->p[list->n] = si;
	list->n++;
	return list;
error:
	ppcg_split_index_list_free(list);
	ppcg_split_index_free(si);
	return NULL;
}

/* Return a list that is the concatenation of "list1" and "list2".
 */
static ppcg_split_index_list *ppcg_split_index_list_concat(
	__isl_take ppcg_split_index_list *list1,
	__isl_take ppcg_split_index_list *list2)
{
	int i;

	if (!list2)
		goto error;
	list1 = ppcg_split_index_list_grow(list1, list2->n);
	if (!list1)
		goto error;
	for (i = 0; i < list2->n; ++i)
		list1->p[list1->n + i] = list2->p[i];
	list1->n += list2->n;
	list2->n = 0;
	ppcg_split_index_list_free(list2);
	return list1;
error:
	ppcg_split_index_list_free(list1);
	ppcg_split_index_list_free(list2);
	return NULL;
}

/* Call "fn" on all the elements of "list" and destroy the list.
 */
static isl_stat ppcg_split_index_list_reduce(
	__isl_take ppcg_split_index_list *list,
	isl_stat (*fn)(__isl_take ppcg_split_index *si, void *user), void *user)
{
	int i;
	ppcg_split_index *si;

	if (!list)
		return isl_stat_error;

	for (i = 0; i < list->n; ++i) {
		ppcg_split_index *si;

		si = list->p[i];
		list->p[i] = NULL;
		if (fn(si, user) < 0)
			goto error;
	}

	ppcg_split_index_list_free(list);
	return isl_stat_ok;
error:
	ppcg_split_index_list_free(list);
	return isl_stat_error;
}

/* Does "test" succeed for all elements of "list"?
 */
static isl_bool ppcg_split_index_list_every(
	__isl_keep ppcg_split_index_list *list,
	isl_bool (*test)(__isl_keep ppcg_split_index *si, void *user),
	void *user)
{
	int i;

	if (!list)
		return isl_bool_error;

	for (i = 0; i < list->n; ++i) {
		isl_bool ok;

		ok = test(list->p[i], user);
		if (ok < 0 || !ok)
			return ok;
	}

	return isl_bool_true;
}

/* ppcg_split_index_list_every callback that checks whether "si1"
 * is _not_ equal to "si2".
 */
static isl_bool not_equal(__isl_keep ppcg_split_index *si1, void *user)
{
	ppcg_split_index *si2 = user;

	return isl_bool_not(ppcg_split_index_is_equal(si1, si2));
}

/* Does "si" appear in "list"?
 * That is, is it not the case that all elements are different from "si"?
 */
static isl_bool ppcg_split_index_list_find(
	__isl_keep ppcg_split_index_list *list, __isl_keep ppcg_split_index *si)
{
	isl_bool not_found;

	not_found = ppcg_split_index_list_every(list, &not_equal, si);
	return isl_bool_not(not_found);
}

/* Add "si" to the end of "list" if it does not already appear in "list".
 */
static ppcg_split_index_list *add_unique(__isl_take ppcg_split_index_list *list,
	__isl_take ppcg_split_index *si)
{
	isl_bool found;

	found = ppcg_split_index_list_find(list, si);
	if (found >= 0 && !found)
		return ppcg_split_index_list_add(list, si);
	ppcg_split_index_free(si);
	if (found < 0)
		return ppcg_split_index_list_free(list);
	return list;
}

/* Check if "si2" can be appended to "si1" to form a valid split index
 * expression.
 * If so, construct this combined split index expression and
 * add it to the end of "list", provided it does not already appear in "list".
 */
static __isl_give ppcg_split_index_list *add_append(
	__isl_take ppcg_split_index_list *list,
	__isl_keep ppcg_split_index *si1, __isl_keep ppcg_split_index *si2)
{
	isl_bool ok;
	ppcg_split_index *si;

	ok = ppcg_split_index_can_append(si1, si2);
	if (ok < 0)
		return ppcg_split_index_list_free(list);
	if (!ok)
		return list;
	si = ppcg_split_index_append(si1, si2);
	return add_unique(list, si);
}

/* Check if "si1" and "si2" have the same inner part and
 * can be combined into a single valid split index expression.
 * If so, construct this combined split index expression and
 * add it to the end of "list", provided it does not already appear in "list".
 */
static __isl_give ppcg_split_index_list *add_combine(
	__isl_take ppcg_split_index_list *list,
	__isl_keep ppcg_split_index *si1, __isl_keep ppcg_split_index *si2)
{
	isl_bool ok;
	ppcg_split_index *si;

	ok = ppcg_split_index_can_combine(si1, si2);
	if (ok < 0)
		return ppcg_split_index_list_free(list);
	if (!ok)
		return list;
	si = ppcg_split_index_combine(si1, si2);
	return add_unique(list, si);
}

/* Combine the elements of "list" in different ways to obtain
 * split index expressions that cover multiple elements of "list".
 *
 * In particular, start with a list of combinations containing
 * the first element of "list" and
 * consider the remaining elements of "list" in turn.
 * For each such element, try and combine the element with all combinations
 * computed in the previous iteration, storing successful combinations before
 * the corresponding combinations for the previous iteration and
 * before the element itself.
 *
 * If "combine_all" is set, then only those combinations are kept
 * that cover all elements in the list considered so far.
 * The resulting list may still contain more than one element
 * if there are multiple ways of covering all elements in the original list.
 */
static __isl_give ppcg_split_index_list *ppcg_split_index_list_combine(
	__isl_take ppcg_split_index_list *list, int combine_all)
{
	int i, j;
	isl_ctx *ctx;
	ppcg_split_index_list *res;

	if (!list)
		return NULL;
	if (list->n == 0)
		return list;

	ctx = ppcg_split_index_list_get_ctx(list);
	res = ppcg_split_index_list_alloc(ctx, 1);
	res = ppcg_split_index_list_add(res, list->p[0]);
	list->p[0] = NULL;
	for (i = 1; i < list->n; ++i) {
		int prev_n;
		int min_size;
		ppcg_split_index_list *prev = res;

		if (!prev)
			return ppcg_split_index_list_free(list);
		prev_n = ppcg_split_index_list_n(prev);
		min_size = 1;
		if (!combine_all)
			min_size += prev_n;
		res = ppcg_split_index_list_alloc(ctx, min_size);

		for (j = 0; j < prev_n; ++j) {
			res = add_append(res, prev->p[j], list->p[i]);
			res = add_append(res, list->p[i], prev->p[j]);
			res = add_combine(res, list->p[i], prev->p[j]);
			if (!combine_all) {
				res = add_unique(res, prev->p[j]);
				prev->p[j] = NULL;
			}
		}
		if (!combine_all) {
			res = add_unique(res, list->p[i]);
			list->p[i] = NULL;
		}

		ppcg_split_index_list_free(prev);
	}

	ppcg_split_index_list_free(list);
	return res;
}

/* isl_multi_aff_list_foreach callback that adds the split index expression
 * extracted from "ma" to *si_list.
 */
static isl_stat add_split_index(__isl_take isl_multi_aff *ma, void *user)
{
	ppcg_split_index_list **si_list = user;
	ppcg_split_index *si;

	si = ppcg_split_index_from_multi_aff(ma, 1);
	*si_list = ppcg_split_index_list_add(*si_list, si);

	if (!*si_list)
		return isl_stat_error;
	return isl_stat_ok;
}

/* ppcg_split_index_is_independent wrapper that can serve
 * as a callback to ppcg_split_index_list_every.
 */
static isl_bool is_independent(__isl_keep ppcg_split_index *si, void *user)
{
	return ppcg_split_index_is_independent(si);
}

/* Given a sequence "list" of index expressions for a given array,
 * construct a sequence of split index expressions that cover
 * all the index expressions in "list".
 * That is, if there is any index expression where the innermost
 * expression is not linearly independent of the outer expressions,
 * or if it is impossible to construct a split index expression
 * that covers all index expressions, then return an empty list.
 */
static __isl_give ppcg_split_index_list *extract_array_split_index(
	__isl_take isl_multi_aff_list *list)
{
	int n;
	isl_bool ok;
	isl_ctx *ctx;
	ppcg_split_index_list *si_list;

	ctx = isl_multi_aff_list_get_ctx(list);
	n = isl_multi_aff_list_n_multi_aff(list);
	si_list = ppcg_split_index_list_alloc(ctx, n);
	if (isl_multi_aff_list_foreach(list, &add_split_index, &si_list) < 0)
		si_list = ppcg_split_index_list_free(si_list);
	isl_multi_aff_list_free(list);

	ok = ppcg_split_index_list_every(si_list, &is_independent, NULL);
	if (ok < 0)
		return ppcg_split_index_list_free(si_list);
	if (!ok)
		return ppcg_split_index_list_clear(si_list);

	si_list = ppcg_split_index_list_combine(si_list, 1);

	return si_list;
}

/* Internal data structure for extract_split_index.
 *
 * "accesses" are the original accesses.
 * "si_list" collects the results.
 */
struct ppcg_extract_split_index_data {
	isl_union_map *accesses;
	ppcg_split_index_list *si_list;
};

/* isl_set_list_foreach callback that extracts split index expressions
 * from the accesses to "array" and adds them to data->si_list.
 *
 * Restrict the accesses in data->accesses to "array",
 * compute the corresponding split index expressions and
 * add the result to data->si_list.
 */
static isl_stat array_extract_split_index(__isl_take isl_set *array, void *user)
{
	struct ppcg_extract_split_index_data *data = user;
	isl_union_map *array_accesses;
	isl_multi_aff_list *array_affine;
	ppcg_split_index_list *si_list;

	array_accesses = isl_union_map_copy(data->accesses);
	array_accesses = isl_union_map_intersect_range(array_accesses,
						isl_union_set_from_set(array));
	array_affine = extract_array_affine(array_accesses);
	si_list = extract_array_split_index(array_affine);
	data->si_list = ppcg_split_index_list_concat(data->si_list, si_list);
	if (!data->si_list)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Given the accesses "accesses" from a given statement,
 * construct split index expressions for each of the arrays
 * marked consecutive in "c" and concatenate the results.
 * The split index expressions for a given array cover all
 * references to that array.  If no such split index expressions
 * can be found for a given array, then the array is ignored
 * in this statement.
 */
static __isl_give ppcg_split_index_list *extract_split_index(
	__isl_keep ppcg_consecutive *c, __isl_take isl_union_map *accesses)
{
	struct ppcg_extract_split_index_data data = { accesses };
	isl_ctx *ctx;
	isl_set_list *arrays;

	ctx = ppcg_consecutive_get_ctx(c);
	arrays = ppcg_consecutive_get_array_list(c);
	data.si_list = ppcg_split_index_list_alloc(ctx, 0);
	if (isl_set_list_foreach(arrays, &array_extract_split_index, &data) < 0)
		data.si_list = ppcg_split_index_list_free(data.si_list);
	isl_set_list_free(arrays);
	isl_union_map_free(accesses);

	return data.si_list;
}

/* Internal data structure for compute_stmt_intra.
 *
 * "space" is the domain space of the isl_multi_aff objects that are
 * constructed.
 * "list" collects the objects.
 */
struct ppcg_stmt_intra_data {
	isl_space *space;
	isl_multi_aff_list *list;
};

/* ppcg_split_index_list_reduce callback that converts "si"
 * into an isl_multi_aff and adds it to data->list.
 */
static isl_stat add_multi_aff(__isl_take ppcg_split_index *si, void *user)
{
	struct ppcg_stmt_intra_data *data = user;
	isl_multi_aff *ma;

	ma = ppcg_split_index_construct_multi_aff(si,
						isl_space_copy(data->space));
	data->list = isl_multi_aff_list_add(data->list, ma);
	if (!data->list)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Given the accesses "accesses" from statement "stmt",
 * construct a list of intra-statement consecutivity constraints
 * for each of the arrays marked consecutive in "c".
 *
 * Extract split index expressions for each of the arrays separately and
 * then try to combine them as much as possible.
 * Those that cover the highest number of arrays appear first
 * in the resulting list.
 * Finally, convert the split index expressions to
 * intra-statement consecutivity constraints.
 */
static __isl_give isl_multi_aff_list *compute_stmt_intra(
	__isl_keep ppcg_consecutive *c, __isl_keep isl_set *stmt,
	__isl_take isl_union_map *accesses)
{
	struct ppcg_stmt_intra_data data;
	int n;
	ppcg_split_index_list *si_list;

	si_list = extract_split_index(c, accesses);
	if (!si_list)
		return NULL;
	if (ppcg_split_index_list_n(si_list) == 0) {
		isl_ctx *ctx = ppcg_consecutive_get_ctx(c);
		ppcg_split_index_list_free(si_list);
		return isl_multi_aff_list_alloc(ctx, 0);
	}

	si_list = ppcg_split_index_list_combine(si_list, 0);
	if (!si_list)
		return NULL;

	data.space = isl_set_get_space(stmt);
	n = ppcg_split_index_list_n(si_list);
	data.list = isl_multi_aff_list_alloc(isl_set_get_ctx(stmt), n);
	if (ppcg_split_index_list_reduce(si_list, &add_multi_aff, &data) < 0)
		data.list = isl_multi_aff_list_free(data.list);
	isl_space_free(data.space);
	return data.list;
}

/* Internal data structure for add_stmt_intra.
 *
 * "c" is the input to ppcg_consecutive_add_consecutivity_constraints.
 * "accesses" are tagged accesses to the consecutive arrays
 * with the tags moved to the range.
 *
 * "intra" is the list of intra-statement consecutivity constraints
 * extended by add_stmt_intra.
 */
struct ppcg_add_stmt_intra_data {
	ppcg_consecutive *c;
	isl_union_map *accesses;
	isl_multi_aff_list *intra;
};

/* Compute a list of intra-statement consecutivity constraints
 * for statement "stmt" and add the results to data->intra.
 *
 * The intra-statement consecutivity constraints are computed from
 * the access relation restricted to "stmt".
 */
static isl_stat add_stmt_intra(__isl_take isl_set *stmt, void *user)
{
	struct ppcg_add_stmt_intra_data *data = user;
	isl_union_map *stmt_accesses;
	isl_multi_aff_list *stmt_intra;

	stmt_accesses = isl_union_map_copy(data->accesses);
	stmt_accesses = isl_union_map_intersect_domain(stmt_accesses,
				    isl_union_set_from_set(isl_set_copy(stmt)));
	stmt_accesses = isl_union_map_uncurry(stmt_accesses);
	stmt_intra = compute_stmt_intra(data->c, stmt, stmt_accesses);
	isl_set_free(stmt);
	data->intra = isl_multi_aff_list_concat(data->intra, stmt_intra);
	if (!data->intra)
		return isl_stat_error;

	return isl_stat_ok;
}

/* Add intra-statement consecutivity constraints to "sc" based
 * on the information in "c".
 * The list of consecutive arrays is assumed to be non-empty.
 *
 * Compute a list of intra-statement consecutivity constraints
 * for each statement that accesses a consecutive array,
 * concatenate the results and add the concatenated list
 * as intra-statement consecutivity constraints to "sc".
 */
static __isl_give isl_schedule_constraints *add_intra_consecutivity_constraints(
	__isl_keep ppcg_consecutive *c, __isl_take isl_schedule_constraints *sc)
{
	struct ppcg_add_stmt_intra_data data = { c };
	isl_ctx *ctx;
	isl_union_set *arrays;
	isl_union_set *stmts;
	isl_union_map *universe;

	ctx = isl_schedule_constraints_get_ctx(sc);
	arrays = ppcg_consecutive_get_arrays(c);
	data.intra = isl_multi_aff_list_alloc(ctx, 0);
	data.accesses = ppcg_consecutive_get_accesses(c);
	data.accesses = isl_union_map_intersect_range(data.accesses, arrays);
	data.accesses = isl_union_map_curry(data.accesses);
	universe = isl_union_map_universe(isl_union_map_copy(data.accesses));
	stmts = isl_union_map_domain(universe);
	if (isl_union_set_foreach_set(stmts, &add_stmt_intra, &data) < 0)
		data.intra = isl_multi_aff_list_free(data.intra);
	isl_union_set_free(stmts);
	isl_union_map_free(data.accesses);

	sc = isl_schedule_constraints_set_intra_consecutivity(sc, data.intra);

	return sc;
}

/* Add consecutivity constraints to "sc" based on the information in "c".
 * If there are no consecutive arrays, then no constraints need to be added.
 */
__isl_give isl_schedule_constraints *
ppcg_consecutive_add_consecutivity_constraints(__isl_keep ppcg_consecutive *c,
	__isl_take isl_schedule_constraints *sc)
{
	isl_bool empty;

	empty = ppcg_consecutive_is_empty(c);
	if (empty < 0)
		return isl_schedule_constraints_free(sc);
	if (empty)
		return sc;
	sc = add_intra_consecutivity_constraints(c, sc);
	return sc;
}
