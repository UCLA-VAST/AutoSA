/*
 * Copyright 2011 Leiden University. All rights reserved.
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

#include <isl/id.h>
#include <isl/space.h>
#include <isl/local_space.h>
#include <isl/aff.h>
#include <isl/ast.h>
#include <isl/ast_build.h>
#include <isl/printer.h>
#include <isl/val.h>
#include <pet.h>
#include "expr.h"
#include "print.h"
#include "scop.h"

/* Return the dimension of the domain of the embedded map
 * in the domain of "mpa".
 */
static int domain_domain_dim(__isl_keep isl_multi_pw_aff *mpa)
{
	int dim;
	isl_space *space;

	space = isl_multi_pw_aff_get_space(mpa);
	space = isl_space_unwrap(isl_space_domain(space));
	dim = isl_space_dim(space, isl_dim_in);
	isl_space_free(space);

	return dim;
}

/* Given an access expression, check if any of the arguments
 * for which an isl_ast_expr would be constructed by
 * pet_expr_build_nested_ast_exprs are not themselves access expressions.
 * If so, set *found and abort the search.
 */
static int depends_on_expressions(__isl_keep pet_expr *expr, void *user)
{
	int i, dim;
	int *found = user;

	if (expr->n_arg == 0)
		return 0;

	dim = domain_domain_dim(expr->acc.index);

	for (i = 0; i < expr->n_arg; ++i) {
		if (!isl_multi_pw_aff_involves_dims(expr->acc.index,
						    isl_dim_in, dim + i, 1))
			continue;
		if (expr->args[i]->type != pet_expr_access) {
			*found = 1;
			return -1;
		}
	}

	return 0;
}

/* pet_stmt_build_ast_exprs is currently limited to only handle
 * some forms of data dependent accesses.
 * If pet_stmt_can_build_ast_exprs returns 1, then pet_stmt_build_ast_exprs
 * can safely be called on "stmt".
 */
int pet_stmt_can_build_ast_exprs(struct pet_stmt *stmt)
{
	int r;
	int found = 0;

	if (!stmt)
		return -1;

	r = pet_tree_foreach_access_expr(stmt->body,
					&depends_on_expressions, &found);
	if (r < 0 && !found)
		return -1;

	return !found;
}

/* pet_stmt_build_ast_exprs is currently limited to only handle
 * some forms of data dependent accesses.
 * If pet_scop_can_build_ast_exprs returns 1, then pet_stmt_build_ast_exprs
 * can safely be called on all statements in the scop.
 */
int pet_scop_can_build_ast_exprs(struct pet_scop *scop)
{
	int i;

	if (!scop)
		return -1;

	for (i = 0; i < scop->n_stmt; ++i) {
		int ok = pet_stmt_can_build_ast_exprs(scop->stmts[i]);
		if (ok < 0 || !ok)
			return ok;
	}

	return 1;
}

/* Internal data structure for pet_stmt_build_ast_exprs.
 *
 * "build" is used to construct an AST expression from an index expression.
 * "fn_index" is used to transform the index expression prior to
 *	the construction of the AST expression.
 * "fn_expr" is used to transform the constructed AST expression.
 * "ref2expr" collects the results.
 */
struct pet_build_ast_expr_data {
	isl_ast_build *build;
	__isl_give isl_multi_pw_aff *(*fn_index)(
		__isl_take isl_multi_pw_aff *mpa, __isl_keep isl_id *id,
		void *user);
	void *user_index;
	__isl_give isl_ast_expr *(*fn_expr)(__isl_take isl_ast_expr *expr,
		__isl_keep isl_id *id, void *user);
	void *user_expr;
	isl_id_to_ast_expr *ref2expr;
};

/* Given an index expression "index" with nested expressions, replace
 * those nested expressions by parameters.  The identifiers
 * of those parameters reference the corresponding arguments
 * of "expr".  The same identifiers are used in
 * pet_expr_build_nested_ast_exprs.
 *
 * In particular, if "index" is of the form
 *
 *	{ [domain -> [e_1, ..., e_n]] -> array[f(e_1, ..., e_n)] }
 *
 * then we construct the expression
 *
 *	[p_1, ..., p_n] -> { domain -> array[f(p_1, ..., p_n)] }
 *
 */
