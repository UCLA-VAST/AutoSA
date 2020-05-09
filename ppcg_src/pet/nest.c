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

#include <string.h>

#include <isl/id.h>
#include <isl/space.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_map.h>
#include <isl/aff.h>
#include <isl/val.h>

#include "aff.h"
#include "expr.h"
#include "expr_arg.h"
#include "nest.h"
#include "scop.h"
#include "tree.h"

/* A wrapper around pet_expr_free to be used as an isl_id free user function.
 */
static void pet_expr_free_wrap(void *user)
{
	pet_expr_free((pet_expr *) user);
}

/* Create an isl_id that refers to the nested access "expr".
 */
__isl_give isl_id *pet_nested_pet_expr(__isl_take pet_expr *expr)
{
	isl_id *id;

	id = isl_id_alloc(pet_expr_get_ctx(expr), "__pet_expr", expr);
	id = isl_id_set_free_user(id, &pet_expr_free_wrap);

	return id;
}

/* Extract a pet_expr from an isl_id created by pet_nested_pet_expr.
 * Such an isl_id has name "__pet_expr" and
 * the user pointer points to a pet_expr object.
 */
__isl_give pet_expr *pet_nested_extract_expr(__isl_keep isl_id *id)
{
	return pet_expr_copy((pet_expr *) isl_id_get_user(id));
}

/* Does "id" refer to a nested access created by pet_nested_pet_expr?
 */
int pet_nested_in_id(__isl_keep isl_id *id)
{
	const char *name;

	if (!id)
		return 0;
	if (!isl_id_get_user(id))
		return 0;

	name = isl_id_get_name(id);
	return !strcmp(name, "__pet_expr");
}

/* Does parameter "pos" of "space" refer to a nested access?
 */
static int pet_nested_in_space(__isl_keep isl_space *space, int pos)
{
	int nested;
	isl_id *id;

	id = isl_space_get_dim_id(space, isl_dim_param, pos);
	nested = pet_nested_in_id(id);
	isl_id_free(id);

	return nested;
}

/* Does parameter "pos" of "set" refer to a nested access?
 */
int pet_nested_in_set(__isl_keep isl_set *set, int pos)
{
	int nested;
	isl_id *id;

	id = isl_set_get_dim_id(set, isl_dim_param, pos);
	nested = pet_nested_in_id(id);
	isl_id_free(id);

	return nested;
}

/* Does parameter "pos" of "map" refer to a nested access?
 */
int pet_nested_in_map(__isl_keep isl_map *map, int pos)
{
	int nested;
	isl_id *id;

	id = isl_map_get_dim_id(map, isl_dim_param, pos);
	nested = pet_nested_in_id(id);
	isl_id_free(id);

	return nested;
}

/* Does parameter "pos" of "umap" refer to a nested access?
 */
static int pet_nested_in_union_map(__isl_keep isl_union_map *umap, int pos)
{
	int nested;
	isl_id *id;

	id = isl_union_map_get_dim_id(umap, isl_dim_param, pos);
	nested = pet_nested_in_id(id);
	isl_id_free(id);

	return nested;
}

/* Does "space" involve any parameters that refer to nested accesses?
 */
int pet_nested_any_in_space(__isl_keep isl_space *space)
{
	int i;
	int nparam;

	nparam = isl_space_dim(space, isl_dim_param);
	for (i = 0; i < nparam; ++i)
		if (pet_nested_in_space(space, i))
			return 1;

	return 0;
}

/* Does "pa" involve any parameters that refer to nested accesses?
 */
int pet_nested_any_in_pw_aff(__isl_keep isl_pw_aff *pa)
{
	isl_space *space;
	int nested;

	space = isl_pw_aff_get_space(pa);
	nested = pet_nested_any_in_space(space);
	isl_space_free(space);

	return nested;
}

/* How many parameters of "space" refer to nested accesses?
 */
int pet_nested_n_in_space(__isl_keep isl_space *space)
{
	int i, n = 0;
	int nparam;

	nparam = isl_space_dim(space, isl_dim_param);
	for (i = 0; i < nparam; ++i)
		if (pet_nested_in_space(space, i))
			++n;

	return n;
}

/* How many parameters of "map" refer to nested accesses?
 */
int pet_nested_n_in_map(__isl_keep isl_map *map)
{
	isl_space *space;
	int n;

	space = isl_map_get_space(map);
	n = pet_nested_n_in_space(space);
	isl_space_free(space);

	return n;
}

