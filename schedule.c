/*
 * Copyright 2010-2011 INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
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

/* Construct a map that maps a domain of dimensionality "len"
 * to another domain of the same dimensionality such that
 * coordinate "first" of the range is equal to the sum of the "wave_len"
 * coordinates starting at "first" in the domain.
 * The remaining coordinates in the range are equal to the corresponding ones
 * in the domain.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *wavefront(__isl_take isl_space *dim, int len,
        int first, int wave_len)
{
    int i;
    isl_int v;
    isl_basic_map *bmap;
    isl_constraint *c;

    isl_int_init(v);

    dim = isl_space_add_dims(dim, isl_dim_in, len);
    dim = isl_space_add_dims(dim, isl_dim_out, len);
    bmap = isl_basic_map_universe(isl_space_copy(dim));

    for (i = 0; i < len; ++i) {
        if (i == first)
            continue;

        c = isl_equality_alloc(isl_space_copy(dim));
        isl_int_set_si(v, -1);
        isl_constraint_set_coefficient(c, isl_dim_in, i, v);
        isl_int_set_si(v, 1);
        isl_constraint_set_coefficient(c, isl_dim_out, i, v);
        bmap = isl_basic_map_add_constraint(bmap, c);
    }

    c = isl_equality_alloc(isl_space_copy(dim));
    isl_int_set_si(v, -1);
    for (i = 0; i < wave_len; ++i)
        isl_constraint_set_coefficient(c, isl_dim_in, first + i, v);
    isl_int_set_si(v, 1);
    isl_constraint_set_coefficient(c, isl_dim_out, first, v);
    bmap = isl_basic_map_add_constraint(bmap, c);

    isl_space_free(dim);
    isl_int_clear(v);

    return isl_map_from_basic_map(bmap);
}

/* Construct a map from a len-dimensional domain to
 * a (len-n)-dimensional domain that projects out the n coordinates
 * starting at first.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *project_out(__isl_take isl_space *dim,
    int len, int first, int n)
{
    int i, j;
    isl_constraint *c;
    isl_basic_map *bmap;
    isl_int v;

    isl_int_init(v);

    dim = isl_space_add_dims(dim, isl_dim_in, len);
    dim = isl_space_add_dims(dim, isl_dim_out, len - n);
    bmap = isl_basic_map_universe(isl_space_copy(dim));

    for (i = 0, j = 0; i < len; ++i) {
        if (i >= first && i < first + n)
            continue;
        c = isl_equality_alloc(isl_space_copy(dim));
        isl_int_set_si(v, -1);
        isl_constraint_set_coefficient(c, isl_dim_in, i, v);
        isl_int_set_si(v, 1);
        isl_constraint_set_coefficient(c, isl_dim_out, j, v);
        bmap = isl_basic_map_add_constraint(bmap, c);
        ++j;
    }
    isl_space_free(dim);

    isl_int_clear(v);

    return isl_map_from_basic_map(bmap);
}

/* Construct a projection that maps a src_len dimensional domain
 * to its first dst_len coordinates.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *projection(__isl_take isl_space *dim,
    int src_len, int dst_len)
{
    return project_out(dim, src_len, dst_len, src_len - dst_len);
}

/* Extend "set" with unconstrained coordinates to a total length of "dst_len".
 */
__isl_give isl_set *extend(__isl_take isl_set *set, int dst_len)
{
    int n_set;
    isl_space *dim;
    isl_map *map;

    dim = isl_set_get_space(set);
    n_set = isl_space_dim(dim, isl_dim_set);
    dim = isl_space_drop_dims(dim, isl_dim_set, 0, n_set);
    map = projection(dim, dst_len, n_set);
    map = isl_map_reverse(map);

    return isl_set_apply(set, map);
}