static __isl_give isl_multi_pw_aff *parametrize_nested_exprs(
	__isl_take isl_multi_pw_aff *index, __isl_keep pet_expr *expr)
{
	int i;
	isl_ctx *ctx;
	isl_space *space, *space2;
	isl_local_space *ls;
	isl_multi_aff *ma, *ma2;

	ctx = isl_multi_pw_aff_get_ctx(index);
	space = isl_multi_pw_aff_get_domain_space(index);
	space = isl_space_unwrap(space);

	space2 = isl_space_domain(isl_space_copy(space));
	ma = isl_multi_aff_identity(isl_space_map_from_set(space2));

	space = isl_space_insert_dims(space, isl_dim_param, 0,
					expr->n_arg);
	for (i = 0; i < expr->n_arg; ++i) {
		isl_id *id = isl_id_alloc(ctx, NULL, expr->args[i]);

		space = isl_space_set_dim_id(space, isl_dim_param, i, id);
	}
	space2 = isl_space_domain(isl_space_copy(space));
	ls = isl_local_space_from_space(space2);
	ma2 = isl_multi_aff_zero(space);
	for (i = 0; i < expr->n_arg; ++i) {
		isl_aff *aff;
		aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
						isl_dim_param, i);
		ma2 = isl_multi_aff_set_aff(ma2, i, aff);
	}
	isl_local_space_free(ls);

	ma = isl_multi_aff_range_product(ma, ma2);

	return isl_multi_pw_aff_pullback_multi_aff(index, ma);
}

static __isl_give isl_ast_expr *pet_expr_build_ast_expr(
	__isl_keep pet_expr *expr, struct pet_build_ast_expr_data *data);

/* Construct an associative array from identifiers for the nested
 * expressions of "expr" to the corresponding isl_ast_expr.
 * The identifiers reference the corresponding arguments of "expr".
 * The same identifiers are used in parametrize_nested_exprs.
 * Note that we only need to construct isl_ast_expr objects for
 * those arguments that actually appear in the index expression of "expr".
 */
static __isl_give isl_id_to_ast_expr *pet_expr_build_nested_ast_exprs(
	__isl_keep pet_expr *expr, struct pet_build_ast_expr_data *data)
{
	int i, dim;
	isl_ctx *ctx = isl_ast_build_get_ctx(data->build);
	isl_id_to_ast_expr *id2expr;

	dim = domain_domain_dim(expr->acc.index);
	id2expr = isl_id_to_ast_expr_alloc(ctx, expr->n_arg);

	for (i = 0; i < expr->n_arg; ++i) {
		isl_id *id;
		isl_ast_expr *ast_expr;

		if (!isl_multi_pw_aff_involves_dims(expr->acc.index,
						    isl_dim_in, dim + i, 1))
			continue;

		id = isl_id_alloc(ctx, NULL, expr->args[i]);
		ast_expr = pet_expr_build_ast_expr(expr->args[i], data);
		id2expr = isl_id_to_ast_expr_set(id2expr, id, ast_expr);
	}

	return id2expr;
}

/* Construct an AST expression from an access expression.
 *
 * If the expression has any arguments, we first convert those
 * to AST expressions and replace the references to those arguments
 * in the index expression by parameters.
 *
 * Then we apply the index transformation if any was provided by the user.
 *
 * If the "access" is actually an affine expression, we print is as such.
 * Otherwise, we print a proper access.
 *
 * If the original expression had any arguments, then they are plugged in now.
 *
 * Finally, we apply an AST transformation on the result, if any was provided
 * by the user.
 */