/* How many parameters of "set" refer to nested accesses?
 */
int pet_nested_n_in_set(__isl_keep isl_set *set)
{
	isl_space *space;
	int n;

	space = isl_set_get_space(set);
	n = pet_nested_n_in_space(space);
	isl_space_free(space);

	return n;
}

/* Remove all parameters from "space" that refer to nested accesses.
 */
__isl_give isl_space *pet_nested_remove_from_space(__isl_take isl_space *space)
{
	int i;
	int nparam;

	nparam = isl_space_dim(space, isl_dim_param);
	for (i = nparam - 1; i >= 0; --i)
		if (pet_nested_in_space(space, i))
			space = isl_space_drop_dims(space, isl_dim_param, i, 1);

	return space;
}

/* Remove all parameters from "set" that refer to nested accesses.
 */
__isl_give isl_set *pet_nested_remove_from_set(__isl_take isl_set *set)
{
	int i;
	int nparam;

	nparam = isl_set_dim(set, isl_dim_param);
	for (i = nparam - 1; i >= 0; --i)
		if (pet_nested_in_set(set, i))
			set = isl_set_project_out(set, isl_dim_param, i, 1);

	return set;
}

/* Remove all parameters from "umap" that refer to nested accesses.
 */
static __isl_give isl_union_map *pet_nested_remove_from_union_map(
	__isl_take isl_union_map *umap)
{
	int i;
	int nparam;

	nparam = isl_union_map_dim(umap, isl_dim_param);
	for (i = nparam - 1; i >= 0; --i)
		if (pet_nested_in_union_map(umap, i))
			umap = isl_union_map_project_out(umap,
							isl_dim_param, i, 1);

	return umap;
}

/* Remove all parameters from "mpa" that refer to nested accesses.
 */
static __isl_give isl_multi_pw_aff *pet_nested_remove_from_multi_pw_aff(
	__isl_take isl_multi_pw_aff *mpa)
{
	int i;
	int nparam;
	isl_space *space;

	space = isl_multi_pw_aff_get_space(mpa);
	nparam = isl_space_dim(space, isl_dim_param);
	for (i = nparam - 1; i >= 0; --i) {
		if (!pet_nested_in_space(space, i))
			continue;
		mpa = isl_multi_pw_aff_drop_dims(mpa, isl_dim_param, i, 1);
	}
	isl_space_free(space);

	return mpa;
}

/* Remove all parameters from the index expression and
 * access relations of "expr" that refer to nested accesses.
 */
static __isl_give pet_expr *expr_remove_nested_parameters(
	__isl_take pet_expr *expr, void *user)
{
	enum pet_expr_access_type type;

	expr = pet_expr_cow(expr);
	if (!expr)
		return NULL;

	for (type = pet_expr_access_begin; type < pet_expr_access_end; ++type) {
		if (!expr->acc.access[type])
			continue;
		expr->acc.access[type] =
		    pet_nested_remove_from_union_map(expr->acc.access[type]);
		if (!expr->acc.access[type])
			break;
	}
	expr->acc.index = pet_nested_remove_from_multi_pw_aff(expr->acc.index);
	if (type < pet_expr_access_end || !expr->acc.index)
		return pet_expr_free(expr);

	return expr;
}

/* Remove all nested access parameters from the schedule and all
 * accesses of "stmt".
 * There is no need to remove them from the domain as these parameters
 * have already been removed from the domain when this function is called.
 */
struct pet_stmt *pet_stmt_remove_nested_parameters(struct pet_stmt *stmt)
{
	int i;

	if (!stmt)
		return NULL;
	stmt->body = pet_tree_map_access_expr(stmt->body,
			    &expr_remove_nested_parameters, NULL);
	if (!stmt->body)
		goto error;
	for (i = 0; i < stmt->n_arg; ++i) {
		stmt->args[i] = pet_expr_map_access(stmt->args[i],
			    &expr_remove_nested_parameters, NULL);
		if (!stmt->args[i])
			goto error;
	}

	return stmt;
error:
	pet_stmt_free(stmt);
	return NULL;
}

/* Set *dim to the dimension of the domain of the access expression "expr" and
 * abort the search.
 */
static int set_dim(__isl_keep pet_expr *expr, void *user)
{
	int *dim = user;
	isl_space *space;

	space = pet_expr_access_get_domain_space(expr);
	*dim = isl_space_dim(space, isl_dim_set);
	isl_space_free(space);

	return -1;
}

