/*
 * Copyright 2010-2011 INRIA Saclay
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include <isl/set.h>
#include <isl/map.h>
#include <isl/constraint.h>

#include "schedule.h"

/* Construct a map from a len-dimensional domain to
 * a (len-n)-dimensional domain that projects out the n coordinates
 * starting at first.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *project_out(__isl_take isl_space *dim,
    int len, int first, int n)
{
    int i, j;
    isl_basic_map *bmap;

    dim = isl_space_add_dims(dim, isl_dim_in, len);
    dim = isl_space_add_dims(dim, isl_dim_out, len - n);
    bmap = isl_basic_map_universe(dim);

    for (i = 0, j = 0; i < len; ++i) {
        if (i >= first && i < first + n)
            continue;
	bmap = isl_basic_map_equate(bmap, isl_dim_in, i, isl_dim_out, j);
        ++j;
    }

    return isl_map_from_basic_map(bmap);
}

/* Add parameters with identifiers "ids" to "set".
 */
static __isl_give isl_set *add_params(__isl_take isl_set *set,
	__isl_keep isl_id_list *ids)
{
	int i, n;
	unsigned nparam;

	n = isl_id_list_n_id(ids);

	nparam = isl_set_dim(set, isl_dim_param);
	set = isl_set_add_dims(set, isl_dim_param, n);

	for (i = 0; i < n; ++i) {
		isl_id *id;

		id = isl_id_list_get_id(ids, i);
		set = isl_set_set_dim_id(set, isl_dim_param, nparam + i, id);
	}

	return set;
}

/* Equate the dimensions of "set" starting at "first" to
 * freshly created parameters with identifiers "ids".
 * The number of equated dimensions is equal to the number of elements in "ids".
 */
static __isl_give isl_set *parametrize(__isl_take isl_set *set,
	int first, __isl_keep isl_id_list *ids)
{
	int i, n;
	unsigned nparam;

	nparam = isl_set_dim(set, isl_dim_param);

	set = add_params(set, ids);

	n = isl_id_list_n_id(ids);
	for (i = 0; i < n; ++i)
		set = isl_set_equate(set, isl_dim_param, nparam + i,
					isl_dim_set, first + i);

	return set;
}

/* Given a parameter space "space", create a set of dimension "len"
 * of which the dimensions starting at "first" are equated to
 * freshly created parameters with identifiers "ids".
 */
__isl_give isl_set *parametrization(__isl_take isl_space *space,
	int len, int first, __isl_keep isl_id_list *ids)
{
	isl_set *set;

	space = isl_space_set_from_params(space);
	space = isl_space_add_dims(space, isl_dim_set, len);
	set = isl_set_universe(space);

	return parametrize(set, first, ids);
}