static __isl_give isl_ast_expr *pet_expr_build_ast_expr(
	__isl_keep pet_expr *expr, struct pet_build_ast_expr_data *data)
{
	isl_pw_aff *pa;
	isl_multi_pw_aff *mpa;
	isl_ast_expr *ast_expr;
	isl_id_to_ast_expr *id2expr;
	isl_ast_build *build = data->build;

	if (!expr)
		return NULL;
	if (expr->type != pet_expr_access)
		isl_die(isl_ast_build_get_ctx(build), isl_error_invalid,
			"not an access expression", return NULL);

	mpa = isl_multi_pw_aff_copy(expr->acc.index);

	if (expr->n_arg > 0) {
		mpa = parametrize_nested_exprs(mpa, expr);
		id2expr = pet_expr_build_nested_ast_exprs(expr, data);
	}

	if (data->fn_index)
		mpa = data->fn_index(mpa, expr->acc.ref_id, data->user_index);
	mpa = isl_multi_pw_aff_coalesce(mpa);

	if (!pet_expr_is_affine(expr)) {
		ast_expr = isl_ast_build_access_from_multi_pw_aff(build, mpa);
	} else {
		pa = isl_multi_pw_aff_get_pw_aff(mpa, 0);
		ast_expr = isl_ast_build_expr_from_pw_aff(build, pa);
		isl_multi_pw_aff_free(mpa);
	}
	if (expr->n_arg > 0)
		ast_expr = isl_ast_expr_substitute_ids(ast_expr, id2expr);
	if (data->fn_expr)
		ast_expr = data->fn_expr(ast_expr, expr->acc.ref_id,
					    data->user_index);

	return ast_expr;
}

/* Construct an AST expression from the access expression "expr" and
 * add the mapping from reference identifier to AST expression to
 * data->ref2expr.
 */
static int add_access(__isl_keep pet_expr *expr, void *user)
{
	struct pet_build_ast_expr_data *data = user;
	isl_id *id;
	isl_ast_expr *ast_expr;

	ast_expr = pet_expr_build_ast_expr(expr, data);

	id = isl_id_copy(expr->acc.ref_id);
	data->ref2expr = isl_id_to_ast_expr_set(data->ref2expr, id, ast_expr);

	return 0;
}

/* Construct an associative array from reference identifiers of
 * access expressions in "stmt" to the corresponding isl_ast_expr.
 * Each index expression is first transformed through "fn_index"
 * (if not NULL).  Then an AST expression is generated using "build".
 * Finally, the AST expression is transformed using "fn_expr"
 * (if not NULL).
 */
__isl_give isl_id_to_ast_expr *pet_stmt_build_ast_exprs(struct pet_stmt *stmt,
	__isl_keep isl_ast_build *build,
	__isl_give isl_multi_pw_aff *(*fn_index)(
		__isl_take isl_multi_pw_aff *mpa, __isl_keep isl_id *id,
		void *user), void *user_index,
	__isl_give isl_ast_expr *(*fn_expr)(__isl_take isl_ast_expr *expr,
		__isl_keep isl_id *id, void *user), void *user_expr)
{
	struct pet_build_ast_expr_data data =
		{ build, fn_index, user_index, fn_expr, user_expr };
	isl_ctx *ctx;

	if (!stmt || !build)
		return NULL;

	ctx = isl_ast_build_get_ctx(build);
	data.ref2expr = isl_id_to_ast_expr_alloc(ctx, 0);
	if (pet_tree_foreach_access_expr(stmt->body, &add_access, &data) < 0)
		data.ref2expr = isl_id_to_ast_expr_free(data.ref2expr);

	return data.ref2expr;
}

/* Print the access expression "expr" to "p".
 *
 * We look up the corresponding isl_ast_expr in "ref2expr"
 * and print that to "p".
 */
static __isl_give isl_printer *print_access(__isl_take isl_printer *p,
	__isl_keep pet_expr *expr, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	isl_ast_expr *ast_expr;
	int is_access;

	if (!isl_id_to_ast_expr_has(ref2expr, expr->acc.ref_id))
		isl_die(isl_printer_get_ctx(p), isl_error_internal,
			"missing expression", return isl_printer_free(p));

	ast_expr = isl_id_to_ast_expr_get(ref2expr,
					isl_id_copy(expr->acc.ref_id));
	is_access = isl_ast_expr_get_type(ast_expr) == isl_ast_expr_op &&
		isl_ast_expr_get_op_type(ast_expr) == isl_ast_op_access;
	if (!is_access)
		p = isl_printer_print_str(p, "(");
	p = isl_printer_print_ast_expr(p, ast_expr);
	if (!is_access)
		p = isl_printer_print_str(p, ")");
	isl_ast_expr_free(ast_expr);

	return p;
}

