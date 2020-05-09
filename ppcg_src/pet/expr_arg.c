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

#include "context.h"
#include "expr.h"
#include "expr_arg.h"

/* Equate the arguments "pos1" and "pos2" of the access expression "expr".
 *
 * We may assume that "pos1" is smaller than "pos2".
 * We replace all references to the argument at position "pos2"
 * to references to the argument at position "pos1" (leaving all other
 * variables untouched) and then drop argument "pos2".
 */
static __isl_give pet_expr *equate_arg(__isl_take pet_expr *expr, int pos1,
	int pos2)
{
	int in;
	isl_space *space;
	isl_multi_aff *ma;

	if (!expr)
		return NULL;
	if (pos1 == pos2)
		return expr;
	if (pos1 > pos2)
		return equate_arg(expr, pos2, pos1);
	if (pos1 < 0 || pos2 >= expr->n_arg)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"position out of bounds", return pet_expr_free(expr));

	space = isl_multi_pw_aff_get_domain_space(expr->acc.index);
	space = isl_space_unwrap(space);
	in = isl_space_dim(space, isl_dim_in);
	isl_space_free(space);

	pos1 += in;
	pos2 += in;
	space = isl_multi_pw_aff_get_domain_space(expr->acc.index);
	space = isl_space_map_from_set(space);
	ma = isl_multi_aff_identity(space);
	ma = isl_multi_aff_set_aff(ma, pos2, isl_multi_aff_get_aff(ma, pos1));
	expr = pet_expr_access_pullback_multi_aff(expr, ma);
	expr = pet_expr_access_project_out_arg(expr, in, pos2 - in);

	return expr;
}

/* Remove all arguments of the access expression "expr" that are duplicates
 * of earlier arguments.
 */
__isl_give pet_expr *pet_expr_remove_duplicate_args(__isl_take pet_expr *expr)
{
	int i, j;

	if (!expr)
		return NULL;
	if (expr->n_arg < 2)
		return expr;

	for (i = expr->n_arg - 1; i >= 0; --i) {
		for (j = 0; j < i; ++j)
			if (pet_expr_is_equal(expr->args[i], expr->args[j]))
				break;
		if (j >= i)
			continue;
		expr = equate_arg(expr, j, i);
		if (!expr)
			return NULL;
	}

	return expr;
}

/* Insert argument "arg" at position "pos" in the arguments
 * of access expression "expr".
 *
 * Besides actually inserting the argument, we also need to make
 * sure that we adjust the references to the original arguments.
 *
 * If "expr" has no arguments to start with, then its domain is of the form
 *
 *	S[i]
 *
 * otherwise, it is of the form
 *
 *	[S[i] -> [args]]
 *
 * In the first case, we compute the pullback over
 *
 *	[S[i] -> [arg]] -> S[i]
 *
 * In the second case, we compute the pullback over
 *
 *	[S[i] -> [args_before_pos,arg,args_after_pos]] -> [S[i] -> [args]]
 */
__isl_give pet_expr *pet_expr_insert_arg(__isl_take pet_expr *expr, int pos,
	__isl_take pet_expr *arg)
{
	int i, n;
	isl_space *space;
	isl_multi_aff *ma;

	if (!expr || !arg)
		goto error;
	if (expr->type != pet_expr_access)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"not an access pet_expr", goto error);

	n = pet_expr_get_n_arg(expr);
	if (pos < 0 || pos > n)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"position out of bounds", goto error);

	expr = pet_expr_set_n_arg(expr, n + 1);
	for (i = n; i > pos; --i)
		pet_expr_set_arg(expr, i, pet_expr_get_arg(expr, i - 1));
	expr = pet_expr_set_arg(expr, pos, arg);

	space = pet_expr_access_get_domain_space(expr);
	space = isl_space_from_domain(space);
	space = isl_space_add_dims(space, isl_dim_out, n + 1);

	if (n == 0) {
		ma = isl_multi_aff_domain_map(space);
	} else {
		isl_multi_aff *ma2, *proj;

		ma = isl_multi_aff_domain_map(isl_space_copy(space));
		ma2 = isl_multi_aff_range_map(space);
		space = isl_space_range(isl_multi_aff_get_space(ma2));
		proj = isl_multi_aff_project_out_map(space,
						    isl_dim_set, pos, 1);
		ma2 = isl_multi_aff_pullback_multi_aff(proj, ma2);
		ma = isl_multi_aff_range_product(ma, ma2);
	}

	expr = pet_expr_access_pullback_multi_aff(expr, ma);

	return expr;
error:
	pet_expr_free(expr);
	pet_expr_free(arg);
	return NULL;
}

