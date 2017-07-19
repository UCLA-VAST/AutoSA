/*
 * Copyright 2017      Sven Verdoolaege
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege.
 */

#include <stdio.h>

#include <isl/mat.h>
#include <isl/space.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_set.h>
#include <isl/union_map.h>
#include <isl/flow.h>

#define ISL_TYPE	isl_multi_aff
#include <isl/maybe_templ.h>
#undef ISL_TYPE

#include "consecutivity.h"
#include "util.h"

/* A ppcg_consecutive object contains the input to
 * ppcg_consecutive_add_consecutivity_constraints and mainly consists
 * of a list of consecutive arrays (possibly with their extents) and
 * lists of tagged read and write accesses.
 *
 * "extent_list" is a list of extents of the arrays.
 * That is, each element in the list is the set of all elements
 * in an array.  The sets need to be bounded in all but the first dimension.
 * The order of the arrays in the list determines the priority of the arrays.
 * This field is NULL if the ppcg_consecutive was created using
 * ppcg_consecutive_from_array_list.
 *
 * "array_list" is a list of arrays.
 * Each array is represented by a universe set.
 * The order of the arrays in the list determines the priority of the arrays.
 * If the ppcg_consecutive was created using ppcg_consecutive_from_extent_list,
 * then "array_list" has the same length as "extent_list"
 * with each element containing the universe of the corresponding element
 * in "extent_list" and the field may be NULL if it has not been computed yet.
 *
 * "extents" is the union of the elements in "extent_list".
 * This field may be NULL if it has not been computed yet.
 * "arrays" is the union of the elements in "array_list".
 * This field may be NULL if it has not been computed yet.
 *
 * "reads" are the tagged read accesses.
 * This field may be NULL if it has not been set yet.
 * "write" are the tagged write accesses.
 * This field may be NULL if it has not been set yet.
 * "accesses" is the union of "reads" and "writes".
 * This field may be NULL if it has not been (re)computed yet.
 *
 * "kills" are the tagged kill accesses.
 * This field may be NULL if it has not been set yet.
 *
 * "untag" maps tagged instance sets to the corresponding untagged
 *	instance set.
 * This field may be NULL if it has not been set or computed yet.
 *
 * "schedule" represents the (original) schedule.
 * This field may be NULL if it has not been set yet.
 * "tagged_schedule" is the schedule formulated in terms of tagged
 *	instance sets.
 * This field may be NULL if it has not been (re)computed yet.
 */
struct ppcg_consecutive {
	isl_set_list *extent_list;
	isl_set_list *array_list;
	isl_union_set *extents;
	isl_union_set *arrays;

	isl_union_map *reads;
	isl_union_map *writes;
	isl_union_map *accesses;
	isl_union_map *kills;

	isl_union_pw_multi_aff *untag;