/* Determine the dimension of the domain of the access expressions in "expr".
 *
 * In particular, return the dimension of the domain of the first access
 * expression in "expr" as all access expressions should have the same
 * domain.
 *
 * If "expr" does not contain any access expressions, then we return 0.
 */
static int pet_expr_domain_dim(__isl_keep pet_expr *expr)
{
	int dim = -1;

	if (pet_expr_foreach_access_expr(expr, &set_dim, &dim) >= 0)
		return 0;

	return dim;
}

/* Embed all access expressions in "expr" in the domain "space".
 * The initial domain of the access expressions
 * is an anonymous domain of a dimension that may be lower
 * than the dimension of "space".
 * We may therefore need to introduce extra dimensions as well as
 * (potentially) the name of "space".
 */
static __isl_give pet_expr *embed(__isl_take pet_expr *expr,
	__isl_keep isl_space *space)
{
	int n;
	isl_multi_pw_aff *mpa;

	n = pet_expr_domain_dim(expr);
	if (n < 0)
		return pet_expr_free(expr);

	space = isl_space_copy(space);
	mpa = isl_multi_pw_aff_from_multi_aff(pet_prefix_projection(space, n));
	expr = pet_expr_update_domain(expr, mpa);

	return expr;
}

/* For each nested access parameter in "space",
 * construct a corresponding pet_expr, place it in args and
 * record its position in "param2pos".
 * The constructed pet_expr objects are embedded in "space"
 * (with the nested access parameters removed).
 * "n_arg" is the number of elements that are already in args.
 * The position recorded in "param2pos" takes this number into account.
 * If the pet_expr corresponding to a parameter is identical to
 * the pet_expr corresponding to an earlier parameter, then these two
 * parameters are made to refer to the same element in args.
 *
 * Return the final number of elements in args or -1 if an error has occurred.
 */
int pet_extract_nested_from_space(__isl_keep isl_space *space,
	int n_arg, __isl_give pet_expr **args, int *param2pos)
{
	int i, nparam;
	isl_space *domain;

	domain = isl_space_copy(space);
	domain = pet_nested_remove_from_space(domain);
	nparam = isl_space_dim(space, isl_dim_param);
	for (i = 0; i < nparam; ++i) {
		int j;
		isl_id *id = isl_space_get_dim_id(space, isl_dim_param, i);

		if (!pet_nested_in_id(id)) {
			isl_id_free(id);
			continue;
		}

		args[n_arg] = embed(pet_nested_extract_expr(id), domain);
		isl_id_free(id);
		if (!args[n_arg])
			return -1;

		for (j = 0; j < n_arg; ++j)
			if (pet_expr_is_equal(args[j], args[n_arg]))
				break;

		if (j < n_arg) {
			pet_expr_free(args[n_arg]);
			args[n_arg] = NULL;
			param2pos[i] = j;
		} else
			param2pos[i] = n_arg++;
	}
	isl_space_free(domain);

	return n_arg;
}

/* For each nested access parameter in the access relations in "expr",
 * construct a corresponding pet_expr, append it to the arguments of "expr"
 * and record its position in "param2pos" (relative to the initial
 * number of arguments).
 * n is the number of nested access parameters.
 */
__isl_give pet_expr *pet_expr_extract_nested(__isl_take pet_expr *expr, int n,
	int *param2pos)
{
	isl_ctx *ctx;
	isl_space *space;
	int i, n_arg;
	pet_expr **args;

	ctx = pet_expr_get_ctx(expr);
	args = isl_calloc_array(ctx, pet_expr *, n);
	if (!args)
		return pet_expr_free(expr);

	n_arg = pet_expr_get_n_arg(expr);
	space = pet_expr_access_get_domain_space(expr);
	n = pet_extract_nested_from_space(space, 0, args, param2pos);
	isl_space_free(space);

	if (n < 0)
		expr = pet_expr_free(expr);
	else
		expr = pet_expr_set_n_arg(expr, n_arg + n);

	for (i = 0; i < n; ++i)
		expr = pet_expr_set_arg(expr, n_arg + i, args[i]);
	free(args);

	return expr;
}