/* Remove the argument at position "pos" in the arguments
 * of access expression "expr", making sure it is not referenced
 * from the index expression.
 * "dim" is the dimension of the iteration domain.
 *
 * Besides actually removing the argument, we also need to make sure that
 * we eliminate any reference from the access relation (if any) and that
 * we adjust the references to the remaining arguments.
 *
 * If "expr" has a single argument, then we compute the pullback over
 *
 *	S[i] -> [S[i] -> [arg]]
 *
 * Otherwise, we compute the pullback over
 *
 *	[S[i] -> [args]] -> [S[i] -> [args_before_pos,args_after_pos]]
 */
__isl_give pet_expr *pet_expr_access_project_out_arg(__isl_take pet_expr *expr,
	int dim, int pos)
{
	int i, n;
	isl_bool involves;
	isl_space *space, *dom, *ran;
	isl_multi_aff *ma1, *ma2;
	enum pet_expr_access_type type;
	isl_map *map;
	isl_union_map *umap;

	expr = pet_expr_cow(expr);
	if (!expr)
		return NULL;
	if (expr->type != pet_expr_access)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"not an access pet_expr", return pet_expr_free(expr));
	n = pet_expr_get_n_arg(expr);
	if (pos < 0 || pos >= n)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"position out of bounds", return pet_expr_free(expr));

	involves = isl_multi_pw_aff_involves_dims(expr->acc.index,
					isl_dim_in, dim + pos, 1);
	if (involves < 0)
		return pet_expr_free(expr);
	if (involves)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"cannot project out", return pet_expr_free(expr));
	space = isl_multi_pw_aff_get_domain_space(expr->acc.index);
	map = isl_map_identity(isl_space_map_from_set(space));
	map = isl_map_eliminate(map, isl_dim_out, dim + pos, 1);
	umap = isl_union_map_from_map(map);
	for (type = pet_expr_access_begin; type < pet_expr_access_end; ++type) {
		if (!expr->acc.access[type])
			continue;
		expr->acc.access[type] =
		    isl_union_map_apply_domain(expr->acc.access[type],
						isl_union_map_copy(umap));
		if (!expr->acc.access[type])
			break;
	}
	isl_union_map_free(umap);
	if (!expr->acc.index || type < pet_expr_access_end)
		return pet_expr_free(expr);

	space = isl_multi_pw_aff_get_domain_space(expr->acc.index);
	space = isl_space_unwrap(space);
	dom = isl_space_map_from_set(isl_space_domain(isl_space_copy(space)));
	ma1 = isl_multi_aff_identity(dom);
	if (n == 1) {
		ma2 = isl_multi_aff_zero(space);
		ma1 = isl_multi_aff_range_product(ma1, ma2);
	} else {
		ran = isl_space_map_from_set(isl_space_range(space));
		ma2 = isl_multi_aff_identity(ran);
		ma2 = isl_multi_aff_drop_dims(ma2, isl_dim_in, pos, 1);
		ma1 = isl_multi_aff_product(ma1, ma2);
	}

	expr = pet_expr_access_pullback_multi_aff(expr, ma1);
	if (!expr)
		return NULL;
	pet_expr_free(expr->args[pos]);
	for (i = pos; i + 1 < n; ++i)
		expr->args[i] = expr->args[i + 1];
	expr->n_arg = n - 1;

	return expr;
}

/* Plug in "value" for the argument at position "pos" of "expr".
 *
 * The input "value" is of the form
 *
 *	S[i] -> [value(i)]
 *
 * while the index expression of "expr" has domain
 *
 *	[S[i] -> [args]]
 *
 * We therefore first pullback "value" to this domain, resulting in
 *
 *	[S[i] -> [args]] -> [value(i)]
 *
 * Then we compute the pullback of "expr" over
 *
 *	[S[i] -> [args]] -> [S[i] -> [args_before_pos,value(i),args_after_pos]]
 *
 * and drop the now redundant argument at position "pos".
 */
static __isl_give pet_expr *plug_in(__isl_take pet_expr *expr, int pos,
	__isl_take isl_pw_aff *value)
{
	int n_in;
	isl_space *space;
	isl_multi_aff *ma;
	isl_multi_pw_aff *mpa;

	space = isl_multi_pw_aff_get_space(expr->acc.index);
	space = isl_space_unwrap(isl_space_domain(space));
	n_in = isl_space_dim(space, isl_dim_in);
	ma = isl_multi_aff_domain_map(space);
	value = isl_pw_aff_pullback_multi_aff(value, ma);

	space = isl_multi_pw_aff_get_space(expr->acc.index);
	space = isl_space_map_from_set(isl_space_domain(space));
	mpa = isl_multi_pw_aff_identity(space);
	mpa = isl_multi_pw_aff_set_pw_aff(mpa, n_in + pos, value);

	expr = pet_expr_access_pullback_multi_pw_aff(expr, mpa);
	expr = pet_expr_access_project_out_arg(expr, n_in, pos);

	return expr;
}