/* Is "op" a postfix operator?
 */
static int is_postfix(enum pet_op_type op)
{
	switch (op) {
	case pet_op_post_inc:
	case pet_op_post_dec:
		return 1;
	default:
		return 0;
	}
}

static __isl_give isl_printer *print_pet_expr(__isl_take isl_printer *p,
	__isl_keep pet_expr *expr, int outer,
	__isl_keep isl_id_to_ast_expr *ref2expr);

/* Print operation expression "expr" to "p".
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_op(__isl_take isl_printer *p,
	__isl_keep pet_expr *expr, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	switch (expr->n_arg) {
	case 1:
		if (!is_postfix(expr->op))
			p = isl_printer_print_str(p, pet_op_str(expr->op));
		p = print_pet_expr(p, expr->args[pet_un_arg], 0, ref2expr);
		if (is_postfix(expr->op))
			p = isl_printer_print_str(p, pet_op_str(expr->op));
		break;
	case 2:
		p = print_pet_expr(p, expr->args[pet_bin_lhs], 0,
					ref2expr);
		p = isl_printer_print_str(p, " ");
		p = isl_printer_print_str(p, pet_op_str(expr->op));
		p = isl_printer_print_str(p, " ");
		p = print_pet_expr(p, expr->args[pet_bin_rhs], 0,
					ref2expr);
		break;
	case 3:
		p = print_pet_expr(p, expr->args[pet_ter_cond], 0,
					ref2expr);
		p = isl_printer_print_str(p, " ? ");
		p = print_pet_expr(p, expr->args[pet_ter_true], 0,
					ref2expr);
		p = isl_printer_print_str(p, " : ");
		p = print_pet_expr(p, expr->args[pet_ter_false], 0,
					ref2expr);
		break;
	}

	return p;
}

/* Print "expr" to "p".
 *
 * If "outer" is set, then we are printing the outer expression statement.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_pet_expr(__isl_take isl_printer *p,
	__isl_keep pet_expr *expr, int outer,
	__isl_keep isl_id_to_ast_expr *ref2expr)
{
	int i;

	switch (expr->type) {
	case pet_expr_error:
		p = isl_printer_free(p);
		break;
	case pet_expr_int:
		p = isl_printer_print_val(p, expr->i);
		break;
	case pet_expr_double:
		p = isl_printer_print_str(p, expr->d.s);
		break;
	case pet_expr_access:
		p = print_access(p, expr, ref2expr);
		break;
	case pet_expr_op:
		if (!outer)
			p = isl_printer_print_str(p, "(");
		p = print_op(p, expr, ref2expr);
		if (!outer)
			p = isl_printer_print_str(p, ")");
		break;
	case pet_expr_call:
		p = isl_printer_print_str(p, expr->c.name);
		p = isl_printer_print_str(p, "(");
		for (i = 0; i < expr->n_arg; ++i) {
			if (i)
				p = isl_printer_print_str(p, ", ");
			p = print_pet_expr(p, expr->args[i], 1, ref2expr);
		}
		p = isl_printer_print_str(p, ")");
		break;
	case pet_expr_cast:
		if (!outer)
			p = isl_printer_print_str(p, "(");
		p = isl_printer_print_str(p, "(");
		p = isl_printer_print_str(p, expr->type_name);
		p = isl_printer_print_str(p, ") ");
		p = print_pet_expr(p, expr->args[0], 0, ref2expr);
		if (!outer)
			p = isl_printer_print_str(p, ")");
		break;
	}

	return p;
}

static __isl_give isl_printer *print_pet_tree(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, int in_block,
	__isl_keep isl_id_to_ast_expr *ref2expr);

/* Print "tree" to "p", where "tree" is of type pet_tree_block.
 *
 * If "in_block" is set, then the caller has just printed a block,
 * so there is no need to print one for this node.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_pet_tree_block(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, int in_block,
	__isl_keep isl_id_to_ast_expr *ref2expr)
{
	int i, n;

	if (!in_block) {
		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "{");
		p = isl_printer_end_line(p);
		p = isl_printer_indent(p, 2);
	}

	n = pet_tree_block_n_child(tree);

	for (i = 0; i < n; ++i) {
		pet_tree *child;

		child = pet_tree_block_get_child(tree, i);
		p = print_pet_tree(p, child, 0, ref2expr);
		pet_tree_free(child);
	}

	if (!in_block) {
		p = isl_printer_indent(p, -2);
		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "}");
		p = isl_printer_end_line(p);
	}

	return p;
}

/* Print "tree" to "p", where "tree" is of type pet_tree_if or
 * pet_tree_if_else..
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_pet_tree_if(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	pet_expr *expr;
	pet_tree *body;

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "if (");
	expr = pet_tree_if_get_cond(tree);
	p = print_pet_expr(p, expr, 1, ref2expr);
	pet_expr_free(expr);
	p = isl_printer_print_str(p, ") {");
	p = isl_printer_end_line(p);

	p = isl_printer_indent(p, 2);
	body = pet_tree_if_get_then(tree);
	p = print_pet_tree(p, body, 1, ref2expr);
	pet_tree_free(body);
	p = isl_printer_indent(p, -2);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "}");

	if (pet_tree_get_type(tree) == pet_tree_if_else) {
		p = isl_printer_print_str(p, " else {");
		p = isl_printer_end_line(p);

		p = isl_printer_indent(p, 2);
		body = pet_tree_if_get_else(tree);
		p = print_pet_tree(p, body, 1, ref2expr);
		pet_tree_free(body);
		p = isl_printer_indent(p, -2);

		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "}");
	}

	p = isl_printer_end_line(p);

	return p;
}

/* Print "tree" to "p", where "tree" is of type pet_tree_for.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_pet_tree_for(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	pet_expr *expr_iv, *expr;
	pet_tree *body;

	expr_iv = pet_tree_loop_get_var(tree);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "for (");
	p = print_pet_expr(p, expr_iv, 1, ref2expr);
	p = isl_printer_print_str(p, " = ");
	expr = pet_tree_loop_get_init(tree);
	p = print_pet_expr(p, expr, 0, ref2expr);
	pet_expr_free(expr);
	p = isl_printer_print_str(p, "; ");
	expr = pet_tree_loop_get_cond(tree);
	p = print_pet_expr(p, expr, 1, ref2expr);
	pet_expr_free(expr);
	p = isl_printer_print_str(p, "; ");
	p = print_pet_expr(p, expr_iv, 1, ref2expr);
	p = isl_printer_print_str(p, " += ");
	expr = pet_tree_loop_get_inc(tree);
	p = print_pet_expr(p, expr, 0, ref2expr);
	pet_expr_free(expr);
	p = isl_printer_print_str(p, ") {");
	p = isl_printer_end_line(p);

	pet_expr_free(expr_iv);

	p = isl_printer_indent(p, 2);
	body = pet_tree_loop_get_body(tree);
	p = print_pet_tree(p, body, 1, ref2expr);
	pet_tree_free(body);
	p = isl_printer_indent(p, -2);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "}");
	p = isl_printer_end_line(p);

	return p;
}

/* Print "tree" to "p", where "tree" is of type pet_tree_while or
 * pet_tree_infinite_loop.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 *
 * pet_tree_loop_get_cond returns "1" when called on a tree of type
 * pet_tree_infinite_loop, so we can treat them in the same way
 * as trees of type pet_tree_while.
 */
