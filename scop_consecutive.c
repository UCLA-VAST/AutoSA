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

/* isl_space_list_sort callback that orders higher-dimensional spaces
 * before lower-dimensional spaces.
 */
static int cmp_dim(__isl_keep isl_space *a, __isl_keep isl_space *b, void *user)
{
	return isl_space_dim(b, isl_dim_set) - isl_space_dim(a, isl_dim_set);
}

/* Extract a ppcg_consecutive object that contains the list of
 * accessed arrays that are marked consecutive in "scop".
 * Put higher-dimensional arrays before lower-dimensional arrays.
 */
__isl_give ppcg_consecutive *ppcg_scop_extract_consecutive(
	struct ppcg_scop *scop)
{
	int i;
	isl_ctx *ctx;
	isl_union_map *accesses;
	isl_union_set *arrays;
	isl_space_list *array_list;

	ctx = isl_set_get_ctx(scop->context);
	array_list = isl_space_list_alloc(ctx, 0);
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
			array_list = isl_space_list_free(array_list);
			break;
		}
		if (empty)
			continue;
		array_list = isl_space_list_add(array_list,
					isl_set_get_space(array->extent));
	}
	isl_union_set_free(arrays);

	array_list = isl_space_list_sort(array_list, &cmp_dim, NULL);

	return ppcg_consecutive_from_array_list(array_list);
}

/* Add consecutivity constraints to "sc" based on the consecutive
 * arrays in scop->consecutive and the access relations in "scop".
 * Update scop->consecutive with those access relations and call
 * ppcg_consecutive_add_consecutivity_constraints.
 */
__isl_give isl_schedule_constraints *ppcg_add_consecutivity_constraints(
	__isl_take isl_schedule_constraints *sc, struct ppcg_scop *scop)
{
	ppcg_consecutive *c;

	if (!scop)
		return isl_schedule_constraints_free(sc);
	c = scop->consecutive;
	c = ppcg_consecutive_set_tagged_reads(c,
				isl_union_map_copy(scop->tagged_reads));
	c = ppcg_consecutive_set_tagged_writes(c,
				isl_union_map_copy(scop->tagged_may_writes));
	scop->consecutive = c;
	return ppcg_consecutive_add_consecutivity_constraints(c, sc);
}