/* Given that the argument of "expr" at position "pos" is a sum
 * of two expressions, replace references to this argument by the sum
 * of references to the two expressions.
 * "dim" is the dimension of the iteration domain.
 *
 * That is, replace
 *
 *	[S[i] -> [args]] -> [f(i,args_before_pos,arg_pos,args_after_pos)]
 *
 * by
 *
 *	[S[i] -> [args_before_pos,arg0,arg1,args_after_pos]] ->
 *		[f(i, args_before_pos, arg0 + arg1, args_after_pos)]
 *
 * where arg0 and arg1 refer to the arguments of the sum expression
 * that the original arg_pos referred to.
 *
 * We introduce (an unreferenced) arg1 and replace arg_pos by arg0
 * in the arguments and then we compute the pullback over
 *
 *	[S[i] -> [args_before_pos,arg0,arg1,args_after_pos]] ->
 *		[S[i] -> [args_before_pos,arg0+arg1,arg1,args_after_pos]]
 */
static __isl_give pet_expr *splice_sum(__isl_take pet_expr *expr, int dim,
	int pos)
{
	isl_space *space;
	pet_expr *arg;
	isl_multi_aff *ma;
	isl_aff *aff1, *aff2;

	arg = expr->args[pos];
	expr = pet_expr_insert_arg(expr, pos + 1, pet_expr_get_arg(arg, 1));
	expr = pet_expr_set_arg(expr, pos, pet_expr_get_arg(arg, 0));
	if (!expr)
		return NULL;

	space = isl_multi_pw_aff_get_space(expr->acc.index);
	space = isl_space_map_from_set(isl_space_domain(space));
	ma = isl_multi_aff_identity(space);
	aff1 = isl_multi_aff_get_aff(ma, dim + pos);
	aff2 = isl_multi_aff_get_aff(ma, dim + pos + 1);
	aff1 = isl_aff_add(aff1, aff2);
	ma = isl_multi_aff_set_aff(ma, dim + pos, aff1);

	expr = pet_expr_access_pullback_multi_aff(expr, ma);

	return expr;
}

/* Try and integrate the arguments of "expr" into the index expression
 * of "expr" by trying to convert the arguments to affine expressions.
 * "pc" is the context in which the affine expressions are created.
 *
 * For example, given an access expression with index expression
 *
 *	[S[i] -> [arg0]] -> A[arg0]
 *
 * where the first argument is itself an access to a variable "i"
 * that is assigned the value
 *
 *	S[i] -> [i]
 *
 * by "pc", this value is plugged into
 * the index expression of "expr", resulting in
 *
 *	[i] -> { S[] -> A[i] }
 *	S[i] -> A[i]
 *
 *
 * In particular, we first remove duplicate arguments so that we
 * only need to convert a given expression once.
 *
 * Then we try and convert the arguments to affine expressions and
 * (if successful) we plug them into the index expression.
 *
 * Occasionally, we may be unable to convert an entire argument, while
 * we could convert a sub-argument.  In particular, this may happen
 * if the top-level argument is an addition of two expressions
 * of which only one can be converted to an affine expression.
 * We therefore replace a reference to a "+" argument by the sum
 * of references to the summands.
 */
__isl_give pet_expr *pet_expr_access_plug_in_args(__isl_take pet_expr *expr,
	__isl_keep pet_context *pc)
{
	int i, n;

	expr = pet_expr_remove_duplicate_args(expr);
	if (!expr)
		return NULL;
	if (expr->type != pet_expr_access)
		isl_die(pet_expr_get_ctx(expr), isl_error_invalid,
			"not an access pet_expr", return pet_expr_free(expr));

	n = pet_expr_get_n_arg(expr);
	if (n == 0)
		return expr;

	for (i = n - 1; expr && i >= 0; --i) {
		isl_pw_aff *pa;
		pet_expr *arg = expr->args[i];

		pa = pet_expr_extract_affine(arg, pc);
		if (!pa)
			return pet_expr_free(expr);
		if (!isl_pw_aff_involves_nan(pa)) {
			expr = plug_in(expr, i, pa);
			continue;
		}
		isl_pw_aff_free(pa);

		if (pet_expr_get_type(arg) == pet_expr_op &&
		    pet_expr_op_get_type(arg) == pet_op_add) {
			int dim = pet_context_dim(pc);
			expr = splice_sum(expr, dim, i);
			i += 2;
		}
	}

	return expr;
}

/* A wrapper around pet_expr_access_plug_in_args for use
 * as a pet_expr_map_access callback.
 */
static __isl_give pet_expr *plug_in_args(__isl_take pet_expr *expr, void *user)
{
	struct pet_context *pc = user;
	return pet_expr_access_plug_in_args(expr, pc);
}

/* For each access subexpression of "expr", try and integrate its arguments in
 * its index expression by trying to convert the arguments
 * to affine expressions.
 * "pc" is the context in which the affine expressions are created.
 */
__isl_give pet_expr *pet_expr_plug_in_args(__isl_take pet_expr *expr,
	__isl_keep pet_context *pc)
{
	return pet_expr_map_access(expr, &plug_in_args, pc);
}