static __isl_give isl_printer *print_pet_tree_while(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	pet_expr *expr;
	pet_tree *body;

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "while (");
	expr = pet_tree_loop_get_cond(tree);
	p = print_pet_expr(p, expr, 1, ref2expr);
	pet_expr_free(expr);
	p = isl_printer_print_str(p, ") {");
	p = isl_printer_end_line(p);

	p = isl_printer_indent(p, 2);
	body = pet_tree_loop_get_body(tree);
	p = print_pet_tree(p, body, 1, ref2expr);
	pet_tree_free(body);
	p = isl_printer_indent(p, -2);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "}");
	p = isl_printer_end_line(p);

	return p;
}

/* Print "tree" to "p", where "tree" is of type pet_tree_decl_init.
 *
 * We assume all variables have already been declared, so we
 * only print the assignment implied by the declaration initialization.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_pet_tree_decl_init(
	__isl_take isl_printer *p, __isl_keep pet_tree *tree,
	__isl_keep isl_id_to_ast_expr *ref2expr)
{
	pet_expr *var, *init;

	p = isl_printer_start_line(p);

	var = pet_tree_decl_get_var(tree);
	p = print_pet_expr(p, var, 1, ref2expr);
	pet_expr_free(var);

	p = isl_printer_print_str(p, " = ");

	init = pet_tree_decl_get_init(tree);
	p = print_pet_expr(p, init, 1, ref2expr);
	pet_expr_free(init);

	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

/* Print "tree" to "p", where "tree" is of type pet_tree_return.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 */
static __isl_give isl_printer *print_pet_tree_return(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	pet_expr *expr;

	expr = pet_tree_expr_get_expr(tree);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "return ");
	p = print_pet_expr(p, expr, 1, ref2expr);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);
	pet_expr_free(expr);

	return p;
}