	isl_schedule *schedule;
	isl_schedule *tagged_schedule;
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

/* Initialize a ppcg_consecutive object from a list of array extents
 * for the arrays that should be considered consecutive.
 * Each element in the list contains the elements of an array and
 * needs to be bounded in all but the first dimension.
 * The order of the arrays in the list determines the priority of the arrays.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_from_extent_list(
	__isl_take isl_set_list *extent_list)
{
	isl_ctx *ctx;
	ppcg_consecutive *c;

	if (!extent_list)
		return NULL;
	ctx = isl_set_list_get_ctx(extent_list);
	c = isl_calloc_type(ctx, struct ppcg_consecutive);
	if (!c)
		goto error;
	c->extent_list = extent_list;
	return c;
error:
	isl_set_list_free(extent_list);
	return NULL;
}

/* Free "c" and return NULL.
 */
__isl_null ppcg_consecutive *ppcg_consecutive_free(
	__isl_take ppcg_consecutive *c)
{
	if (!c)
		return NULL;

	isl_set_list_free(c->extent_list);
	isl_set_list_free(c->array_list);
	isl_union_set_free(c->extents);
	isl_union_set_free(c->arrays);
	isl_union_map_free(c->reads);
	isl_union_map_free(c->writes);
	isl_union_map_free(c->accesses);
	isl_union_map_free(c->kills);

	isl_union_pw_multi_aff_free(c->untag);

	isl_schedule_free(c->schedule);
	isl_schedule_free(c->tagged_schedule);
	free(c);

	return NULL;
}

/* Replace the tagged read access relation of "c" by "reads".
 * Each domain element needs to uniquely identify a reference to an array.
 * The "accesses" field is derived from "reads" (and "writes") and
 * therefore needs to be reset in case it had already been computed.
 * Similarly, the "untag" and "tagged_schedule" fields may have been
 * derived from the "accesses" field (among others).
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_reads(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *reads)
{
	if (!c || !reads)
		goto error;
	c->accesses = isl_union_map_free(c->accesses);
	c->untag = isl_union_pw_multi_aff_free(c->untag);
	c->tagged_schedule = isl_schedule_free(c->tagged_schedule);
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
 * Similarly, the "untag" and "tagged_schedule" fields may have been
 * derived from the "accesses" field (among others).
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_writes(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *writes)
{
	if (!c || !writes)
		goto error;
	c->accesses = isl_union_map_free(c->accesses);
	c->untag = isl_union_pw_multi_aff_free(c->untag);
	c->tagged_schedule = isl_schedule_free(c->tagged_schedule);
	isl_union_map_free(c->writes);
	c->writes = writes;
	return c;
error:
	ppcg_consecutive_free(c);
	isl_union_map_free(writes);
	return NULL;
}

/* Replace the tagged kill access relation of "c" by "kills".
 * Each domain element needs to uniquely identify a reference to an array.
 * The "untag" field may have been derived from "kills" (and "accesses") and
 * therefore needs to be reset in case it had already been computed.
 * Similarly, the "tagged_schedule" field may have been
 * derived from the "untag" field.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_tagged_kills(
	__isl_take ppcg_consecutive *c, __isl_take isl_union_map *kills)
{
	if (!c || !kills)
		goto error;
	c->untag = isl_union_pw_multi_aff_free(c->untag);
	c->tagged_schedule = isl_schedule_free(c->tagged_schedule);
	isl_union_map_free(c->kills);
	c->kills = kills;
	return c;
error:
	ppcg_consecutive_free(c);
	isl_union_map_free(kills);
	return NULL;
}

/* Replace the schedule of "c" by "schedule".
 * The domain of "schedule" is formed by untagged domain elements.
 * The "tagged_schedule" field is derived from "schedule" (and "untag") and
 * therefore needs to be reset in case it had already been computed.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_schedule(
	__isl_take ppcg_consecutive *c, __isl_take isl_schedule *schedule)
{
	if (!c || !schedule)
		goto error;
	c->tagged_schedule = isl_schedule_free(c->tagged_schedule);
	isl_schedule_free(c->schedule);
	c->schedule = schedule;
	return c;
error:
	ppcg_consecutive_free(c);
	isl_schedule_free(schedule);
	return NULL;
}

/* Set the map from tagged instances to untagged instances of "c"
 * to "untag".
 * If this map is not set by the user, then it is computed
 * from the access relations when needed.
 */
__isl_give ppcg_consecutive *ppcg_consecutive_set_untag(
	__isl_take ppcg_consecutive *c,
	__isl_take isl_union_pw_multi_aff *untag)
{
	if (!c || !untag)
		goto error;
	isl_union_pw_multi_aff_free(c->untag);
	c->untag = untag;
	return c;
error:
	ppcg_consecutive_free(c);
	isl_union_pw_multi_aff_free(untag);
	return NULL;
}

/* Return the isl_ctx to which "c" belongs.
 */
isl_ctx *ppcg_consecutive_get_ctx(__isl_keep ppcg_consecutive *c)
{
	isl_set_list *list;

	if (!c)
		return NULL;

	list = c->extent_list ? c->extent_list : c->array_list;
	return isl_set_list_get_ctx(list);
}

/* Is the list of consecutive arrays of "c" empty?
 */
isl_bool ppcg_consecutive_is_empty(__isl_keep ppcg_consecutive *c)
{
	isl_set_list *list;
	int n;

	if (!c)
		return isl_bool_error;
	list = c->extent_list ? c->extent_list : c->array_list;
	n = isl_set_list_n_set(list);
	if (n < 0)
		return isl_bool_error;
	return n == 0 ? isl_bool_true : isl_bool_false;
}

/* Was "c" created using ppcg_consecutive_from_extent_list?
 * That is, are the extents available?
 */
isl_bool ppcg_consecutive_has_extent_list(__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return isl_bool_error;
	if (c->extent_list)
		return isl_bool_true;
	return isl_bool_false;
}

/* Return the list of array extents of "c".
 */
__isl_give isl_set_list *ppcg_consecutive_get_extent_list(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	if (!c->extent_list)
		isl_die(ppcg_consecutive_get_ctx(c), isl_error_invalid,
			"ppcg_consecutive object not created from extent list",
			return NULL);
	return isl_set_list_copy(c->extent_list);
}

/* isl_set_list_foreach callback for adding the universe of "set"
 * to the isl_set_list *list.
 */
static isl_stat add_universe(__isl_take isl_set *set, void *user)
{
	isl_set_list **list = user;
	isl_space *space;

	space = isl_set_get_space(set);
	isl_set_free(set);
	*list = isl_set_list_add(*list, isl_set_universe(space));
	if (!*list)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Return the list of arrays of "c", each represented by a universe set.
 * Construct the list from the extent list if "c" was created
 * using ppcg_consecutive_from_extent_list.
 * Keep a copy of the result in c->array_list for later reuse.
 */
__isl_give isl_set_list *ppcg_consecutive_get_array_list(
	__isl_keep ppcg_consecutive *c)
{
	isl_ctx *ctx;
	int i, n;

	if (!c)
		return NULL;
	if (c->array_list)
		return isl_set_list_copy(c->array_list);
	if (!c->extent_list)
		return NULL;

	n = isl_set_list_n_set(c->extent_list);
	ctx = ppcg_consecutive_get_ctx(c);
	c->array_list = isl_set_list_alloc(ctx, n);
	if (isl_set_list_foreach(c->extent_list,
				&add_universe, &c->array_list) < 0)
		c->array_list = isl_set_list_free(c->array_list);
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

/* Return the union of the array extents of "c".
 * Keep a copy of the result in c->extents for later reuse.
 */
__isl_give isl_union_set *ppcg_consecutive_get_extents(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	if (c->extents)
		return isl_union_set_copy(c->extents);

	c->extents = set_list_union(ppcg_consecutive_get_extent_list(c));
	return isl_union_set_copy(c->extents);
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

/* Return the tagged read access relation of "c'.
 * Take into account that the relation may not have been set (yet).
 */
__isl_give isl_union_map *ppcg_consecutive_get_reads(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	c->reads = init_union_map(c, c->reads);
	return isl_union_map_copy(c->reads);
}

/* Return the tagged write access relation of "c'.
 * Take into account that the relation may not have been set (yet).
 */
__isl_give isl_union_map *ppcg_consecutive_get_writes(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	c->writes = init_union_map(c, c->writes);
	return isl_union_map_copy(c->writes);
}

/* Return the tagged kill access relation of "c'.
 * Take into account that the relation may not have been set (yet).
 */
__isl_give isl_union_map *ppcg_consecutive_get_kills(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	c->kills = init_union_map(c, c->kills);
	return isl_union_map_copy(c->kills);
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

/* Has the user specified a schedule for "c"?
 */
isl_bool ppcg_consecutive_has_schedule(__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return isl_bool_error;
	if (c->schedule)
		return isl_bool_true;
	return isl_bool_false;
}

/* Return the schedule associated to "c".
 * Assume that such a schedule has been specified.
 */
__isl_give isl_schedule *ppcg_consecutive_get_schedule(
	__isl_keep ppcg_consecutive *c)
{
	if (!c)
		return NULL;
	if (!c->schedule)
		isl_die(ppcg_consecutive_get_ctx(c), isl_error_invalid,
			"missing schedule", return NULL);
	return isl_schedule_copy(c->schedule);
}

/* Return an affine function that drops the tags from tagged instances.
 * If it was not set explicitly or computed before,
 * then it is derived from the tagged statement instances
 * involved in any of the accesses.
 * Keep a copy of the result in c->untag for later reuse.
 */
__isl_give isl_union_pw_multi_aff *ppcg_consecutive_get_untag(
	__isl_keep ppcg_consecutive *c)
{
	isl_union_map *accesses;

	if (!c)
		return NULL;
	if (c->untag)
		return isl_union_pw_multi_aff_copy(c->untag);
	accesses = ppcg_consecutive_get_accesses(c);
	accesses = isl_union_map_union(accesses, ppcg_consecutive_get_kills(c));
	c->untag = ppcg_untag_from_tagged_accesses(accesses);
	return isl_union_pw_multi_aff_copy(c->untag);
}

/* Return the schedule associated to "c", reformulated in terms
 * of the tagged instances.
 * Keep a copy of the result in c->tagged_schedule for later reuse.
 */
__isl_give isl_schedule *ppcg_consecutive_get_tagged_schedule(
	__isl_keep ppcg_consecutive *c)
{
	isl_schedule *schedule;
	isl_union_pw_multi_aff *untag;

	if (!c)
		return NULL;
	if (c->tagged_schedule)
		return isl_schedule_copy(c->tagged_schedule);

	schedule = ppcg_consecutive_get_schedule(c);
	untag = ppcg_consecutive_get_untag(c);
	schedule = isl_schedule_pullback_union_pw_multi_aff(schedule, untag);
	c->tagged_schedule = schedule;
	return isl_schedule_copy(c->tagged_schedule);
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

/* Check if "umap" represents an integral affine function
 * (on a single domain) and, if so, return it as an isl_multi_aff.
 * A function is considered affine here if it is not piecewise and
 * if the single possibly quasi-affine expression does not involve any
 * local variables.
 */
static __isl_give isl_maybe_isl_multi_aff umap_try_extract_affine(
	__isl_keep isl_union_map *umap)
{
	isl_maybe_isl_multi_aff m = { isl_bool_false };
	isl_map *map;

	if (!umap) {
		m.valid = isl_bool_error;
		return m;
	}
	if (isl_union_map_n_map(umap) != 1)
		return m;
	map = isl_map_from_union_map(isl_union_map_copy(umap));
	m = map_try_extract_affine(map);
	isl_map_free(map);

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

/* Does "si" represent a valid split index expression?
 * That is, are the affine expressions in the inner part
 * linearly independent of each other and of those in the outer part?
 */
static isl_bool ppcg_split_index_is_valid(__isl_keep ppcg_split_index *si)
{
	int rank;

	if (!si)
		return isl_bool_error;
	rank = isl_mat_rank(si->inner);
	if (rank < 0)
		return isl_bool_error;
	if (rank != isl_mat_rows(si->inner))
		return isl_bool_false;
	return ppcg_split_index_is_independent(si);
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

/* Return a set that selects the last element of "set"
 * in the last "n_last" coordinates.
 * "set" is assumed to be rectangular.
 */
static __isl_give isl_set *select_last(__isl_take isl_set *set, int n_last)
{
	int i, n;
	isl_multi_pw_aff *mpa;

	n = isl_set_dim(set, isl_dim_set);
	mpa = isl_multi_pw_aff_zero(isl_set_get_space(set));
	for (i = 1; i <= n_last; ++i) {
		isl_pw_aff *bound;

		bound = isl_set_dim_max(isl_set_copy(set), n - i);
		mpa = isl_multi_pw_aff_set_pw_aff(mpa, i, bound);
	}
	isl_set_free(set);
	set = isl_set_from_multi_pw_aff(mpa);
	set = isl_set_eliminate(set, isl_dim_set, 0, n - n_last);
	return set;
}

/* Construct a relation that maps each element in "extent"
 * to the next element at the "level" innermost position.
 * If d is the dimension of "extent", then the "level" innermost position
 * is position d - level.
 * At this position, the index expression is incremented by one.
 * At previous positions, the index expressions is kept constant.
 * At later positions, the source should refer to the last element
 * in "extent" while the target should refer to the first (zero) element.
 */
static __isl_give isl_map *construct_next_element_set(
	__isl_take isl_set *extent, int level)
{
	isl_space *space;
	isl_set *last;
	isl_map *map;
	int i, dim;

	space = isl_set_get_space(extent);
	last = select_last(extent, level - 1);
	dim = isl_space_dim(space, isl_dim_set);
	map = ppcg_next(space, dim - level);
	map = isl_map_eliminate(map, isl_dim_out, dim - (level - 1), level - 1);
	for (i = 1; i < level; ++i)
		map = isl_map_fix_si(map, isl_dim_out, dim - i, 0);
	map = isl_map_intersect_domain(map, last);
	return map;
}

/* Internal data structure for construct_next_element.
 * "level" is the position at which the next element is computed.
 * "next" collects the results.
 */
struct ppcg_next_element_data {
	int level;
	isl_union_map *next;
};

/* Construct a relation that maps each element in "set"
 * to the next element at the data->level innermost position
 * (provided there is such a position in "set") and
 * add the result to data->next;
 */
static isl_stat array_next_element(__isl_take isl_set *set, void *user)
{
	struct ppcg_next_element_data *data = user;
	isl_map *next;

	if (data->level > isl_set_dim(set, isl_dim_set)) {
		isl_set_free(set);
		return isl_stat_ok;
	}
	next = construct_next_element_set(set, data->level);
	data->next = isl_union_map_add_map(data->next, next);

	if (!data->next)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Construct a relation that maps each element in "extents"
 * to the next element at the "level" innermost position.
 */
static __isl_give isl_union_map *construct_next_element(
	__isl_keep isl_union_set *extents, int level)
{
	struct ppcg_next_element_data data = { level };
	isl_space *space;

	space = isl_union_set_get_space(extents);
	data.next = isl_union_map_empty(space);

	if (isl_union_set_foreach_set(extents, &array_next_element, &data) < 0)
		data.next = isl_union_map_free(data.next);

	return data.next;
}

/* Compute may-dependences from "source" to "sink" without an intermediate
 * "kill" based on the schedule in "c".
 * "source", "sink" and "kill" are tagged access relations.
 */
static __isl_give isl_union_map *compute_deps(__isl_keep ppcg_consecutive *c,
	__isl_keep isl_union_map *source, __isl_keep isl_union_map *sink,
	__isl_keep isl_union_map *kill)
{
	isl_schedule *schedule;
	isl_union_access_info *info;
	isl_union_flow *flow;
	isl_union_map *dep;

	info = isl_union_access_info_from_sink(isl_union_map_copy(sink));
	info = isl_union_access_info_set_may_source(info,
						isl_union_map_copy(source));
	info = isl_union_access_info_set_kill(info, isl_union_map_copy(kill));
	schedule = ppcg_consecutive_get_tagged_schedule(c);
	info = isl_union_access_info_set_schedule(info, schedule);

	flow = isl_union_access_info_compute_flow(info);
	dep = isl_union_flow_get_may_dependence(flow);
	isl_union_flow_free(flow);

	return dep;
}

/* Compute pairs of statement instances such that the relation
 * between the elements accessed through "accesses" is
 * as specified by "next_element" and such that there is no intermediate "kill".
 * "c" holds the schedule that determines whether a kill is intermediate.
 * "accesses" and "kill" are tagged access relations.
 * "kill" has been extended by the caller to kill both the original
 * element and the next element.
 *
 * First compute an access relation that replaces the accessed
 * array elements by their image in "next_element".
 * This allows the pairs of statement instances to be computed
 * using regular dependence analysis.
 *
 * The access to the source element of "next_element" may appear
 * either before or after the access to the corresponding target element
 * in the original schedule.
 * Dependence analysis is therefore applied twice.
 * In both cases, the kill access relation blocks dependences
 * where either the element itself (the target of "next_element") or
 * the previous element (the source of "next_element") is killed.
 * The result of the dependence analysis where the access to the source
 * element is executed after the one to the target element
 * is reversed to ensure that the source access appears first
 * in the final result.
 */
static __isl_give isl_union_map *compute_next_access(
	__isl_keep ppcg_consecutive *c, __isl_keep isl_union_map *accesses,
	__isl_keep isl_union_map *next_element, __isl_keep isl_union_map *kill)
{
	isl_union_map *accesses_next;
	isl_union_map *dep, *dep2;

	accesses_next = isl_union_map_copy(accesses);
	accesses_next = isl_union_map_apply_range(accesses,
					    isl_union_map_copy(next_element));
	dep = compute_deps(c, accesses_next, accesses, kill);
	dep2 = compute_deps(c, accesses, accesses_next, kill);
	isl_union_map_free(accesses_next);
	dep = isl_union_map_union(dep, isl_union_map_reverse(dep2));

	return dep;
}

/* isl_union_map_remove_map_if callback that removes maps
 * between a domain and itself.
 * "map" relates tagged domain instances.
 * The tags need to be removed before the domain and range
 * spaces can be compared.
 */
static isl_bool is_internal(__isl_keep isl_map *map, void *user)
{
	isl_space *space;
	isl_bool internal;

	space = isl_map_get_space(map);
	space = isl_space_factor_domain(space);
	internal = isl_space_tuple_is_equal(space, isl_dim_in,
					space, isl_dim_out);
	isl_space_free(space);
	return internal;
}

/* Set the name of the output tuple of "ma" to "intra_<n>".
 */
static __isl_give isl_multi_aff *set_name(__isl_take isl_multi_aff *ma, int n)
{
	char buffer[25];

	snprintf(buffer, sizeof(buffer), "intra_%d", n);
	return isl_multi_aff_set_tuple_name(ma, isl_dim_out, buffer);
}

/* Internal data structure for add_inter_consecutivity_constraints.
 *
 * "n" is the number of auxiliary intra-statement consecutivity constraints
 * that have been constructed.  It is used to construct the name
 * of the next such constraint.
 * "intra" collects the auxiliary intra-statement consecutivity constraints
 * that are needed for the constraints in "inter".
 * "inter" collects the constructed inter-statement consecutivity constraints.
 *
 * Inside construct_inter_level, "level" is the level (starting
 * from innermost at level=1) where inter-statement consecutivity
 * is considered, i.e., where the index expression is increased by one.
 * Inside collect_inter, "accesses" is the access relation from
 * which the pairs of accesses to consecutive array elements were computed.
 */
struct ppcg_collect_inter_data {
	int n;
	isl_multi_aff_list *intra;
	isl_map_list *inter;

	int level;
	isl_union_map *accesses;
};

/* Try and extract an affine expression from the "accesses" on "space" and
 * split it into an outer and an inner part with "n_inner" inner expressions.
 * The split is considered to fail if the result would be an invalid
 * intra-statement consecutivity constraint.  That is, it fails
 * if the rows of the inner part are not linearly independent or
 * if they are not linearly independent of the outer part.
 * "accesses" and "space" refer to tagged instances, but the tags
 * are removed from the result.
 *
 * In order to perform the validity check, the isl_multi_aff
 * is converted into an ppcg_split_index.  The final result is
 * then reconstructed from this ppcg_split_index.
 * An alternative would be to perform the split on the isl_multi_aff
 * directly, but reconstructing the isl_multi_aff has the advantage
 * that it will only contain the relevant parts (i.e., the linear parts)
 * of the original expressions.
 */
static __isl_give isl_maybe_isl_multi_aff try_extract_split_affine_on_domain(
	__isl_keep isl_union_map *accesses, __isl_take isl_space *space,
	int n_inner)
{
	isl_union_set *uset;
	isl_maybe_isl_multi_aff m;
	ppcg_split_index *si;

	uset = isl_union_set_from_set(isl_set_universe(space));
	accesses = isl_union_map_copy(accesses);
	accesses = isl_union_map_intersect_domain(accesses, uset);
	accesses = isl_union_map_domain_factor_domain(accesses);
	m = umap_try_extract_affine(accesses);
	isl_union_map_free(accesses);

	if (m.valid < 0 || !m.valid)
		return m;

	space = isl_multi_aff_get_domain_space(m.value);
	si = ppcg_split_index_from_multi_aff(m.value, n_inner);
	m.valid = ppcg_split_index_is_valid(si);

	if (m.valid < 0 || !m.valid) {
		isl_space_free(space);
		ppcg_split_index_free(si);
		m.value = NULL;
		return m;
	}

	m.value = ppcg_split_index_construct_multi_aff(si, space);

	return m;
}

/* Construct an inter-statement consecutivity constraint
 * on "map" referencing the intra-statement consecutivity constraints
 * "ma1" and "ma2".  Add the inter-statement consecutivity constraint
 * to data->inter and the intra-statement consecutivity constraints
 * to data->intra.
 * "map" relates tagged instances, while "ma1" and "ma2"
 * are formulated in terms of untagged instances.
 *
 * The intra-statement consecutivity constraints are given unique names and
 * the corresponding identifiers are attached to domain and range of "map"
 * to form the inter-statement consecutivity constraint.
 */
static isl_stat add_valid_inter(__isl_take isl_map *map,
	__isl_take isl_multi_aff *ma1, __isl_take isl_multi_aff *ma2,
	struct ppcg_collect_inter_data *data)
{
	isl_id *id;
	isl_space *space;

	ma1 = set_name(ma1, data->n++);
	ma2 = set_name(ma2, data->n++);

	space = isl_space_params(isl_map_get_space(map));
	space = isl_space_map_from_set(isl_space_set_from_params(space));
	id = isl_multi_aff_get_tuple_id(ma1, isl_dim_out);
	space = isl_space_set_tuple_id(space, isl_dim_in, id);
	id = isl_multi_aff_get_tuple_id(ma2, isl_dim_out);
	space = isl_space_set_tuple_id(space, isl_dim_out, id);
	map = isl_map_factor_domain(map);
	map = isl_map_product(map, isl_map_universe(space));

	data->intra = isl_multi_aff_list_add(data->intra, ma1);
	data->intra = isl_multi_aff_list_add(data->intra, ma2);
	data->inter = isl_map_list_add(data->inter, map);

	if (!data->intra || !data->inter)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Try and construct an inter-statement consecutivity constraint
 * from pairs of statement instances "map" from different statements
 * that access consecutive elements at the data->level innermost
 * index expression.
 * If successful, add it to data->list.
 *
 * Extract the (tagged) domain spaces from "map" and
 * extract the corresponding split affine index expressions.
 * Construct an inter-statement consecutivity constraint from "map"
 * referencing these split affine index expressions and add them
 * to data->inter and data->intra respectively.
 */
static isl_stat add_inter(__isl_take isl_map *map, void *user)
{
	struct ppcg_collect_inter_data *data = user;
	isl_space *space, *space1, *space2;
	isl_maybe_isl_multi_aff m1, m2;
	isl_multi_aff *ma1, *ma2;
	int level = data->level;

	space = isl_map_get_space(map);
	space1 = isl_space_domain(isl_space_copy(space));
	space2 = isl_space_range(space);
	m1 = try_extract_split_affine_on_domain(data->accesses, space1, level);
	m2 = try_extract_split_affine_on_domain(data->accesses, space2, level);

	if (m1.valid == isl_bool_true && m2.valid == isl_bool_true)
		return add_valid_inter(map, m1.value, m2.value, data);

	isl_multi_aff_free(m1.value);
	isl_multi_aff_free(m2.value);
	isl_map_free(map);
	if (m1.valid < 0 || m2.valid < 0)
		return isl_stat_error;
	return isl_stat_ok;
}

/* Construct a list of inter-statement consecutivity constraints
 * from pairs of statement instances "dep" from different statements
 * that access consecutive elements at the data->level innermost
 * index expression.
 * data->accesses are the accesses from which "dep" was constructed.
 *
 * Consider each map in "dep" in turn.
 */
static isl_stat collect_inter(__isl_take isl_union_map *dep,
	struct ppcg_collect_inter_data *data)
{
	isl_stat res;

	res = isl_union_map_foreach_map(dep, &add_inter, data);
	isl_union_map_free(dep);

	return res;
}

/* Construct a list of inter-statement consecutivity constraints and
 * corresponding referenced intra-statement consecutivity constraints
 * for arrays with extents "extents", read access relation "reads",
 * write access relation "writes" and the information in "c"
 * for the index expression at the data->level innermost position.
 * Add the constraints to data->inter and data->intra.
 * data->level is set to 1 for the innermost index expression.
 *
 * First compute a relation between array elements and
 * the next array element at the given position.
 * Then extend the kill relation to not only kill the original elements
 * but also the next element.  This ensures that it will block
 * dependences to either the element itself or the next element.
 * Use these relations to compute pairs of statement accesses
 * that access consecutive elements (according to "next_element").
 * Do this for the read and write access relations separately.
 * Finally, construct inter-statement consecutivity constraints and
 * corresponding intra-statement consecutivity constraints
 * for each such relation between distinct statements.
 */
static isl_stat construct_inter_level(__isl_keep ppcg_consecutive *c,
	__isl_keep isl_union_set *extents,
	__isl_keep isl_union_map *reads, __isl_keep isl_union_map *writes,
	struct ppcg_collect_inter_data *data)
{
	isl_stat res;
	isl_union_map *kills, *kills_next;
	isl_union_map *next_element;
	isl_union_map *dep, *dep2;
	isl_union_map *accesses;

	kills = ppcg_consecutive_get_kills(c);
	next_element = construct_next_element(extents, data->level);
	kills_next = isl_union_map_apply_range(isl_union_map_copy(kills),
					    isl_union_map_copy(next_element));
	kills = isl_union_map_union(kills, kills_next);
	dep = compute_next_access(c, writes, next_element, kills);
	dep2 = compute_next_access(c, reads, next_element, kills);
	isl_union_map_free(kills);
	isl_union_map_free(next_element);
	dep = isl_union_map_union(dep, dep2);
	dep = isl_union_map_remove_map_if(dep, &is_internal, NULL);

	accesses = isl_union_map_copy(reads);
	accesses = isl_union_map_union(accesses, isl_union_map_copy(writes));
	data->accesses = accesses;
	res = collect_inter(dep, data);
	isl_union_map_free(data->accesses);

	return res;
}

/* isl_union_set_foreach_set callback that updates
 * max_dim to the dimension of "set" if it is greater than
 * the current value of max_dim.
 */
static isl_stat update_max_dim(__isl_take isl_set *set, void *user)
{
	int *max_dim = user;
	int dim;

	dim = isl_set_dim(set, isl_dim_set);
	isl_set_free(set);
	if (dim > *max_dim)
		*max_dim = dim;

	return isl_stat_ok;
}

/* Return the maximal dimension of the sets in "uset".
 */
static int max_dim(__isl_keep isl_union_set *uset)
{
	int max_dim = 0;

	if (isl_union_set_foreach_set(uset, &update_max_dim, &max_dim) < 0)
		max_dim = -1;

	return max_dim;
}

/* Construct a list of inter-statement consecutivity constraints and
 * corresponding referenced intra-statement consecutivity constraints
 * for arrays with extents "extents", read access relation "reads",
 * write access relation "writes" and the information in "c".
 * Add the constraints to data->inter and data->intra.
 *
 * Inter-statement consecutivity is considered at different positions of
 * the index expressions, starting from the innermost (level = 1)
 * to the outermost (level = max).
 */
static isl_stat construct_inter(__isl_keep ppcg_consecutive *c,
	__isl_keep isl_union_set *extents, __isl_keep isl_union_map *reads,
	__isl_keep isl_union_map *writes, struct ppcg_collect_inter_data *data)
{
	int max;

	max = max_dim(extents);
	if (max < 0)
		return isl_stat_error;

	for (data->level = 1; data->level <= max; ++data->level) {
		if (construct_inter_level(c, extents, reads, writes, data) < 0)
			return isl_stat_error;
	}

	return isl_stat_ok;
}

/* isl_union_map_remove_map_if callback that checks
 * whether "map" is not injective.
 */
static isl_bool not_injective(__isl_keep isl_map *map, void *user)
{
	return isl_bool_not(isl_map_is_injective(map));
}

/* Add inter-statement consecutivity constraints to "sc" based
 * on the information in "c", along with
 * corresponding referenced intra-statement consecutivity constraints.
 * The list of consecutive arrays is assumed to be non-empty.
 * The inter-statement consecutivity constraints replace the original
 * inter-statement consecutivity constraints of "sc".
 * The auxiliary intra-statement consecutivity constraints are added
 * to those already referenced by "sc".
 *
 * First check if both a schedule and the array extents are available.
 * If not, then no inter-statement consecutivity constraints can be computed.
 *
 * Inter-statement consecutivity is currently only considered
 * between pairs of accesses that access each element only once.
 *
 * Construct a list of inter-statement consecutivity constraints and
 * a list of corresponding intra-statement consecutivity constraints from
 * the extents of the accessed arrays and the access relations and
 * add them to "sc".
 */
static __isl_give isl_schedule_constraints *add_inter_consecutivity_constraints(
	__isl_keep ppcg_consecutive *c, __isl_take isl_schedule_constraints *sc)
{
	struct ppcg_collect_inter_data data = { 0 };
	isl_bool has_data;
	isl_ctx *ctx;
	isl_union_map *reads, *writes, *accesses;
	isl_union_set *accessed, *extents;
	isl_multi_aff_list *intra;

	has_data = ppcg_consecutive_has_schedule(c);
	if (has_data >= 0 && has_data)
		has_data = ppcg_consecutive_has_extent_list(c);
	if (has_data < 0)
		return isl_schedule_constraints_free(sc);
	if (!has_data)
		return sc;

	accesses = ppcg_consecutive_get_accesses(c);

	accesses = isl_union_map_remove_map_if(accesses, &not_injective, NULL);
	reads = ppcg_consecutive_get_reads(c);
	writes = ppcg_consecutive_get_writes(c);
	reads = isl_union_map_intersect(reads, isl_union_map_copy(accesses));
	writes = isl_union_map_intersect(writes, isl_union_map_copy(accesses));

	accessed = isl_union_map_range(isl_union_map_universe(accesses));
	extents = ppcg_consecutive_get_extents(c);
	extents = isl_union_set_intersect(extents, accessed);

	ctx = ppcg_consecutive_get_ctx(c);
	data.intra = isl_multi_aff_list_alloc(ctx, 0);
	data.inter = isl_map_list_alloc(ctx, 0);
	if (construct_inter(c, extents, reads, writes, &data) < 0)
		sc = isl_schedule_constraints_free(sc);
	isl_union_set_free(extents);
	isl_union_map_free(reads);
	isl_union_map_free(writes);

	intra = isl_schedule_constraints_get_intra_consecutivity(sc);
	intra = isl_multi_aff_list_concat(intra, data.intra);
	sc = isl_schedule_constraints_set_intra_consecutivity(sc, intra);
	sc = isl_schedule_constraints_set_inter_consecutivity(sc, data.inter);

	return sc;
}

/* Add consecutivity constraints to "sc" based on the information in "c".
 * If there are no consecutive arrays, then no constraints need to be added.
 *
 * Inter-statement consecutivity constraints are only added
 * if "c" was created using ppcg_consecutive_from_extent_list and
 * if a schedule was specified.
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
	sc = add_inter_consecutivity_constraints(c, sc);
	return sc;
}
