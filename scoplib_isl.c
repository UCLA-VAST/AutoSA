/*
 * Copyright 2010      INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include "scoplib_isl.h"

/* Set the dimension names of type "type" according to the elements
 * in the array "names".
 */
__isl_give isl_dim *set_dim_names(__isl_take isl_dim *dim,
        enum isl_dim_type type, char **names)
{
	int i;

	for (i = 0; i < isl_dim_size(dim, type); ++i)
		dim = isl_dim_set_name(dim, type, i, names[i]);

	return dim;
}


/* Convert a scoplib_matrix_p containing the constraints of a domain
 * to an isl_set.
 */
__isl_give isl_set *scoplib_matrix_to_isl_set(scoplib_matrix_p matrix,
        __isl_take isl_dim *dim)
{
    int i, j;
    int n_eq = 0, n_ineq = 0;
    isl_ctx *ctx;
    isl_mat *eq, *ineq;
    isl_int v;
    isl_basic_set *bset;

    isl_int_init(v);

    ctx = isl_dim_get_ctx(dim);

    for (i = 0; i < matrix->NbRows; ++i)
        if (SCOPVAL_zero_p(matrix->p[i][0]))
            n_eq++;
        else
            n_ineq++;

    eq = isl_mat_alloc(ctx, n_eq, matrix->NbColumns - 1);
    ineq = isl_mat_alloc(ctx, n_ineq, matrix->NbColumns - 1);

    n_eq = n_ineq = 0;
    for (i = 0; i < matrix->NbRows; ++i) {
        isl_mat **m;
        int row;

        if (SCOPVAL_zero_p(matrix->p[i][0])) {
            m = &eq;
            row = n_eq++;
        } else {
            m = &ineq;
            row = n_ineq++;
        }

        for (j = 0; j < matrix->NbColumns - 1; ++j) {
            int t = SCOPVAL_get_si(matrix->p[i][1 + j]);
            isl_int_set_si(v, t);
            *m = isl_mat_set_element(*m, row, j, v);
        }
    }

    isl_int_clear(v);

    bset = isl_basic_set_from_constraint_matrices(dim, eq, ineq,
                isl_dim_set, isl_dim_div, isl_dim_param, isl_dim_cst);
    return isl_set_from_basic_set(bset);
}


/* Convert a scoplib_matrix_list_p describing a union of domains
 * to an isl_set.
 */
__isl_give isl_set *scoplib_matrix_list_to_isl_set(
        scoplib_matrix_list_p list, __isl_take isl_dim *dim)
{
    isl_set *set;

    set = isl_set_empty(isl_dim_copy(dim));
    for (; list; list = list->next) {
        isl_set *set_i;
        set_i = scoplib_matrix_to_isl_set(list->elt, isl_dim_copy(dim));
        set = isl_set_union(set, set_i);
    }

    isl_dim_free(dim);
    return set;
}


/* Return the number of lines until the next non-zero element
 * in the first column of "access" or until the end of the matrix.
 */
static int access_len(scoplib_matrix_p access, int first)
{
    int i;

    for (i = first + 1; i < access->NbRows; ++i)
        if (!SCOPVAL_zero_p(access->p[i][0]))
            break;

    return i - first;
}


/* Convert an m x (1 + n + 1) scoplib_matrix_p [d A c]
 * to an m x (m + n + 1) isl_mat [-I A c].
 */
static __isl_give isl_mat *extract_equalities(isl_ctx *ctx,
        scoplib_matrix_p matrix, int first, int n)
{
    int i, j;
    int n_col;
    isl_int v;
    isl_mat *eq;

    n_col = matrix->NbColumns;

    isl_int_init(v);
    eq = isl_mat_alloc(ctx, n, n + n_col - 1);

    for (i = 0; i < n; ++i) {
        isl_int_set_si(v, 0);
        for (j = 0; j < n; ++j)
            eq = isl_mat_set_element(eq, i, j, v);
        isl_int_set_si(v, -1);
        eq = isl_mat_set_element(eq, i, i, v);
        for (j = 0; j < n_col - 1; ++j) {
            int t = SCOPVAL_get_si(matrix->p[first + i][1 + j]);
            isl_int_set_si(v, t);
            eq = isl_mat_set_element(eq, i, n + j, v);
        }
    }

    isl_int_clear(v);

    return eq;
}


/* Convert a scoplib_matrix_p describing a series of accesses
 * to an isl_union_map with domain "dom" (in space "D").
 * Each access in "access" has a non-zero integer in the first column
 * of the first row identifying the array being accessed.  The remaining
 * entries of the first column are zero.
 * Let "A" be array identified by the first entry.
 * The remaining columns have the form [B c].
 * Each such access is converted to a map { D[i] -> A[B i + c] } * dom.
 *
 * Note that each access in the input is described by at least one row,
 * which means that there is no way of distinguishing between an access
 * to a scalar and an access to the first element of a 1-dimensional array.
 */
__isl_give isl_union_map *scoplib_access_to_isl_union_map(
        scoplib_matrix_p access, __isl_take isl_set *dom, char **arrays)
{
    int i, len, n_col;
    isl_ctx *ctx;
    isl_dim *dim;
    isl_mat *eq, *ineq;
    isl_union_map *res;

    ctx = isl_set_get_ctx(dom);

    dim = isl_set_get_dim(dom);
    dim = isl_dim_drop(dim, isl_dim_set, 0, isl_dim_size(dim, isl_dim_set));
    res = isl_union_map_empty(dim);

    n_col = access->NbColumns;

    for (i = 0; i < access->NbRows; i += len) {
        isl_basic_map *bmap;
        isl_map *map;
        int arr = SCOPVAL_get_si(access->p[i][0]) - 1;

        len = access_len(access, i);

        dim = isl_set_get_dim(dom);
        dim = isl_dim_from_domain(dim);
        dim = isl_dim_add(dim, isl_dim_out, len);
        dim = isl_dim_set_tuple_name(dim, isl_dim_out, arrays[arr]);

        ineq = isl_mat_alloc(ctx, 0, len + n_col - 1);
        eq = extract_equalities(ctx, access, i, len);

        bmap = isl_basic_map_from_constraint_matrices(dim, eq, ineq,
            isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
        map = isl_map_from_basic_map(bmap);
        map = isl_map_intersect_domain(map, isl_set_copy(dom));
        res = isl_union_map_union(res, isl_union_map_from_map(map));
    }

    isl_set_free(dom);

    return res;
}


/* Convert a scoplib_matrix_p schedule [0 A c] to
 * the isl_map { i -> A i + c } in the space prescribed by "dim".
 */
__isl_give isl_map *scoplib_schedule_to_isl_map(
        scoplib_matrix_p schedule, __isl_take isl_dim *dim)
{
    int n_row, n_col;
    isl_ctx *ctx;
    isl_mat *eq, *ineq;
    isl_basic_map *bmap;

    ctx = isl_dim_get_ctx(dim);
    n_row = schedule->NbRows;
    n_col = schedule->NbColumns;

    ineq = isl_mat_alloc(ctx, 0, n_row + n_col - 1);
    eq = extract_equalities(ctx, schedule, 0, n_row);

    bmap = isl_basic_map_from_constraint_matrices(dim, eq, ineq,
            isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
    return isl_map_from_basic_map(bmap);
}