/* Mark self dependences among the arguments of "expr" starting at "first".
 * These arguments have already been added to the list of arguments
 * but are not yet referenced directly from the index expression.
 * Instead, they are still referenced through parameters encoding
 * nested accesses.
 *
 * In particular, if "expr" is a read access, then check the arguments
 * starting at "first" to see if "expr" accesses a subset of
 * the elements accessed by the argument, or under more restrictive conditions.
 * If so, then this nested access can be removed from the constraints
 * governing the outer access.  There is no point in restricting
 * accesses to an array if in order to evaluate the restriction,
 * we have to access the same elements (or more).
 *
 * Rather than removing the argument at this point (which would
 * complicate the resolution of the other nested accesses), we simply
 * mark it here by replacing it by a NaN pet_expr.
 * These NaNs are then later removed in remove_marked_self_dependences.
 */
static __isl_give pet_expr *mark_self_dependences(__isl_take pet_expr *expr,
	int first)
{
	int i, n;

	if (pet_expr_access_is_write(expr))
		return expr;

	n = pet_expr_get_n_arg(expr);
	for (i = first; i < n; ++i) {
		int mark;
		pet_expr *arg;

		arg = pet_expr_get_arg(expr, i);
		mark = pet_expr_is_sub_access(expr, arg, first);
		pet_expr_free(arg);
		if (mark < 0)
			return pet_expr_free(expr);
		if (!mark)
			continue;

		arg = pet_expr_new_int(isl_val_nan(pet_expr_get_ctx(expr)));
		expr = pet_expr_set_arg(expr, i, arg);
	}

	return expr;
}

/* Is "expr" a NaN integer expression?
 */
static int expr_is_nan(__isl_keep pet_expr *expr)
{
	isl_val *v;
	int is_nan;

	if (pet_expr_get_type(expr) != pet_expr_int)
		return 0;

	v = pet_expr_int_get_val(expr);
	is_nan = isl_val_is_nan(v);
	isl_val_free(v);

	return is_nan;
}

/* Check if we have marked any self dependences (as NaNs)
 * in mark_self_dependences and remove them here.
 * It is safe to project them out since these arguments
 * can at most be referenced from the condition of the access relation,
 * but do not appear in the index expression.
 * "dim" is the dimension of the iteration domain.
 */
static __isl_give pet_expr *remove_marked_self_dependences(
	__isl_take pet_expr *expr, int dim, int first)
{
	int i, n;

	n = pet_expr_get_n_arg(expr);
	for (i = n - 1; i >= first; --i) {
		int is_nan;
		pet_expr *arg;

		arg = pet_expr_get_arg(expr, i);
		is_nan = expr_is_nan(arg);
		pet_expr_free(arg);
		if (!is_nan)
			continue;
		expr = pet_expr_access_project_out_arg(expr, dim, i);
	}

	return expr;
}

/* Look for parameters in any access relation in "expr" that
 * refer to nested accesses.  In particular, these are
 * parameters with name "__pet_expr".
 *
 * If there are any such parameters, then the domain of the index
 * expression and the access relation, which is either "domain" or
 * [domain -> [a_1,...,a_m]] at this point, is replaced by
 * [domain -> [t_1,...,t_n]] or [domain -> [a_1,...,a_m,t_1,...,t_n]],
 * with m the original number of arguments (n_arg) and
 * n the number of these parameters
 * (after identifying identical nested accesses).
 *
 * This transformation is performed in several steps.
 * We first extract the arguments in pet_expr_extract_nested.
 * param2pos maps the original parameter position to the position
 * of the argument beyond the initial (n_arg) number of arguments.
 * Then we move these parameters to input dimensions.
 * t2pos maps the positions of these temporary input dimensions
 * to the positions of the corresponding arguments inside the space
 * [domain -> [t_1,...,t_n]].
 * Finally, we express these temporary dimensions in terms of the domain
 * [domain -> [a_1,...,a_m,t_1,...,t_n]] and precompose index expression and
 * access relations with this function.
 */