/* Print "tree" to "p".
 *
 * If "in_block" is set, then the caller has just printed a block,
 * so there is no need to print one for this node.
 *
 * The access subexpressions are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 *
 * We assume all variables have already been declared,
 * so there is nothing to print for nodes of type pet_tree_decl.
 */
static __isl_give isl_printer *print_pet_tree(__isl_take isl_printer *p,
	__isl_keep pet_tree *tree, int in_block,
	__isl_keep isl_id_to_ast_expr *ref2expr)
{
	pet_expr *expr;
	enum pet_tree_type type;

	type = pet_tree_get_type(tree);
	switch (type) {
	case pet_tree_error:
		return isl_printer_free(p);
	case pet_tree_block:
		return print_pet_tree_block(p, tree, in_block, ref2expr);
	case pet_tree_break:
	case pet_tree_continue:
		p = isl_printer_start_line(p);
		if (type == pet_tree_break)
			p = isl_printer_print_str(p, "break;");
		else
			p = isl_printer_print_str(p, "continue;");
		return isl_printer_end_line(p);
	case pet_tree_expr:
		expr = pet_tree_expr_get_expr(tree);
		p = isl_printer_start_line(p);
		p = print_pet_expr(p, expr, 1, ref2expr);
		p = isl_printer_print_str(p, ";");
		p = isl_printer_end_line(p);
		pet_expr_free(expr);
		break;
	case pet_tree_return:
		return print_pet_tree_return(p, tree, ref2expr);
	case pet_tree_if:
	case pet_tree_if_else:
		return print_pet_tree_if(p, tree, ref2expr);
	case pet_tree_for:
		return print_pet_tree_for(p, tree, ref2expr);
	case pet_tree_while:
	case pet_tree_infinite_loop:
		return print_pet_tree_while(p, tree, ref2expr);
	case pet_tree_decl:
		return p;
	case pet_tree_decl_init:
		return print_pet_tree_decl_init(p, tree, ref2expr);
	}

	return p;
}

/* Print "stmt" to "p".
 *
 * The access expressions in "stmt" are replaced by the isl_ast_expr
 * associated to its reference identifier in "ref2expr".
 *
 * If the statement is an assume or a kill statement, then we print nothing.
 */
__isl_give isl_printer *pet_stmt_print_body(struct pet_stmt *stmt,
	__isl_take isl_printer *p, __isl_keep isl_id_to_ast_expr *ref2expr)
{
	if (!stmt)
		return isl_printer_free(p);
	if (pet_stmt_is_assume(stmt))
		return p;
	if (pet_stmt_is_kill(stmt))
		return p;
	p = print_pet_tree(p, stmt->body, 0, ref2expr);

	return p;
}

/* Copy the contents of "input" from offset "start" to "end" to "output".
 */
int copy(FILE *input, FILE *output, long start, long end)
{
	char buffer[1024];
	size_t n, m;

	if (end < 0) {
		fseek(input, 0, SEEK_END);
		end = ftell(input);
	}

	fseek(input, start, SEEK_SET);

	while (start < end) {
		n = end - start;
		if (n > 1024)
			n = 1024;
		n = fread(buffer, 1, n, input);
		if (n <= 0)
			return -1;
		m = fwrite(buffer, 1, n, output);
		if (n != m)
			return -1;
		start += n;
	}

	return 0;
}
