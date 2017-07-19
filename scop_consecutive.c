/*
 * Copyright 2017      Sven Verdoolaege
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege.
 */

#include <isl/set.h>
#include <isl/space.h>
#include <isl/union_set.h>
#include <isl/union_map.h>

#include "ppcg.h"
#include "consecutivity.h"
#include "scop_consecutive.h"

/* Look for an array with identifier "id".
 * If found, return its index in pet->arrays.
 * Otherwise, return pet->n_array.
 * Return -1 on error.
 */
static int find_array_index(struct pet_scop *pet, __isl_keep isl_id *id)
{
	int i;

	for (i = 0; i < pet->n_array; ++i) {
		struct pet_array *array = pet->arrays[i];
		isl_id *array_id = isl_set_get_tuple_id(array->extent);

		if (!array_id)
			return -1;
		isl_id_free(array_id);
		if (array_id == id)
			return i;
	}

	return pet->n_array;
}

/* Internal data structure for extract_from_id_list.
 *
 * "pet" is the pet_scop containing information about the arrays.
 * "extents" is the list of extents that is being constructed.
 */
struct ppcg_extract_list_data {
	struct pet_scop *pet;
	isl_set_list *extents;
};

/* isl_id_list_foreach callback that looks for the array with
 * identifier "id" and, if it can be found, adds its extent to data->extents.
 */
static isl_stat add_extent(__isl_take isl_id *id, void *user)
{
	struct ppcg_extract_list_data *data = user;
	struct pet_array *array;
	int i;

	i = find_array_index(data->pet, id);
	isl_id_free(id);
	if (i < 0)
		return isl_stat_error;
	if (i >= data->pet->n_array)
		return isl_stat_ok;
	array = data->pet->arrays[i];
	data->extents = isl_set_list_add(data->extents,
					isl_set_copy(array->extent));
	return isl_stat_ok;
}

/* Construct a ppcg_consecutive object that contains the list
 * of arrays specified by the consecutive_arrays command line option.
 *
 * Extract a list of isl_id objects from the command line option,
 * convert it to a list of array extents and use that to construct
 * a ppcg_consecutive object.
 */
static __isl_give ppcg_consecutive *extract_from_id_list(
	struct ppcg_scop *scop)
{
	struct ppcg_extract_list_data data = { scop->pet };
	isl_ctx *ctx;
	isl_id_list *list;

	ctx = isl_set_get_ctx(scop->context);
	list = isl_id_list_read_from_str(ctx,
					scop->options->consecutive_arrays);
	data.extents = isl_set_list_alloc(ctx, 0);
	if (isl_id_list_foreach(list, &add_extent, &data) < 0)
		data.extents = isl_set_list_free(data.extents);
	isl_id_list_free(list);

	return ppcg_consecutive_from_extent_list(data.extents);
}

/* isl_set_list_sort callback that orders higher-dimensional sets
 * before lower-dimensional sets.
 */
static int cmp_dim(__isl_keep isl_set *a, __isl_keep isl_set *b, void *user)
{
	return isl_set_dim(b, isl_dim_set) - isl_set_dim(a, isl_dim_set);
}

/* Extract a ppcg_consecutive object that contains the list of
 * extents of accessed arrays that are marked consecutive in "scop".
 * Put higher-dimensional arrays before lower-dimensional arrays.
 *
 * If the user has specified any consecutive arrays on the command line
 * then use those arrays instead, in the order specified by the user.
 */
__isl_give ppcg_consecutive *ppcg_scop_extract_consecutive(
	struct ppcg_scop *scop)
{
	int i;
	isl_ctx *ctx;
	isl_union_map *accesses;
	isl_union_set *arrays;
	isl_set_list *extents;

	if (scop->options->consecutive_arrays)
		return extract_from_id_list(scop);

	ctx = isl_set_get_ctx(scop->context);
	extents = isl_set_list_alloc(ctx, 0);
	accesses = isl_union_map_copy(scop->reads);
	accesses = isl_union_map_union(accesses,
					isl_union_map_copy(scop->may_writes));
	arrays = isl_union_map_range(isl_union_map_universe(accesses));
	for (int i = 0; i < scop->pet->n_array; ++i) {
		struct pet_array *array = scop->pet->arrays[i];
		isl_space *space;
		isl_set *set;
		isl_bool empty;

		if (!array->consecutive)
			continue;

		space = isl_set_get_space(array->extent);
		set = isl_union_set_extract_set(arrays, space);
		empty = isl_set_plain_is_empty(set);
		isl_set_free(set);

		if (empty < 0) {
			extents = isl_set_list_free(extents);
			break;
		}
		if (empty)
			continue;
		extents = isl_set_list_add(extents,
					isl_set_copy(array->extent));
	}
	isl_union_set_free(arrays);

	extents = isl_set_list_sort(extents, &cmp_dim, NULL);

	return ppcg_consecutive_from_extent_list(extents);
}

/* Add consecutivity constraints to "sc" based on the consecutive
 * arrays in scop->consecutive and the access relations and schedule
 * in "scop".
 * Update scop->consecutive with those access relations and schedule and call
 * ppcg_consecutive_add_consecutivity_constraints.
 */
__isl_give isl_schedule_constraints *ppcg_add_consecutivity_constraints(
	__isl_take isl_schedule_constraints *sc, struct ppcg_scop *scop)
{
	isl_union_map *kills;
	ppcg_consecutive *c;

	if (!scop)
		return isl_schedule_constraints_free(sc);
	c = scop->consecutive;
	c = ppcg_consecutive_set_tagged_reads(c,
				isl_union_map_copy(scop->tagged_reads));
	c = ppcg_consecutive_set_tagged_writes(c,
				isl_union_map_copy(scop->tagged_may_writes));
	kills = isl_union_map_copy(scop->tagged_must_kills);
	kills = isl_union_map_union(kills,
				isl_union_map_copy(scop->tagged_must_writes));
	c = ppcg_consecutive_set_tagged_kills(c, kills);
	c = ppcg_consecutive_set_schedule(c, isl_schedule_copy(scop->schedule));
	c = ppcg_consecutive_set_untag(c,
				isl_union_pw_multi_aff_copy(scop->tagger));
	scop->consecutive = c;
	return ppcg_consecutive_add_consecutivity_constraints(c, sc);
}