__isl_give pet_expr *pet_expr_resolve_nested(__isl_take pet_expr *expr,
	__isl_keep isl_space *domain)
{
	int i, n, n_arg, dim, n_in;
	int nparam;
	isl_ctx *ctx;
	isl_space *space;
	isl_local_space *ls;
	isl_aff *aff;
	isl_multi_aff *ma;
	int *param2pos;
	int *t2pos;

	if (!expr)
		return expr;

	n_arg = pet_expr_get_n_arg(expr);
	for (i = 0; i < n_arg; ++i) {
		pet_expr *arg;
		arg = pet_expr_get_arg(expr, i);
		arg = pet_expr_resolve_nested(arg, domain);
		expr = pet_expr_set_arg(expr, i, arg);
	}

	if (pet_expr_get_type(expr) != pet_expr_access)
		return expr;

	dim = isl_space_dim(domain, isl_dim_set);
	n_in = dim + n_arg;

	space = pet_expr_access_get_parameter_space(expr);
	n = pet_nested_n_in_space(space);
	isl_space_free(space);
	if (n == 0)
		return expr;

	expr = pet_expr_access_align_params(expr);
	if (!expr)
		return NULL;

	space = pet_expr_access_get_parameter_space(expr);
	nparam = isl_space_dim(space, isl_dim_param);
	isl_space_free(space);

	ctx = pet_expr_get_ctx(expr);

	param2pos = isl_alloc_array(ctx, int, nparam);
	t2pos = isl_alloc_array(ctx, int, n);
	if (!param2pos)
		goto error;
	expr = pet_expr_extract_nested(expr, n, param2pos);
	expr = mark_self_dependences(expr, n_arg);
	if (!expr)
		goto error;

	n = 0;
	space = pet_expr_access_get_parameter_space(expr);
	nparam = isl_space_dim(space, isl_dim_param);
	for (i = nparam - 1; i >= 0; --i) {
		isl_id *id = isl_space_get_dim_id(space, isl_dim_param, i);
		if (!pet_nested_in_id(id)) {
			isl_id_free(id);
			continue;
		}

		expr = pet_expr_access_move_dims(expr,
				    isl_dim_in, n_in + n, isl_dim_param, i, 1);
		t2pos[n] = n_in + param2pos[i];
		n++;

		isl_id_free(id);
	}
	isl_space_free(space);

	space = isl_space_copy(domain);
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out,
					pet_expr_get_n_arg(expr));
	space = isl_space_wrap(space);
	ls = isl_local_space_from_space(isl_space_copy(space));
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, n_in + n);
	ma = isl_multi_aff_zero(space);

	for (i = 0; i < n_in; ++i) {
		aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
					    isl_dim_set, i);
		ma = isl_multi_aff_set_aff(ma, i, aff);
	}
	for (i = 0; i < n; ++i) {
		aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
					    isl_dim_set, t2pos[i]);
		ma = isl_multi_aff_set_aff(ma, n_in + i, aff);
	}
	isl_local_space_free(ls);

	expr = pet_expr_access_pullback_multi_aff(expr, ma);

	expr = remove_marked_self_dependences(expr, dim, n_arg);

	free(t2pos);
	free(param2pos);
	return expr;
error:
	free(t2pos);
	free(param2pos);
	return pet_expr_free(expr);
}

/* Wrapper around pet_expr_resolve_nested
 * for use as a callback to pet_tree_map_expr.
 */
static __isl_give pet_expr *resolve_nested(__isl_take pet_expr *expr,
	void *user)
{
	isl_space *space = user;

	return pet_expr_resolve_nested(expr, space);
}

/* Call pet_expr_resolve_nested on each of the expressions in "tree".
 */
__isl_give pet_tree *pet_tree_resolve_nested(__isl_take pet_tree *tree,
	__isl_keep isl_space *space)
{
	return pet_tree_map_expr(tree, &resolve_nested, space);
}

/* For each nested access parameter in the domain of "stmt",
 * construct a corresponding pet_expr, place it before the original
 * elements in stmt->args and record its position in "param2pos".
 * n is the number of nested access parameters.
 */
struct pet_stmt *pet_stmt_extract_nested(struct pet_stmt *stmt, int n,
	int *param2pos)
{
	int i;
	isl_ctx *ctx;
	isl_space *space;
	int n_arg;
	pet_expr **args;

	ctx = isl_set_get_ctx(stmt->domain);

	n_arg = stmt->n_arg;
	args = isl_calloc_array(ctx, pet_expr *, n + n_arg);
	if (!args)
		goto error;

	space = isl_set_get_space(stmt->domain);
	if (isl_space_is_wrapping(space))
		space = isl_space_domain(isl_space_unwrap(space));
	n_arg = pet_extract_nested_from_space(space, 0, args, param2pos);
	isl_space_free(space);

	if (n_arg < 0)
		goto error;

	for (i = 0; i < stmt->n_arg; ++i)
		args[n_arg + i] = stmt->args[i];
	free(stmt->args);
	stmt->args = args;
	stmt->n_arg += n_arg;

	return stmt;
error:
	if (args) {
		for (i = 0; i < n; ++i)
			pet_expr_free(args[i]);
		free(args);
	}
	pet_stmt_free(stmt);
	return NULL;
}

/* Check whether any of the arguments i of "stmt" starting at position "n"
 * is equal to one of the first "n" arguments j.
 * If so, combine the constraints on arguments i and j and remove
 * argument i.
 */
static struct pet_stmt *remove_duplicate_arguments(struct pet_stmt *stmt, int n)
{
	int i, j;
	isl_map *map;

	if (!stmt)
		return NULL;
	if (n == 0)
		return stmt;
	if (n == stmt->n_arg)
		return stmt;

	map = isl_set_unwrap(stmt->domain);

	for (i = stmt->n_arg - 1; i >= n; --i) {
		for (j = 0; j < n; ++j)
			if (pet_expr_is_equal(stmt->args[i], stmt->args[j]))
				break;
		if (j >= n)
			continue;

		map = isl_map_equate(map, isl_dim_out, i, isl_dim_out, j);
		map = isl_map_project_out(map, isl_dim_out, i, 1);

		pet_expr_free(stmt->args[i]);
		for (j = i; j + 1 < stmt->n_arg; ++j)
			stmt->args[j] = stmt->args[j + 1];
		stmt->n_arg--;
	}

	stmt->domain = isl_map_wrap(map);
	if (!stmt->domain)
		goto error;
	return stmt;
error:
	pet_stmt_free(stmt);
	return NULL;
}

/* Look for parameters in the iteration domain of "stmt" that
 * refer to nested accesses.  In particular, these are
 * parameters with name "__pet_expr".
 *
 * If there are any such parameters, then as many extra variables
 * (after identifying identical nested accesses) are inserted in the
 * range of the map wrapped inside the domain, before the original variables.
 * If the original domain is not a wrapped map, then a new wrapped
 * map is created with zero output dimensions.
 * The parameters are then equated to the corresponding output dimensions
 * and subsequently projected out, from the iteration domain,
 * the schedule and the access relations.
 * For each of the output dimensions, a corresponding argument
 * expression is inserted, embedded in the current iteration domain.
 * param2pos maps the position of the parameter to the position
 * of the corresponding output dimension in the wrapped map.
 */
struct pet_stmt *pet_stmt_resolve_nested(struct pet_stmt *stmt)
{
	int i, n;
	int nparam;
	unsigned n_arg;
	isl_ctx *ctx;
	isl_map *map;
	int *param2pos;

	if (!stmt)
		return NULL;

	n = pet_nested_n_in_set(stmt->domain);
	if (n == 0)
		return stmt;

	ctx = isl_set_get_ctx(stmt->domain);

	n_arg = stmt->n_arg;
	nparam = isl_set_dim(stmt->domain, isl_dim_param);
	param2pos = isl_alloc_array(ctx, int, nparam);
	stmt = pet_stmt_extract_nested(stmt, n, param2pos);
	if (!stmt) {
		free(param2pos);
		return NULL;
	}

	n = stmt->n_arg - n_arg;
	if (isl_set_is_wrapping(stmt->domain))
		map = isl_set_unwrap(stmt->domain);
	else
		map = isl_map_from_domain(stmt->domain);
	map = isl_map_insert_dims(map, isl_dim_out, 0, n);

	for (i = nparam - 1; i >= 0; --i) {
		isl_id *id;

		if (!pet_nested_in_map(map, i))
			continue;

		id = pet_expr_access_get_id(stmt->args[param2pos[i]]);
		map = isl_map_set_dim_id(map, isl_dim_out, param2pos[i], id);
		map = isl_map_equate(map, isl_dim_param, i, isl_dim_out,
					param2pos[i]);
		map = isl_map_project_out(map, isl_dim_param, i, 1);
	}

	stmt->domain = isl_map_wrap(map);

	stmt = pet_stmt_remove_nested_parameters(stmt);
	stmt = remove_duplicate_arguments(stmt, n);

	free(param2pos);
	return stmt;
}

/* For each statement in "scop", move the parameters that correspond
 * to nested access into the ranges of the domains and create
 * corresponding argument expressions.
 */
struct pet_scop *pet_scop_resolve_nested(struct pet_scop *scop)
{
	int i;

	if (!scop)
		return NULL;

	for (i = 0; i < scop->n_stmt; ++i) {
		scop->stmts[i] = pet_stmt_resolve_nested(scop->stmts[i]);
		if (!scop->stmts[i])
			return pet_scop_free(scop);
	}

	return scop;
}
