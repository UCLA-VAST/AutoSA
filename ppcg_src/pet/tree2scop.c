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

#include <stdlib.h>
#include <string.h>

#include <isl/id.h>
#include <isl/val.h>
#include <isl/space.h>
#include <isl/local_space.h>
#include <isl/aff.h>
#include <isl/id_to_pw_aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_set.h>

#include "aff.h"
#include "expr.h"
#include "expr_arg.h"
#include "nest.h"
#include "scop.h"
#include "skip.h"
#include "state.h"
#include "tree2scop.h"

/* If "stmt" is an affine assumption, then record the assumption in "pc".
 */
static __isl_give pet_context *add_affine_assumption(struct pet_stmt *stmt,
	__isl_take pet_context *pc)
{
	isl_bool affine;
	isl_set *cond;

	affine = pet_stmt_is_affine_assume(stmt);
	if (affine < 0)
		return pet_context_free(pc);
	if (!affine)
		return pc;
	cond = pet_stmt_assume_get_affine_condition(stmt);
	cond = isl_set_reset_tuple_id(cond);
	pc = pet_context_intersect_domain(pc, cond);
	return pc;
}

/* Given a scop "scop" derived from an assumption statement,
 * record the assumption in "pc", if it is affine.
 * Note that "scop" should consist of exactly one statement.
 */
static __isl_give pet_context *scop_add_affine_assumption(
	__isl_keep pet_scop *scop, __isl_take pet_context *pc)
{
	int i;

	if (!scop)
		return pet_context_free(pc);
	for (i = 0; i < scop->n_stmt; ++i)
		pc = add_affine_assumption(scop->stmts[i], pc);

	return pc;
}

/* Update "pc" by taking into account the writes in "stmt".
 * That is, clear any previously assigned values to variables
 * that are written by "stmt".
 */
static __isl_give pet_context *handle_writes(struct pet_stmt *stmt,
	__isl_take pet_context *pc)
{
	return pet_context_clear_writes_in_tree(pc, stmt->body);
}

/* Update "pc" based on the write accesses in "scop".
 */
static __isl_give pet_context *scop_handle_writes(struct pet_scop *scop,
	__isl_take pet_context *pc)
{
	int i;

	if (!scop)
		return pet_context_free(pc);
	for (i = 0; i < scop->n_stmt; ++i)
		pc = handle_writes(scop->stmts[i], pc);

	return pc;
}

/* Wrapper around pet_expr_resolve_assume
 * for use as a callback to pet_tree_map_expr.
 */
static __isl_give pet_expr *resolve_assume(__isl_take pet_expr *expr,
	void *user)
{
	pet_context *pc = user;

	return pet_expr_resolve_assume(expr, pc);
}

/* Check if any expression inside "tree" is an assume expression and
 * if its single argument can be converted to an affine expression
 * in the context of "pc".
 * If so, replace the argument by the affine expression.
 */
__isl_give pet_tree *pet_tree_resolve_assume(__isl_take pet_tree *tree,
	__isl_keep pet_context *pc)
{
	return pet_tree_map_expr(tree, &resolve_assume, pc);
}

/* Convert a pet_tree to a pet_scop with one statement within the context "pc".
 * "tree" has already been evaluated in the context of "pc".
 * This mainly involves resolving nested expression parameters
 * and setting the name of the iteration space.
 * The name is given by tree->label if it is non-NULL.  Otherwise,
 * it is of the form S_<stmt_nr>.
 */
static struct pet_scop *scop_from_evaluated_tree(__isl_take pet_tree *tree,
	int stmt_nr, __isl_keep pet_context *pc)
{
	isl_space *space;
	isl_set *domain;
	struct pet_stmt *ps;

	space = pet_context_get_space(pc);

	tree = pet_tree_resolve_nested(tree, space);
	tree = pet_tree_resolve_assume(tree, pc);

	domain = pet_context_get_domain(pc);
	ps = pet_stmt_from_pet_tree(domain, stmt_nr, tree);
	return pet_scop_from_pet_stmt(space, ps);
}

/* Convert a top-level pet_expr to a pet_scop with one statement
 * within the context "pc".
 * "expr" has already been evaluated in the context of "pc".
 * We construct a pet_tree from "expr" and continue with
 * scop_from_evaluated_tree.
 * The name is of the form S_<stmt_nr>.
 * The location of the statement is set to "loc".
 */
static struct pet_scop *scop_from_evaluated_expr(__isl_take pet_expr *expr,
	int stmt_nr, __isl_take pet_loc *loc, __isl_keep pet_context *pc)
{
	pet_tree *tree;

	tree = pet_tree_new_expr(expr);
	tree = pet_tree_set_loc(tree, loc);
	return scop_from_evaluated_tree(tree, stmt_nr, pc);
}

/* Convert a pet_tree to a pet_scop with one statement within the context "pc".
 * "tree" has not yet been evaluated in the context of "pc".
 * We evaluate "tree" in the context of "pc" and continue with
 * scop_from_evaluated_tree.
 * The statement name is given by tree->label if it is non-NULL.  Otherwise,
 * it is of the form S_<stmt_nr>.
 */
static struct pet_scop *scop_from_unevaluated_tree(__isl_take pet_tree *tree,
	int stmt_nr, __isl_keep pet_context *pc)
{
	tree = pet_context_evaluate_tree(pc, tree);
	return scop_from_evaluated_tree(tree, stmt_nr, pc);
}

/* Convert a top-level pet_expr to a pet_scop with one statement
 * within the context "pc", where "expr" has not yet been evaluated
 * in the context of "pc".
 * We construct a pet_tree from "expr" and continue with
 * scop_from_unevaluated_tree.
 * The statement name is of the form S_<stmt_nr>.
 * The location of the statement is set to "loc".
 */
static struct pet_scop *scop_from_expr(__isl_take pet_expr *expr,
	int stmt_nr, __isl_take pet_loc *loc, __isl_keep pet_context *pc)
{
	pet_tree *tree;

	tree = pet_tree_new_expr(expr);
	tree = pet_tree_set_loc(tree, loc);
	return scop_from_unevaluated_tree(tree, stmt_nr, pc);
}

/* Construct a pet_scop with a single statement killing the entire
 * array "array".
 * The location of the statement is set to "loc".
 */
static struct pet_scop *kill(__isl_take pet_loc *loc, struct pet_array *array,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	isl_ctx *ctx;
	isl_id *id;
	isl_space *space;
	isl_multi_pw_aff *index;
	isl_map *access;
	pet_expr *expr;

	if (!array)
		goto error;
	ctx = isl_set_get_ctx(array->extent);
	access = isl_map_from_range(isl_set_copy(array->extent));
	id = isl_set_get_tuple_id(array->extent);
	space = isl_space_alloc(ctx, 0, 0, 0);
	space = isl_space_set_tuple_id(space, isl_dim_out, id);
	index = isl_multi_pw_aff_zero(space);
	expr = pet_expr_kill_from_access_and_index(access, index);
	return scop_from_expr(expr, state->n_stmt++, loc, pc);
error:
	pet_loc_free(loc);
	return NULL;
}

/* Construct and return a pet_array corresponding to the variable
 * accessed by "access" by calling the extract_array callback.
 */
static struct pet_array *extract_array(__isl_keep pet_expr *access,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	return state->extract_array(access, pc, state->user);
}

/* Construct a pet_scop for a (single) variable declaration
 * within the context "pc".
 *
 * The scop contains the variable being declared (as an array)
 * and a statement killing the array.
 *
 * If the declaration comes with an initialization, then the scop
 * also contains an assignment to the variable.
 */
static struct pet_scop *scop_from_decl(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	int type_size;
	isl_ctx *ctx;
	struct pet_array *array;
	struct pet_scop *scop_decl, *scop;
	pet_expr *lhs, *rhs, *pe;

	array = extract_array(tree->u.d.var, pc, state);
	if (array)
		array->declared = 1;
	scop_decl = kill(pet_tree_get_loc(tree), array, pc, state);
	scop_decl = pet_scop_add_array(scop_decl, array);

	if (tree->type != pet_tree_decl_init)
		return scop_decl;

	lhs = pet_expr_copy(tree->u.d.var);
	rhs = pet_expr_copy(tree->u.d.init);
	type_size = pet_expr_get_type_size(lhs);
	pe = pet_expr_new_binary(type_size, pet_op_assign, lhs, rhs);
	scop = scop_from_expr(pe, state->n_stmt++, pet_tree_get_loc(tree), pc);

	ctx = pet_tree_get_ctx(tree);
	scop = pet_scop_add_seq(ctx, scop_decl, scop);

	return scop;
}

/* Does "tree" represent a kill statement?
 * That is, is it an expression statement that "calls" __pencil_kill?
 */
static int is_pencil_kill(__isl_keep pet_tree *tree)
{
	pet_expr *expr;
	const char *name;

	if (!tree)
		return -1;
	if (tree->type != pet_tree_expr)
		return 0;
	expr = tree->u.e.expr;
	if (pet_expr_get_type(expr) != pet_expr_call)
		return 0;
	name = pet_expr_call_get_name(expr);
	if (!name)
		return -1;
	return !strcmp(name, "__pencil_kill");
}

/* Add a kill to "scop" that kills what is accessed by
 * the access expression "expr".
 *
 * Mark the access as a write prior to evaluation to avoid
 * the access being replaced by a possible known value
 * during the evaluation.
 *
 * If the access expression has any arguments (after evaluation
 * in the context of "pc"), then we ignore it, since we cannot
 * tell which elements are definitely killed.
 *
 * Otherwise, we extend the index expression to the dimension
 * of the accessed array and intersect with the extent of the array and
 * add a kill expression that kills these array elements is added to "scop".
 */
static struct pet_scop *scop_add_kill(struct pet_scop *scop,
	__isl_take pet_expr *expr, __isl_take pet_loc *loc,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	int dim1, dim2;
	isl_id *id;
	isl_multi_pw_aff *index;
	isl_map *map;
	pet_expr *kill;
	struct pet_array *array;
	struct pet_scop *scop_i;

	expr = pet_expr_access_set_write(expr, 1);
	expr = pet_context_evaluate_expr(pc, expr);
	if (!expr)
		goto error;
	if (expr->n_arg != 0) {
		pet_loc_free(loc);
		pet_expr_free(expr);
		return scop;
	}
	array = extract_array(expr, pc, state);
	if (!array)
		goto error;
	index = pet_expr_access_get_index(expr);
	pet_expr_free(expr);
	map = isl_map_from_multi_pw_aff(isl_multi_pw_aff_copy(index));
	id = isl_map_get_tuple_id(map, isl_dim_out);
	dim1 = isl_set_dim(array->extent, isl_dim_set);
	dim2 = isl_map_dim(map, isl_dim_out);
	map = isl_map_add_dims(map, isl_dim_out, dim1 - dim2);
	map = isl_map_set_tuple_id(map, isl_dim_out, id);
	map = isl_map_intersect_range(map, isl_set_copy(array->extent));
	pet_array_free(array);
	kill = pet_expr_kill_from_access_and_index(map, index);
	scop_i = scop_from_evaluated_expr(kill, state->n_stmt++, loc, pc);
	scop = pet_scop_add_par(state->ctx, scop, scop_i);

	return scop;
error:
	pet_expr_free(expr);
	pet_loc_free(loc);
	return pet_scop_free(scop);
}

/* For each argument of the __pencil_kill call in "tree" that
 * represents an access, add a kill statement to "scop" killing the accessed
 * elements.
 */
static struct pet_scop *scop_from_pencil_kill(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	pet_expr *call;
	struct pet_scop *scop;
	int i, n;

	call = tree->u.e.expr;

	scop = pet_scop_empty(pet_context_get_space(pc));

	n = pet_expr_get_n_arg(call);
	for (i = 0; i < n; ++i) {
		pet_expr *arg;
		pet_loc *loc;

		arg = pet_expr_get_arg(call, i);
		if (!arg)
			return pet_scop_free(scop);
		if (pet_expr_get_type(arg) != pet_expr_access) {
			pet_expr_free(arg);
			continue;
		}
		loc = pet_tree_get_loc(tree);
		scop = scop_add_kill(scop, arg, loc, pc, state);
	}

	return scop;
}

/* Construct a pet_scop for an expression statement within the context "pc".
 *
 * If the expression calls __pencil_kill, then it needs to be converted
 * into zero or more kill statements.
 * Otherwise, a scop is extracted directly from the tree.
 */
static struct pet_scop *scop_from_tree_expr(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	int is_kill;

	is_kill = is_pencil_kill(tree);
	if (is_kill < 0)
		return NULL;
	if (is_kill)
		return scop_from_pencil_kill(tree, pc, state);
	return scop_from_unevaluated_tree(pet_tree_copy(tree),
						state->n_stmt++, pc);
}

/* Construct a pet_scop for a return statement within the context "pc".
 */
static struct pet_scop *scop_from_return(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	return scop_from_unevaluated_tree(pet_tree_copy(tree),
						state->n_stmt++, pc);
}

/* Return those elements in the space of "cond" that come after
 * (based on "sign") an element in "cond" in the final dimension.
 */
static __isl_give isl_set *after(__isl_take isl_set *cond, int sign)
{
	isl_space *space;
	isl_map *previous_to_this;
	int i, dim;

	dim = isl_set_dim(cond, isl_dim_set);
	space = isl_space_map_from_set(isl_set_get_space(cond));
	previous_to_this = isl_map_universe(space);
	for (i = 0; i + 1 < dim; ++i)
		previous_to_this = isl_map_equate(previous_to_this,
			isl_dim_in, i, isl_dim_out, i);
	if (sign > 0)
		previous_to_this = isl_map_order_lt(previous_to_this,
			isl_dim_in, dim - 1, isl_dim_out, dim - 1);
	else
		previous_to_this = isl_map_order_gt(previous_to_this,
			isl_dim_in, dim - 1, isl_dim_out, dim - 1);

	cond = isl_set_apply(cond, previous_to_this);

	return cond;
}

/* Remove those iterations of "domain" that have an earlier iteration
 * (based on "sign") in the final dimension where "skip" is satisfied.
 * If "apply_skip_map" is set, then "skip_map" is first applied
 * to the embedded skip condition before removing it from the domain.
 */
static __isl_give isl_set *apply_affine_break(__isl_take isl_set *domain,
	__isl_take isl_set *skip, int sign,
	int apply_skip_map, __isl_keep isl_map *skip_map)
{
	if (apply_skip_map)
		skip = isl_set_apply(skip, isl_map_copy(skip_map));
	skip = isl_set_intersect(skip , isl_set_copy(domain));
	return isl_set_subtract(domain, after(skip, sign));
}

/* Create a single-dimensional multi-affine expression on the domain space
 * of "pc" that is equal to the final dimension of this domain.
 * "loop_nr" is the sequence number of the corresponding loop.
 * If "id" is not NULL, then it is used as the output tuple name.
 * Otherwise, the name is constructed as L_<loop_nr>.
 */
static __isl_give isl_multi_aff *map_to_last(__isl_keep pet_context *pc,
	int loop_nr, __isl_keep isl_id *id)
{
	int pos;
	isl_space *space;
	isl_local_space *ls;
	isl_aff *aff;
	isl_multi_aff *ma;
	char name[50];
	isl_id *label;

	space = pet_context_get_space(pc);
	pos = isl_space_dim(space, isl_dim_set) - 1;
	ls = isl_local_space_from_space(space);
	aff = isl_aff_var_on_domain(ls, isl_dim_set, pos);
	ma = isl_multi_aff_from_aff(aff);

	if (id) {
		label = isl_id_copy(id);
	} else {
		snprintf(name, sizeof(name), "L_%d", loop_nr);
		label = isl_id_alloc(pet_context_get_ctx(pc), name, NULL);
	}
	ma = isl_multi_aff_set_tuple_id(ma, isl_dim_out, label);

	return ma;
}

/* Create an affine expression that maps elements
 * of an array "id_test" to the previous element in the final dimension
 * (according to "inc"), provided this element belongs to "domain".
 * That is, create the affine expression
 *
 *	{ id[outer,x] -> id[outer,x - inc] : (outer,x - inc) in domain }
 */
static __isl_give isl_multi_pw_aff *map_to_previous(__isl_take isl_id *id_test,
	__isl_take isl_set *domain, __isl_take isl_val *inc)
{
	int pos;
	isl_space *space;
	isl_aff *aff;
	isl_pw_aff *pa;
	isl_multi_aff *ma;
	isl_multi_pw_aff *prev;

	pos = isl_set_dim(domain, isl_dim_set) - 1;
	space = isl_set_get_space(domain);
	space = isl_space_map_from_set(space);
	ma = isl_multi_aff_identity(space);
	aff = isl_multi_aff_get_aff(ma, pos);
	aff = isl_aff_add_constant_val(aff, isl_val_neg(inc));
	ma = isl_multi_aff_set_aff(ma, pos, aff);
	domain = isl_set_preimage_multi_aff(domain, isl_multi_aff_copy(ma));
	prev = isl_multi_pw_aff_from_multi_aff(ma);
	pa = isl_multi_pw_aff_get_pw_aff(prev, pos);
	pa = isl_pw_aff_intersect_domain(pa, domain);
	prev = isl_multi_pw_aff_set_pw_aff(prev, pos, pa);
	prev = isl_multi_pw_aff_set_tuple_id(prev, isl_dim_out, id_test);

	return prev;
}

/* Add an implication to "scop" expressing that if an element of
 * virtual array "id_test" has value "satisfied" then all previous elements
 * of this array (in the final dimension) also have that value.
 * The set of previous elements is bounded by "domain".
 * If "sign" is negative then the iterator
 * is decreasing and we express that all subsequent array elements
 * (but still defined previously) have the same value.
 */
static struct pet_scop *add_implication(struct pet_scop *scop,
	__isl_take isl_id *id_test, __isl_take isl_set *domain, int sign,
	int satisfied)
{
	int i, dim;
	isl_space *space;
	isl_map *map;

	dim = isl_set_dim(domain, isl_dim_set);
	domain = isl_set_set_tuple_id(domain, id_test);
	space = isl_space_map_from_set(isl_set_get_space(domain));
	map = isl_map_universe(space);
	for (i = 0; i + 1 < dim; ++i)
		map = isl_map_equate(map, isl_dim_in, i, isl_dim_out, i);
	if (sign > 0)
		map = isl_map_order_ge(map,
				    isl_dim_in, dim - 1, isl_dim_out, dim - 1);
	else
		map = isl_map_order_le(map,
				    isl_dim_in, dim - 1, isl_dim_out, dim - 1);
	map = isl_map_intersect_range(map, domain);
	scop = pet_scop_add_implication(scop, map, satisfied);

	return scop;
}

/* Add a filter to "scop" that imposes that it is only executed
 * when the variable identified by "id_test" has a zero value
 * for all previous iterations of "domain".
 *
 * In particular, add a filter that imposes that the array
 * has a zero value at the previous iteration of domain and
 * add an implication that implies that it then has that
 * value for all previous iterations.
 */
static struct pet_scop *scop_add_break(struct pet_scop *scop,
	__isl_take isl_id *id_test, __isl_take isl_set *domain,
	__isl_take isl_val *inc)
{
	isl_multi_pw_aff *prev;
	int sign = isl_val_sgn(inc);

	prev = map_to_previous(isl_id_copy(id_test), isl_set_copy(domain), inc);
	scop = add_implication(scop, id_test, domain, sign, 0);
	scop = pet_scop_filter(scop, prev, 0);

	return scop;
}

static struct pet_scop *scop_from_tree(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state);

/* Construct a pet_scop for an infinite loop around the given body
 * within the context "pc".
 * "loop_id" is the label on the loop or NULL if there is no such label.
 *
 * The domain of "pc" has already been extended with an infinite loop
 *
 *	{ [t] : t >= 0 }
 *
 * We extract a pet_scop for the body and then embed it in a loop with
 * schedule
 *
 *	{ [outer,t] -> [t] }
 *
 * If the body contains any break, then it is taken into
 * account in apply_affine_break (if the skip condition is affine)
 * or in scop_add_break (if the skip condition is not affine).
 *
 * Note that in case of an affine skip condition,
 * since we are dealing with a loop without loop iterator,
 * the skip condition cannot refer to the current loop iterator and
 * so effectively, the effect on the iteration domain is of the form
 *
 *	{ [outer,0]; [outer,t] : t >= 1 and not skip }
 */
static struct pet_scop *scop_from_infinite_loop(__isl_keep pet_tree *body,
	__isl_keep isl_id *loop_id, __isl_keep pet_context *pc,
	struct pet_state *state)
{
	isl_ctx *ctx;
	isl_id *id_test;
	isl_set *domain;
	isl_set *skip;
	isl_multi_aff *sched;
	struct pet_scop *scop;
	int has_affine_break;
	int has_var_break;

	ctx = pet_tree_get_ctx(body);
	domain = pet_context_get_domain(pc);
	sched = map_to_last(pc, state->n_loop++, loop_id);

	scop = scop_from_tree(body, pc, state);

	has_affine_break = pet_scop_has_affine_skip(scop, pet_skip_later);
	if (has_affine_break)
		skip = pet_scop_get_affine_skip_domain(scop, pet_skip_later);
	has_var_break = pet_scop_has_var_skip(scop, pet_skip_later);
	if (has_var_break)
		id_test = pet_scop_get_skip_id(scop, pet_skip_later);

	scop = pet_scop_reset_skips(scop);
	scop = pet_scop_embed(scop, isl_set_copy(domain), sched);
	if (has_affine_break) {
		domain = apply_affine_break(domain, skip, 1, 0, NULL);
		scop = pet_scop_intersect_domain_prefix(scop,
							isl_set_copy(domain));
	}
	if (has_var_break)
		scop = scop_add_break(scop, id_test, domain, isl_val_one(ctx));
	else
		isl_set_free(domain);

	return scop;
}

/* Construct a pet_scop for an infinite loop, i.e., a loop of the form
 *
 *	for (;;)
 *		body
 *
 * within the context "pc".
 *
 * Extend the domain of "pc" with an extra inner loop
 *
 *	{ [t] : t >= 0 }
 *
 * and construct the scop in scop_from_infinite_loop.
 */
static struct pet_scop *scop_from_infinite_for(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	struct pet_scop *scop;

	pc = pet_context_copy(pc);
	pc = pet_context_clear_writes_in_tree(pc, tree->u.l.body);

	pc = pet_context_add_infinite_loop(pc);

	scop = scop_from_infinite_loop(tree->u.l.body, tree->label, pc, state);

	pet_context_free(pc);

	return scop;
}

/* Construct a pet_scop for a while loop of the form
 *
 *	while (pa)
 *		body
 *
 * within the context "pc".
 *
 * The domain of "pc" has already been extended with an infinite loop
 *
 *	{ [t] : t >= 0 }
 *
 * Here, we add the constraints on the outer loop iterators
 * implied by "pa" and construct the scop in scop_from_infinite_loop.
 * Note that the intersection with these constraints
 * may result in an empty loop.
 */
static struct pet_scop *scop_from_affine_while(__isl_keep pet_tree *tree,
	__isl_take isl_pw_aff *pa, __isl_take pet_context *pc,
	struct pet_state *state)
{
	struct pet_scop *scop;
	isl_set *dom, *local;
	isl_set *valid;

	valid = isl_pw_aff_domain(isl_pw_aff_copy(pa));
	dom = isl_pw_aff_non_zero_set(pa);
	local = isl_set_add_dims(isl_set_copy(dom), isl_dim_set, 1);
	pc = pet_context_intersect_domain(pc, local);
	scop = scop_from_infinite_loop(tree->u.l.body, tree->label, pc, state);
	scop = pet_scop_restrict(scop, dom);
	scop = pet_scop_restrict_context(scop, valid);

	pet_context_free(pc);
	return scop;
}

/* Construct a scop for a while, given the scops for the condition
 * and the body, the filter identifier and the iteration domain of
 * the while loop.
 *
 * In particular, the scop for the condition is filtered to depend
 * on "id_test" evaluating to true for all previous iterations
 * of the loop, while the scop for the body is filtered to depend
 * on "id_test" evaluating to true for all iterations up to the
 * current iteration.
 * The actual filter only imposes that this virtual array has
 * value one on the previous or the current iteration.
 * The fact that this condition also applies to the previous
 * iterations is enforced by an implication.
 *
 * These filtered scops are then combined into a single scop,
 * with the condition scop scheduled before the body scop.
 *
 * "sign" is positive if the iterator increases and negative
 * if it decreases.
 */
static struct pet_scop *scop_add_while(struct pet_scop *scop_cond,
	struct pet_scop *scop_body, __isl_take isl_id *id_test,
	__isl_take isl_set *domain, __isl_take isl_val *inc)
{
	isl_ctx *ctx = isl_set_get_ctx(domain);
	isl_space *space;
	isl_multi_pw_aff *test_index;
	isl_multi_pw_aff *prev;
	int sign = isl_val_sgn(inc);
	struct pet_scop *scop;

	prev = map_to_previous(isl_id_copy(id_test), isl_set_copy(domain), inc);
	scop_cond = pet_scop_filter(scop_cond, prev, 1);

	space = isl_space_map_from_set(isl_set_get_space(domain));
	test_index = isl_multi_pw_aff_identity(space);
	test_index = isl_multi_pw_aff_set_tuple_id(test_index, isl_dim_out,
						isl_id_copy(id_test));
	scop_body = pet_scop_filter(scop_body, test_index, 1);

	scop = pet_scop_add_seq(ctx, scop_cond, scop_body);
	scop = add_implication(scop, id_test, domain, sign, 1);

	return scop;
}

/* Create a pet_scop with a single statement with name S_<stmt_nr>,
 * evaluating "cond" and writing the result to a virtual scalar,
 * as expressed by "index".
 * The expression "cond" has not yet been evaluated in the context of "pc".
 * Do so within the context "pc".
 * The location of the statement is set to "loc".
 */
static struct pet_scop *scop_from_non_affine_condition(
	__isl_take pet_expr *cond, int stmt_nr,
	__isl_take isl_multi_pw_aff *index,
	__isl_take pet_loc *loc, __isl_keep pet_context *pc)
{
	pet_expr *expr, *write;

	cond = pet_context_evaluate_expr(pc, cond);

	write = pet_expr_from_index(index);
	write = pet_expr_access_set_write(write, 1);
	write = pet_expr_access_set_read(write, 0);
	expr = pet_expr_new_binary(1, pet_op_assign, write, cond);

	return scop_from_evaluated_expr(expr, stmt_nr, loc, pc);
}

/* Given that "scop" has an affine skip condition of type pet_skip_now,
 * apply this skip condition to the domain of "pc".
 * That is, remove the elements satisfying the skip condition from
 * the domain of "pc".
 */
static __isl_give pet_context *apply_affine_continue(__isl_take pet_context *pc,
	struct pet_scop *scop)
{
	isl_set *domain, *skip;

	skip = pet_scop_get_affine_skip_domain(scop, pet_skip_now);
	domain = pet_context_get_domain(pc);
	domain = isl_set_subtract(domain, skip);
	pc = pet_context_intersect_domain(pc, domain);

	return pc;
}

/* Add a scop for evaluating the loop increment "inc" at the end
 * of a loop body "scop" within the context "pc".
 *
 * The skip conditions resulting from continue statements inside
 * the body do not apply to "inc", but those resulting from break
 * statements do need to get applied.
 */
static struct pet_scop *scop_add_inc(struct pet_scop *scop,
	__isl_take pet_expr *inc, __isl_take pet_loc *loc,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	struct pet_scop *scop_inc;

	pc = pet_context_copy(pc);

	if (pet_scop_has_skip(scop, pet_skip_later)) {
		isl_multi_pw_aff *skip;
		skip = pet_scop_get_skip(scop, pet_skip_later);
		scop = pet_scop_set_skip(scop, pet_skip_now, skip);
		if (pet_scop_has_affine_skip(scop, pet_skip_now))
			pc = apply_affine_continue(pc, scop);
	} else
		pet_scop_reset_skip(scop, pet_skip_now);
	scop_inc = scop_from_expr(inc, state->n_stmt++, loc, pc);
	scop = pet_scop_add_seq(state->ctx, scop, scop_inc);

	pet_context_free(pc);

	return scop;
}

/* Construct a generic while scop, with iteration domain
 * { [t] : t >= 0 } around the scop for "tree_body" within the context "pc".
 * "loop_id" is the label on the loop or NULL if there is no such label.
 * The domain of "pc" has already been extended with this infinite loop
 *
 *	{ [t] : t >= 0 }
 *
 * The scop consists of two parts,
 * one for evaluating the condition "cond" and one for the body.
 * If "expr_inc" is not NULL, then a scop for evaluating this expression
 * is added at the end of the body,
 * after replacing any skip conditions resulting from continue statements
 * by the skip conditions resulting from break statements (if any).
 *
 * The schedules are combined as a sequence to reflect that the condition is
 * evaluated before the body is executed and the body is filtered to depend
 * on the result of the condition evaluating to true on all iterations
 * up to the current iteration, while the evaluation of the condition itself
 * is filtered to depend on the result of the condition evaluating to true
 * on all previous iterations.
 * The context of the scop representing the body is dropped
 * because we don't know how many times the body will be executed,
 * if at all.
 *
 * If the body contains any break, then it is taken into
 * account in apply_affine_break (if the skip condition is affine)
 * or in scop_add_break (if the skip condition is not affine).
 *
 * Note that in case of an affine skip condition,
 * since we are dealing with a loop without loop iterator,
 * the skip condition cannot refer to the current loop iterator and
 * so effectively, the effect on the iteration domain is of the form
 *
 *	{ [outer,0]; [outer,t] : t >= 1 and not skip }
 */
static struct pet_scop *scop_from_non_affine_while(__isl_take pet_expr *cond,
	__isl_take pet_loc *loc, __isl_keep pet_tree *tree_body,
	__isl_keep isl_id *loop_id, __isl_take pet_expr *expr_inc,
	__isl_take pet_context *pc, struct pet_state *state)
{
	isl_ctx *ctx;
	isl_id *id_test, *id_break_test;
	isl_space *space;
	isl_multi_pw_aff *test_index;
	isl_set *domain;
	isl_set *skip;
	isl_multi_aff *sched;
	struct pet_scop *scop, *scop_body;
	int has_affine_break;
	int has_var_break;

	ctx = state->ctx;
	space = pet_context_get_space(pc);
	test_index = pet_create_test_index(space, state->n_test++);
	scop = scop_from_non_affine_condition(cond, state->n_stmt++,
				isl_multi_pw_aff_copy(test_index),
				pet_loc_copy(loc), pc);
	id_test = isl_multi_pw_aff_get_tuple_id(test_index, isl_dim_out);
	domain = pet_context_get_domain(pc);
	scop = pet_scop_add_boolean_array(scop, isl_set_copy(domain),
					test_index, state->int_size);

	sched = map_to_last(pc, state->n_loop++, loop_id);

	scop_body = scop_from_tree(tree_body, pc, state);

	has_affine_break = pet_scop_has_affine_skip(scop_body, pet_skip_later);
	if (has_affine_break)
		skip = pet_scop_get_affine_skip_domain(scop_body,
							pet_skip_later);
	has_var_break = pet_scop_has_var_skip(scop_body, pet_skip_later);
	if (has_var_break)
		id_break_test = pet_scop_get_skip_id(scop_body, pet_skip_later);

	scop_body = pet_scop_reset_context(scop_body);
	if (expr_inc)
		scop_body = scop_add_inc(scop_body, expr_inc, loc, pc, state);
	else
		pet_loc_free(loc);
	scop_body = pet_scop_reset_skips(scop_body);

	if (has_affine_break) {
		domain = apply_affine_break(domain, skip, 1, 0, NULL);
		scop = pet_scop_intersect_domain_prefix(scop,
							isl_set_copy(domain));
		scop_body = pet_scop_intersect_domain_prefix(scop_body,
							isl_set_copy(domain));
	}
	if (has_var_break) {
		scop = scop_add_break(scop, isl_id_copy(id_break_test),
					isl_set_copy(domain), isl_val_one(ctx));
		scop_body = scop_add_break(scop_body, id_break_test,
					isl_set_copy(domain), isl_val_one(ctx));
	}
	scop = scop_add_while(scop, scop_body, id_test, isl_set_copy(domain),
				isl_val_one(ctx));

	scop = pet_scop_embed(scop, domain, sched);

	pet_context_free(pc);
	return scop;
}

/* Check if the while loop is of the form
 *
 *	while (affine expression)
 *		body
 *
 * If so, call scop_from_affine_while to construct a scop.
 *
 * Otherwise, pass control to scop_from_non_affine_while.
 *
 * "pc" is the context in which the affine expressions in the scop are created.
 * The domain of "pc" is extended with an infinite loop
 *
 *	{ [t] : t >= 0 }
 *
 * before passing control to scop_from_affine_while or
 * scop_from_non_affine_while.
 */
static struct pet_scop *scop_from_while(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	pet_expr *cond_expr;
	isl_pw_aff *pa;

	if (!tree)
		return NULL;

	pc = pet_context_copy(pc);
	pc = pet_context_clear_writes_in_tree(pc, tree->u.l.body);

	cond_expr = pet_expr_copy(tree->u.l.cond);
	cond_expr = pet_context_evaluate_expr(pc, cond_expr);
	pa = pet_expr_extract_affine_condition(cond_expr, pc);
	pet_expr_free(cond_expr);

	pc = pet_context_add_infinite_loop(pc);

	if (!pa)
		goto error;

	if (!isl_pw_aff_involves_nan(pa))
		return scop_from_affine_while(tree, pa, pc, state);
	isl_pw_aff_free(pa);
	return scop_from_non_affine_while(pet_expr_copy(tree->u.l.cond),
				pet_tree_get_loc(tree), tree->u.l.body,
				tree->label, NULL, pc, state);
error:
	pet_context_free(pc);
	return NULL;
}

/* Check whether "cond" expresses a simple loop bound
 * on the final set dimension.
 * In particular, if "up" is set then "cond" should contain only
 * upper bounds on the final set dimension.
 * Otherwise, it should contain only lower bounds.
 */
static int is_simple_bound(__isl_keep isl_set *cond, __isl_keep isl_val *inc)
{
	int pos;

	pos = isl_set_dim(cond, isl_dim_set) - 1;
	if (isl_val_is_pos(inc))
		return !isl_set_dim_has_any_lower_bound(cond, isl_dim_set, pos);
	else
		return !isl_set_dim_has_any_upper_bound(cond, isl_dim_set, pos);
}

/* Extend a condition on a given iteration of a loop to one that
 * imposes the same condition on all previous iterations.
 * "domain" expresses the lower [upper] bound on the iterations
 * when inc is positive [negative] in its final dimension.
 *
 * In particular, we construct the condition (when inc is positive)
 *
 *	forall i' : (domain(i') and i' <= i) => cond(i')
 *
 * (where "<=" applies to the final dimension)
 * which is equivalent to
 *
 *	not exists i' : domain(i') and i' <= i and not cond(i')
 *
 * We construct this set by subtracting the satisfying cond from domain,
 * applying a map
 *
 *	{ [i'] -> [i] : i' <= i }
 *
 * and then subtracting the result from domain again.
 */
static __isl_give isl_set *valid_for_each_iteration(__isl_take isl_set *cond,
	__isl_take isl_set *domain, __isl_take isl_val *inc)
{
	isl_space *space;
	isl_map *previous_to_this;
	int i, dim;

	dim = isl_set_dim(cond, isl_dim_set);
	space = isl_space_map_from_set(isl_set_get_space(cond));
	previous_to_this = isl_map_universe(space);
	for (i = 0; i + 1 < dim; ++i)
		previous_to_this = isl_map_equate(previous_to_this,
			isl_dim_in, i, isl_dim_out, i);
	if (isl_val_is_pos(inc))
		previous_to_this = isl_map_order_le(previous_to_this,
			isl_dim_in, dim - 1, isl_dim_out, dim - 1);
	else
		previous_to_this = isl_map_order_ge(previous_to_this,
			isl_dim_in, dim - 1, isl_dim_out, dim - 1);

	cond = isl_set_subtract(isl_set_copy(domain), cond);
	cond = isl_set_apply(cond, previous_to_this);
	cond = isl_set_subtract(domain, cond);

	isl_val_free(inc);

	return cond;
}

/* Given an initial value of the form
 *
 * { [outer,i] -> init(outer) }
 *
 * construct a domain of the form
 *
 * { [outer,i] : exists a: i = init(outer) + a * inc and a >= 0 }
 */
static __isl_give isl_set *strided_domain(__isl_take isl_pw_aff *init,
	__isl_take isl_val *inc)
{
	int dim;
	isl_aff *aff;
	isl_space *space;
	isl_local_space *ls;
	isl_set *set;

	dim = isl_pw_aff_dim(init, isl_dim_in);

	init = isl_pw_aff_add_dims(init, isl_dim_in, 1);
	space = isl_pw_aff_get_domain_space(init);
	ls = isl_local_space_from_space(space);
	aff = isl_aff_zero_on_domain(isl_local_space_copy(ls));
	aff = isl_aff_add_coefficient_val(aff, isl_dim_in, dim, inc);
	init = isl_pw_aff_add(init, isl_pw_aff_from_aff(aff));

	aff = isl_aff_var_on_domain(ls, isl_dim_set, dim - 1);
	set = isl_pw_aff_eq_set(isl_pw_aff_from_aff(aff), init);

	set = isl_set_lower_bound_si(set, isl_dim_set, dim, 0);
	set = isl_set_project_out(set, isl_dim_set, dim, 1);

	return set;
}

/* Assuming "cond" represents a bound on a loop where the loop
 * iterator "iv" is incremented (or decremented) by one, check if wrapping
 * is possible.
 *
 * Under the given assumptions, wrapping is only possible if "cond" allows
 * for the last value before wrapping, i.e., 2^width - 1 in case of an
 * increasing iterator and 0 in case of a decreasing iterator.
 */
static int can_wrap(__isl_keep isl_set *cond, __isl_keep pet_expr *iv,
	__isl_keep isl_val *inc)
{
	int cw;
	isl_ctx *ctx;
	isl_val *limit;
	isl_set *test;

	test = isl_set_copy(cond);

	ctx = isl_set_get_ctx(test);
	if (isl_val_is_neg(inc))
		limit = isl_val_zero(ctx);
	else {
		limit = isl_val_int_from_ui(ctx, pet_expr_get_type_size(iv));
		limit = isl_val_2exp(limit);
		limit = isl_val_sub_ui(limit, 1);
	}

	test = isl_set_fix_val(cond, isl_dim_set, 0, limit);
	cw = !isl_set_is_empty(test);
	isl_set_free(test);

	return cw;
}

/* Given a space
 *
 *	{ [outer, v] },
 *
 * construct the following affine expression on this space
 *
 *	{ [outer, v] -> [outer, v mod 2^width] }
 *
 * where width is the number of bits used to represent the values
 * of the unsigned variable "iv".
 */
static __isl_give isl_multi_aff *compute_wrapping(__isl_take isl_space *space,
	__isl_keep pet_expr *iv)
{
	int dim;
	isl_aff *aff;
	isl_multi_aff *ma;

	dim = isl_space_dim(space, isl_dim_set);

	space = isl_space_map_from_set(space);
	ma = isl_multi_aff_identity(space);

	aff = isl_multi_aff_get_aff(ma, dim - 1);
	aff = pet_wrap_aff(aff, pet_expr_get_type_size(iv));
	ma = isl_multi_aff_set_aff(ma, dim - 1, aff);

	return ma;
}

/* Given two sets in the space
 *
 *	{ [l,i] },
 *
 * where l represents the outer loop iterators, compute the set
 * of values of l that ensure that "set1" is a subset of "set2".
 *
 * set1 is a subset of set2 if
 *
 *	forall i: set1(l,i) => set2(l,i)
 *
 * or
 *
 *	not exists i: set1(l,i) and not set2(l,i)
 *
 * i.e.,
 *
 *	not exists i: (set1 \ set2)(l,i)
 */
static __isl_give isl_set *enforce_subset(__isl_take isl_set *set1,
	__isl_take isl_set *set2)
{
	int pos;

	pos = isl_set_dim(set1, isl_dim_set) - 1;
	set1 = isl_set_subtract(set1, set2);
	set1 = isl_set_eliminate(set1, isl_dim_set, pos, 1);
	return isl_set_complement(set1);
}

/* Compute the set of outer iterator values for which "cond" holds
 * on the next iteration of the inner loop for each element of "dom".
 *
 * We first construct mapping { [l,i] -> [l,i + inc] } (where l refers
 * to the outer loop iterators), plug that into "cond"
 * and then compute the set of outer iterators for which "dom" is a subset
 * of the result.
 */
static __isl_give isl_set *valid_on_next(__isl_take isl_set *cond,
	__isl_take isl_set *dom, __isl_take isl_val *inc)
{
	int pos;
	isl_space *space;
	isl_aff *aff;
	isl_multi_aff *ma;

	pos = isl_set_dim(dom, isl_dim_set) - 1;
	space = isl_set_get_space(dom);
	space = isl_space_map_from_set(space);
	ma = isl_multi_aff_identity(space);
	aff = isl_multi_aff_get_aff(ma, pos);
	aff = isl_aff_add_constant_val(aff, inc);
	ma = isl_multi_aff_set_aff(ma, pos, aff);
	cond = isl_set_preimage_multi_aff(cond, ma);

	return enforce_subset(dom, cond);
}

/* Construct a pet_scop for the initialization of the iterator
 * of the for loop "tree" within the context "pc" (i.e., the context
 * of the loop).
 */
static __isl_give pet_scop *scop_from_for_init(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	pet_expr *expr_iv, *init;
	int type_size;

	expr_iv = pet_expr_copy(tree->u.l.iv);
	type_size = pet_expr_get_type_size(expr_iv);
	init = pet_expr_copy(tree->u.l.init);
	init = pet_expr_new_binary(type_size, pet_op_assign, expr_iv, init);
	return scop_from_expr(init, state->n_stmt++,
					pet_tree_get_loc(tree), pc);
}

/* Extract the for loop "tree" as a while loop within the context "pc_init".
 * In particular, "pc_init" represents the context of the loop,
 * whereas "pc" represents the context of the body of the loop and
 * has already had its domain extended with an infinite loop
 *
 *	{ [t] : t >= 0 }
 *
 * The for loop has the form
 *
 *	for (iv = init; cond; iv += inc)
 *		body;
 *
 * and is treated as
 *
 *	iv = init;
 *	while (cond) {
 *		body;
 *		iv += inc;
 *	}
 *
 * except that the skips resulting from any continue statements
 * in body do not apply to the increment, but are replaced by the skips
 * resulting from break statements.
 *
 * If the loop iterator is declared in the for loop, then it is killed before
 * and after the loop.
 */
static struct pet_scop *scop_from_non_affine_for(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc_init, __isl_take pet_context *pc,
	struct pet_state *state)
{
	int declared;
	isl_id *iv;
	pet_expr *expr_iv, *inc;
	struct pet_scop *scop_init, *scop;
	int type_size;
	struct pet_array *array;
	struct pet_scop *scop_kill;

	iv = pet_expr_access_get_id(tree->u.l.iv);
	pc = pet_context_clear_value(pc, iv);

	declared = tree->u.l.declared;

	scop_init = scop_from_for_init(tree, pc_init, state);

	expr_iv = pet_expr_copy(tree->u.l.iv);
	type_size = pet_expr_get_type_size(expr_iv);
	inc = pet_expr_copy(tree->u.l.inc);
	inc = pet_expr_new_binary(type_size, pet_op_add_assign, expr_iv, inc);

	scop = scop_from_non_affine_while(pet_expr_copy(tree->u.l.cond),
			pet_tree_get_loc(tree), tree->u.l.body, tree->label,
			inc, pet_context_copy(pc), state);

	scop = pet_scop_add_seq(state->ctx, scop_init, scop);

	pet_context_free(pc);

	if (!declared)
		return scop;

	array = extract_array(tree->u.l.iv, pc_init, state);
	if (array)
		array->declared = 1;
	scop_kill = kill(pet_tree_get_loc(tree), array, pc_init, state);
	scop = pet_scop_add_seq(state->ctx, scop_kill, scop);
	scop_kill = kill(pet_tree_get_loc(tree), array, pc_init, state);
	scop_kill = pet_scop_add_array(scop_kill, array);
	scop = pet_scop_add_seq(state->ctx, scop, scop_kill);

	return scop;
}

/* Given an access expression "expr", is the variable accessed by
 * "expr" assigned anywhere inside "tree"?
 */
static int is_assigned(__isl_keep pet_expr *expr, __isl_keep pet_tree *tree)
{
	int assigned = 0;
	isl_id *id;

	id = pet_expr_access_get_id(expr);
	assigned = pet_tree_writes(tree, id);
	isl_id_free(id);

	return assigned;
}

/* Are all nested access parameters in "pa" allowed given "tree".
 * In particular, is none of them written by anywhere inside "tree".
 *
 * If "tree" has any continue or break nodes in the current loop level,
 * then no nested access parameters are allowed.
 * In particular, if there is any nested access in a guard
 * for a piece of code containing a "continue", then we want to introduce
 * a separate statement for evaluating this guard so that we can express
 * that the result is false for all previous iterations.
 */
static int is_nested_allowed(__isl_keep isl_pw_aff *pa,
	__isl_keep pet_tree *tree)
{
	int i, nparam;

	if (!tree)
		return -1;

	if (!pet_nested_any_in_pw_aff(pa))
		return 1;

	if (pet_tree_has_continue_or_break(tree))
		return 0;

	nparam = isl_pw_aff_dim(pa, isl_dim_param);
	for (i = 0; i < nparam; ++i) {
		isl_id *id = isl_pw_aff_get_dim_id(pa, isl_dim_param, i);
		pet_expr *expr;
		int allowed;

		if (!pet_nested_in_id(id)) {
			isl_id_free(id);
			continue;
		}

		expr = pet_nested_extract_expr(id);
		allowed = pet_expr_get_type(expr) == pet_expr_access &&
			    !is_assigned(expr, tree);

		pet_expr_free(expr);
		isl_id_free(id);

		if (!allowed)
			return 0;
	}

	return 1;
}

/* Internal data structure for collect_local.
 * "pc" and "state" are needed to extract pet_arrays for the local variables.
 * "local" collects the results.
 */
struct pet_tree_collect_local_data {
	pet_context *pc;
	struct pet_state *state;
	isl_union_set *local;
};

/* Add the variable accessed by "var" to data->local.
 * We extract a representation of the variable from
 * the pet_array constructed using extract_array
 * to ensure consistency with the rest of the scop.
 */
static int add_local(struct pet_tree_collect_local_data *data,
	__isl_keep pet_expr *var)
{
	struct pet_array *array;
	isl_set *universe;

	array = extract_array(var, data->pc, data->state);
	if (!array)
		return -1;

	universe = isl_set_universe(isl_set_get_space(array->extent));
	data->local = isl_union_set_add_set(data->local, universe);
	pet_array_free(array);

	return 0;
}

/* If the node "tree" declares a variable, then add it to
 * data->local.
 */
static int extract_local_var(__isl_keep pet_tree *tree, void *user)
{
	enum pet_tree_type type;
	struct pet_tree_collect_local_data *data = user;

	type = pet_tree_get_type(tree);
	if (type == pet_tree_decl || type == pet_tree_decl_init)
		return add_local(data, tree->u.d.var);

	return 0;
}

/* If the node "tree" is a for loop that declares its induction variable,
 * then add it this induction variable to data->local.
 */
static int extract_local_iterator(__isl_keep pet_tree *tree, void *user)
{
	struct pet_tree_collect_local_data *data = user;

	if (pet_tree_get_type(tree) == pet_tree_for && tree->u.l.declared)
		return add_local(data, tree->u.l.iv);

	return 0;
}

/* Collect and return all local variables of the for loop represented
 * by "tree", with "scop" the corresponding pet_scop.
 * "pc" and "state" are needed to extract pet_arrays for the local variables.
 *
 * We collect not only the variables that are declared inside "tree",
 * but also the loop iterators that are declared anywhere inside
 * any possible macro statements in "scop".
 * The latter also appear as declared variable in the scop,
 * whereas other declared loop iterators only appear implicitly
 * in the iteration domains.
 */
static __isl_give isl_union_set *collect_local(struct pet_scop *scop,
	__isl_keep pet_tree *tree, __isl_keep pet_context *pc,
	struct pet_state *state)
{
	int i;
	isl_ctx *ctx;
	struct pet_tree_collect_local_data data = { pc, state };

	ctx = pet_tree_get_ctx(tree);
	data.local = isl_union_set_empty(isl_space_params_alloc(ctx, 0));

	if (pet_tree_foreach_sub_tree(tree, &extract_local_var, &data) < 0)
		return isl_union_set_free(data.local);

	for (i = 0; i < scop->n_stmt; ++i) {
		pet_tree *body = scop->stmts[i]->body;
		if (pet_tree_foreach_sub_tree(body, &extract_local_iterator,
						&data) < 0)
			return isl_union_set_free(data.local);
	}

	return data.local;
}

/* Add an independence to "scop" if the for node "tree" was marked
 * independent.
 * "domain" is the set of loop iterators, with the current for loop
 * innermost.  If "sign" is positive, then the inner iterator increases.
 * Otherwise it decreases.
 * "pc" and "state" are needed to extract pet_arrays for the local variables.
 *
 * If the tree was marked, then collect all local variables and
 * add an independence.
 */
static struct pet_scop *set_independence(struct pet_scop *scop,
	__isl_keep pet_tree *tree, __isl_keep isl_set *domain, int sign,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	isl_union_set *local;

	if (!tree->u.l.independent)
		return scop;

	local = collect_local(scop, tree, pc, state);
	scop = pet_scop_set_independent(scop, domain, local, sign);

	return scop;
}

/* Add a scop for assigning to the variable corresponding to the loop
 * iterator the result of adding the increment to the loop iterator
 * at the end of a loop body "scop" within the context "pc".
 * "tree" represents the for loop.
 *
 * The increment is of the form
 *
 *	iv = iv + inc
 *
 * Note that "iv" on the right hand side will be evaluated in terms
 * of the (possibly virtual) loop iterator, i.e., the inner dimension
 * of the domain, while "iv" on the left hand side will not be evaluated
 * (because it is a write) and will continue to refer to the original
 * variable.
 */
static __isl_give pet_scop *add_iterator_assignment(__isl_take pet_scop *scop,
	__isl_keep pet_tree *tree, __isl_keep pet_context *pc,
	struct pet_state *state)
{
	int type_size;
	pet_expr *expr, *iv, *inc;

	iv = pet_expr_copy(tree->u.l.iv);
	type_size = pet_expr_get_type_size(iv);
	iv = pet_expr_access_set_write(iv, 0);
	iv = pet_expr_access_set_read(iv, 1);
	inc = pet_expr_copy(tree->u.l.inc);
	expr = pet_expr_new_binary(type_size, pet_op_add, iv, inc);
	iv = pet_expr_copy(tree->u.l.iv);
	expr = pet_expr_new_binary(type_size, pet_op_assign, iv, expr);

	scop = scop_add_inc(scop, expr, pet_tree_get_loc(tree), pc, state);

	return scop;
}

/* Construct a pet_scop for a for tree with static affine initialization
 * and constant increment within the context "pc".
 * The domain of "pc" has already been extended with an (at this point
 * unbounded) inner loop iterator corresponding to the current for loop.
 *
 * The condition is allowed to contain nested accesses, provided
 * they are not being written to inside the body of the loop.
 * Otherwise, or if the condition is otherwise non-affine, the for loop is
 * essentially treated as a while loop, with iteration domain
 * { [l,i] : i >= init }, where l refers to the outer loop iterators.
 *
 * We extract a pet_scop for the body after intersecting the domain of "pc"
 *
 *	{ [l,i] : i >= init and condition' }
 *
 * or
 *
 *	{ [l,i] : i <= init and condition' }
 *
 * Where condition' is equal to condition if the latter is
 * a simple upper [lower] bound and a condition that is extended
 * to apply to all previous iterations otherwise.
 * Afterwards, the schedule of the pet_scop is extended with
 *
 *	{ [l,i] -> [i] }
 *
 * or
 *
 *	{ [l,i] -> [-i] }
 *
 * If the condition is non-affine, then we drop the condition from the
 * iteration domain and instead create a separate statement
 * for evaluating the condition.  The body is then filtered to depend
 * on the result of the condition evaluating to true on all iterations
 * up to the current iteration, while the evaluation the condition itself
 * is filtered to depend on the result of the condition evaluating to true
 * on all previous iterations.
 * The context of the scop representing the body is dropped
 * because we don't know how many times the body will be executed,
 * if at all.
 *
 * If the stride of the loop is not 1, then "i >= init" is replaced by
 *
 *	(exists a: i = init + stride * a and a >= 0)
 *
 * If the loop iterator i is unsigned, then wrapping may occur.
 * We therefore use a virtual iterator instead that does not wrap.
 * However, the condition in the code applies
 * to the wrapped value, so we need to change condition(l,i)
 * into condition([l,i % 2^width]).  Similarly, we replace all accesses
 * to the original iterator by the wrapping of the virtual iterator.
 * Note that there may be no need to perform this final wrapping
 * if the loop condition (after wrapping) satisfies certain conditions.
 * However, the is_simple_bound condition is not enough since it doesn't
 * check if there even is an upper bound.
 *
 * Wrapping on unsigned iterators can be avoided entirely if
 * the loop condition is simple, the loop iterator is incremented
 * [decremented] by one and the last value before wrapping cannot
 * possibly satisfy the loop condition.
 *
 * Valid outer iterators for a for loop are those for which the initial
 * value itself, the increment on each domain iteration and
 * the condition on both the initial value and
 * the result of incrementing the iterator for each iteration of the domain
 * can be evaluated.
 * If the loop condition is non-affine, then we only consider validity
 * of the initial value.
 *
 * If the loop iterator was not declared inside the loop header,
 * then the variable corresponding to this loop iterator is assigned
 * the result of adding the increment at the end of the loop body.
 * The assignment of the initial value is taken care of by
 * scop_from_affine_for_init.
 *
 * If the body contains any break, then we keep track of it in "skip"
 * (if the skip condition is affine) or it is handled in scop_add_break
 * (if the skip condition is not affine).
 * Note that the affine break condition needs to be considered with
 * respect to previous iterations in the virtual domain (if any).
 */
static struct pet_scop *scop_from_affine_for(__isl_keep pet_tree *tree,
	__isl_take isl_pw_aff *init_val, __isl_take isl_pw_aff *pa_inc,
	__isl_take isl_val *inc, __isl_take pet_context *pc,
	struct pet_state *state)
{
	isl_set *domain;
	isl_multi_aff *sched;
	isl_set *cond = NULL;
	isl_set *skip = NULL;
	isl_id *id_test = NULL, *id_break_test;
	struct pet_scop *scop, *scop_cond = NULL;
	int pos;
	int is_one;
	int is_unsigned;
	int is_simple;
	int is_virtual;
	int is_non_affine;
	int has_affine_break;
	int has_var_break;
	isl_map *rev_wrap = NULL;
	isl_map *init_val_map;
	isl_pw_aff *pa;
	isl_set *valid_init;
	isl_set *valid_cond;
	isl_set *valid_cond_init;
	isl_set *valid_cond_next;
	isl_set *valid_inc;
	pet_expr *cond_expr;
	pet_context *pc_nested;

	pos = pet_context_dim(pc) - 1;

	domain = pet_context_get_domain(pc);
	cond_expr = pet_expr_copy(tree->u.l.cond);
	cond_expr = pet_context_evaluate_expr(pc, cond_expr);
	pc_nested = pet_context_copy(pc);
	pc_nested = pet_context_set_allow_nested(pc_nested, 1);
	pa = pet_expr_extract_affine_condition(cond_expr, pc_nested);
	pet_context_free(pc_nested);
	pet_expr_free(cond_expr);

	valid_inc = isl_pw_aff_domain(pa_inc);

	is_unsigned = pet_expr_get_type_size(tree->u.l.iv) > 0;

	is_non_affine = isl_pw_aff_involves_nan(pa) ||
			!is_nested_allowed(pa, tree->u.l.body);
	if (is_non_affine)
		pa = isl_pw_aff_free(pa);

	valid_cond = isl_pw_aff_domain(isl_pw_aff_copy(pa));
	cond = isl_pw_aff_non_zero_set(pa);
	if (is_non_affine)
		cond = isl_set_universe(isl_set_get_space(domain));

	valid_cond = isl_set_coalesce(valid_cond);
	is_one = isl_val_is_one(inc) || isl_val_is_negone(inc);
	is_virtual = is_unsigned &&
		(!is_one || can_wrap(cond, tree->u.l.iv, inc));

	init_val_map = isl_map_from_pw_aff(isl_pw_aff_copy(init_val));
	init_val_map = isl_map_equate(init_val_map, isl_dim_in, pos,
					isl_dim_out, 0);
	valid_cond_init = enforce_subset(isl_map_domain(init_val_map),
					isl_set_copy(valid_cond));
	if (is_one && !is_virtual) {
		isl_set *cond;

		isl_pw_aff_free(init_val);
		pa = pet_expr_extract_comparison(
			isl_val_is_pos(inc) ? pet_op_ge : pet_op_le,
				tree->u.l.iv, tree->u.l.init, pc);
		valid_init = isl_pw_aff_domain(isl_pw_aff_copy(pa));
		valid_init = isl_set_eliminate(valid_init, isl_dim_set,
				    isl_set_dim(domain, isl_dim_set) - 1, 1);
		cond = isl_pw_aff_non_zero_set(pa);
		domain = isl_set_intersect(domain, cond);
	} else {
		isl_set *strided;

		valid_init = isl_pw_aff_domain(isl_pw_aff_copy(init_val));
		strided = strided_domain(init_val, isl_val_copy(inc));
		domain = isl_set_intersect(domain, strided);
	}

	if (is_virtual) {
		isl_multi_aff *wrap;
		wrap = compute_wrapping(isl_set_get_space(cond), tree->u.l.iv);
		pc = pet_context_preimage_domain(pc, wrap);
		rev_wrap = isl_map_from_multi_aff(wrap);
		rev_wrap = isl_map_reverse(rev_wrap);
		cond = isl_set_apply(cond, isl_map_copy(rev_wrap));
		valid_cond = isl_set_apply(valid_cond, isl_map_copy(rev_wrap));
		valid_inc = isl_set_apply(valid_inc, isl_map_copy(rev_wrap));
	}
	is_simple = is_simple_bound(cond, inc);
	if (!is_simple) {
		cond = isl_set_gist(cond, isl_set_copy(domain));
		is_simple = is_simple_bound(cond, inc);
	}
	if (!is_simple)
		cond = valid_for_each_iteration(cond,
				    isl_set_copy(domain), isl_val_copy(inc));
	cond = isl_set_align_params(cond, isl_set_get_space(domain));
	domain = isl_set_intersect(domain, cond);
	sched = map_to_last(pc, state->n_loop++, tree->label);
	if (isl_val_is_neg(inc))
		sched = isl_multi_aff_neg(sched);

	valid_cond_next = valid_on_next(valid_cond, isl_set_copy(domain),
					isl_val_copy(inc));
	valid_inc = enforce_subset(isl_set_copy(domain), valid_inc);

	pc = pet_context_intersect_domain(pc, isl_set_copy(domain));

	if (is_non_affine) {
		isl_space *space;
		isl_multi_pw_aff *test_index;
		space = isl_set_get_space(domain);
		test_index = pet_create_test_index(space, state->n_test++);
		scop_cond = scop_from_non_affine_condition(
				pet_expr_copy(tree->u.l.cond), state->n_stmt++,
				isl_multi_pw_aff_copy(test_index),
				pet_tree_get_loc(tree), pc);
		id_test = isl_multi_pw_aff_get_tuple_id(test_index,
							isl_dim_out);
		scop_cond = pet_scop_add_boolean_array(scop_cond,
				isl_set_copy(domain), test_index,
				state->int_size);
	}

	scop = scop_from_tree(tree->u.l.body, pc, state);
	has_affine_break = scop &&
				pet_scop_has_affine_skip(scop, pet_skip_later);
	if (has_affine_break)
		skip = pet_scop_get_affine_skip_domain(scop, pet_skip_later);
	has_var_break = scop && pet_scop_has_var_skip(scop, pet_skip_later);
	if (has_var_break)
		id_break_test = pet_scop_get_skip_id(scop, pet_skip_later);
	if (is_non_affine) {
		scop = pet_scop_reset_context(scop);
	}
	if (!tree->u.l.declared)
		scop = add_iterator_assignment(scop, tree, pc, state);
	scop = pet_scop_reset_skips(scop);
	scop = pet_scop_resolve_nested(scop);
	if (has_affine_break) {
		domain = apply_affine_break(domain, skip, isl_val_sgn(inc),
					    is_virtual, rev_wrap);
		scop = pet_scop_intersect_domain_prefix(scop,
							isl_set_copy(domain));
	}
	isl_map_free(rev_wrap);
	if (has_var_break)
		scop = scop_add_break(scop, id_break_test, isl_set_copy(domain),
					isl_val_copy(inc));
	if (is_non_affine)
		scop = scop_add_while(scop_cond, scop, id_test,
					isl_set_copy(domain),
					isl_val_copy(inc));
	else
		scop = set_independence(scop, tree, domain, isl_val_sgn(inc),
					pc, state);
	scop = pet_scop_embed(scop, domain, sched);
	if (is_non_affine) {
		isl_set_free(valid_inc);
	} else {
		valid_inc = isl_set_intersect(valid_inc, valid_cond_next);
		valid_inc = isl_set_intersect(valid_inc, valid_cond_init);
		valid_inc = isl_set_project_out(valid_inc, isl_dim_set, pos, 1);
		scop = pet_scop_restrict_context(scop, valid_inc);
	}

	isl_val_free(inc);

	valid_init = isl_set_project_out(valid_init, isl_dim_set, pos, 1);
	scop = pet_scop_restrict_context(scop, valid_init);

	pet_context_free(pc);
	return scop;
}

/* Construct a pet_scop for a for tree with static affine initialization
 * and constant increment within the context "pc_init".
 * In particular, "pc_init" represents the context of the loop,
 * whereas the domain of "pc" has already been extended with an (at this point
 * unbounded) inner loop iterator corresponding to the current for loop.
 *
 * If the loop iterator was not declared inside the loop header,
 * then add an assignment of the initial value to the loop iterator
 * before the loop.  The construction of a pet_scop for the loop itself,
 * including updates to the loop iterator, is handled by scop_from_affine_for.
 */
static __isl_give pet_scop *scop_from_affine_for_init(__isl_keep pet_tree *tree,
	__isl_take isl_pw_aff *init_val, __isl_take isl_pw_aff *pa_inc,
	__isl_take isl_val *inc, __isl_keep pet_context *pc_init,
	__isl_take pet_context *pc, struct pet_state *state)
{
	pet_scop *scop_init, *scop;

	if (!tree->u.l.declared)
		scop_init = scop_from_for_init(tree, pc_init, state);

	scop = scop_from_affine_for(tree, init_val, pa_inc, inc, pc, state);

	if (!tree->u.l.declared)
		scop = pet_scop_add_seq(state->ctx, scop_init, scop);

	return scop;
}

/* Construct a pet_scop for a for statement within the context of "pc".
 *
 * We update the context to reflect the writes to the loop variable and
 * the writes inside the body.
 *
 * Then we check if the initialization of the for loop
 * is a static affine value and the increment is a constant.
 * If so, we construct the pet_scop using scop_from_affine_for_init.
 * Otherwise, we treat the for loop as a while loop
 * in scop_from_non_affine_for.
 *
 * Note that the initialization and the increment are extracted
 * in a context where the current loop iterator has been added
 * to the context.  If these turn out not be affine, then we
 * have reconstruct the body context without an assignment
 * to this loop iterator, as this variable will then not be
 * treated as a dimension of the iteration domain, but as any
 * other variable.
 */
static struct pet_scop *scop_from_for(__isl_keep pet_tree *tree,
	__isl_keep pet_context *init_pc, struct pet_state *state)
{
	isl_id *iv;
	isl_val *inc;
	isl_pw_aff *pa_inc, *init_val;
	pet_context *pc, *pc_init_val;

	if (!tree)
		return NULL;

	iv = pet_expr_access_get_id(tree->u.l.iv);
	pc = pet_context_copy(init_pc);
	pc = pet_context_add_inner_iterator(pc, iv);
	pc = pet_context_clear_writes_in_tree(pc, tree->u.l.body);

	pc_init_val = pet_context_copy(pc);
	pc_init_val = pet_context_clear_value(pc_init_val, isl_id_copy(iv));
	init_val = pet_expr_extract_affine(tree->u.l.init, pc_init_val);
	pet_context_free(pc_init_val);
	pa_inc = pet_expr_extract_affine(tree->u.l.inc, pc);
	inc = pet_extract_cst(pa_inc);
	if (!pa_inc || !init_val || !inc)
		goto error;
	if (!isl_pw_aff_involves_nan(pa_inc) &&
	    !isl_pw_aff_involves_nan(init_val) && !isl_val_is_nan(inc))
		return scop_from_affine_for_init(tree, init_val, pa_inc, inc,
						init_pc, pc, state);

	isl_pw_aff_free(pa_inc);
	isl_pw_aff_free(init_val);
	isl_val_free(inc);
	pet_context_free(pc);

	pc = pet_context_copy(init_pc);
	pc = pet_context_add_infinite_loop(pc);
	pc = pet_context_clear_writes_in_tree(pc, tree->u.l.body);
	return scop_from_non_affine_for(tree, init_pc, pc, state);
error:
	isl_pw_aff_free(pa_inc);
	isl_pw_aff_free(init_val);
	isl_val_free(inc);
	pet_context_free(pc);
	return NULL;
}

/* Check whether "expr" is an affine constraint within the context "pc".
 */
static int is_affine_condition(__isl_keep pet_expr *expr,
	__isl_keep pet_context *pc)
{
	isl_pw_aff *pa;
	int is_affine;

	pa = pet_expr_extract_affine_condition(expr, pc);
	if (!pa)
		return -1;
	is_affine = !isl_pw_aff_involves_nan(pa);
	isl_pw_aff_free(pa);

	return is_affine;
}

/* Check if the given if statement is a conditional assignement
 * with a non-affine condition.
 *
 * In particular we check if "stmt" is of the form
 *
 *	if (condition)
 *		a = f(...);
 *	else
 *		a = g(...);
 *
 * where the condition is non-affine and a is some array or scalar access.
 */
static int is_conditional_assignment(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc)
{
	int equal;
	isl_ctx *ctx;
	pet_expr *expr1, *expr2;

	ctx = pet_tree_get_ctx(tree);
	if (!pet_options_get_detect_conditional_assignment(ctx))
		return 0;
	if (tree->type != pet_tree_if_else)
		return 0;
	if (tree->u.i.then_body->type != pet_tree_expr)
		return 0;
	if (tree->u.i.else_body->type != pet_tree_expr)
		return 0;
	expr1 = tree->u.i.then_body->u.e.expr;
	expr2 = tree->u.i.else_body->u.e.expr;
	if (pet_expr_get_type(expr1) != pet_expr_op)
		return 0;
	if (pet_expr_get_type(expr2) != pet_expr_op)
		return 0;
	if (pet_expr_op_get_type(expr1) != pet_op_assign)
		return 0;
	if (pet_expr_op_get_type(expr2) != pet_op_assign)
		return 0;
	expr1 = pet_expr_get_arg(expr1, 0);
	expr2 = pet_expr_get_arg(expr2, 0);
	equal = pet_expr_is_equal(expr1, expr2);
	pet_expr_free(expr1);
	pet_expr_free(expr2);
	if (equal < 0 || !equal)
		return 0;
	if (is_affine_condition(tree->u.i.cond, pc))
		return 0;

	return 1;
}

/* Given that "tree" is of the form
 *
 *	if (condition)
 *		a = f(...);
 *	else
 *		a = g(...);
 *
 * where a is some array or scalar access, construct a pet_scop
 * corresponding to this conditional assignment within the context "pc".
 * "cond_pa" is an affine expression with nested accesses representing
 * the condition.
 *
 * The constructed pet_scop then corresponds to the expression
 *
 *	a = condition ? f(...) : g(...)
 *
 * All access relations in f(...) are intersected with condition
 * while all access relation in g(...) are intersected with the complement.
 */
static struct pet_scop *scop_from_conditional_assignment(
	__isl_keep pet_tree *tree, __isl_take isl_pw_aff *cond_pa,
	__isl_take pet_context *pc, struct pet_state *state)
{
	int type_size;
	isl_set *cond, *comp;
	isl_multi_pw_aff *index;
	pet_expr *expr1, *expr2;
	pet_expr *pe_cond, *pe_then, *pe_else, *pe, *pe_write;
	struct pet_scop *scop;

	cond = isl_pw_aff_non_zero_set(isl_pw_aff_copy(cond_pa));
	comp = isl_pw_aff_zero_set(isl_pw_aff_copy(cond_pa));
	index = isl_multi_pw_aff_from_pw_aff(cond_pa);

	expr1 = tree->u.i.then_body->u.e.expr;
	expr2 = tree->u.i.else_body->u.e.expr;

	pe_cond = pet_expr_from_index(index);

	pe_then = pet_expr_get_arg(expr1, 1);
	pe_then = pet_context_evaluate_expr(pc, pe_then);
	pe_then = pet_expr_restrict(pe_then, cond);
	pe_else = pet_expr_get_arg(expr2, 1);
	pe_else = pet_context_evaluate_expr(pc, pe_else);
	pe_else = pet_expr_restrict(pe_else, comp);
	pe_write = pet_expr_get_arg(expr1, 0);
	pe_write = pet_context_evaluate_expr(pc, pe_write);

	pe = pet_expr_new_ternary(pe_cond, pe_then, pe_else);
	type_size = pet_expr_get_type_size(pe_write);
	pe = pet_expr_new_binary(type_size, pet_op_assign, pe_write, pe);

	scop = scop_from_evaluated_expr(pe, state->n_stmt++,
				pet_tree_get_loc(tree), pc);

	pet_context_free(pc);

	return scop;
}

/* Construct a pet_scop for a non-affine if statement within the context "pc".
 *
 * We create a separate statement that writes the result
 * of the non-affine condition to a virtual scalar.
 * A constraint requiring the value of this virtual scalar to be one
 * is added to the iteration domains of the then branch.
 * Similarly, a constraint requiring the value of this virtual scalar
 * to be zero is added to the iteration domains of the else branch, if any.
 * We combine the schedules as a sequence to ensure that the virtual scalar
 * is written before it is read.
 *
 * If there are any breaks or continues in the then and/or else
 * branches, then we may have to compute a new skip condition.
 * This is handled using a pet_skip_info object.
 * On initialization, the object checks if skip conditions need
 * to be computed.  If so, it does so in pet_skip_info_if_extract_index and
 * adds them in pet_skip_info_add.
 */
static struct pet_scop *scop_from_non_affine_if(__isl_keep pet_tree *tree,
	__isl_take pet_context *pc, struct pet_state *state)
{
	int has_else;
	isl_space *space;
	isl_set *domain;
	isl_multi_pw_aff *test_index;
	struct pet_skip_info skip;
	struct pet_scop *scop, *scop_then, *scop_else = NULL;

	has_else = tree->type == pet_tree_if_else;

	space = pet_context_get_space(pc);
	test_index = pet_create_test_index(space, state->n_test++);
	scop = scop_from_non_affine_condition(pet_expr_copy(tree->u.i.cond),
		state->n_stmt++, isl_multi_pw_aff_copy(test_index),
		pet_tree_get_loc(tree), pc);
	domain = pet_context_get_domain(pc);
	scop = pet_scop_add_boolean_array(scop, domain,
		isl_multi_pw_aff_copy(test_index), state->int_size);

	scop_then = scop_from_tree(tree->u.i.then_body, pc, state);
	if (has_else)
		scop_else = scop_from_tree(tree->u.i.else_body, pc, state);

	pet_skip_info_if_init(&skip, state->ctx, scop_then, scop_else,
					has_else, 0);
	pet_skip_info_if_extract_index(&skip, test_index, pc, state);

	scop_then = pet_scop_filter(scop_then,
					isl_multi_pw_aff_copy(test_index), 1);
	if (has_else) {
		scop_else = pet_scop_filter(scop_else, test_index, 0);
		scop_then = pet_scop_add_par(state->ctx, scop_then, scop_else);
	} else
		isl_multi_pw_aff_free(test_index);

	scop = pet_scop_add_seq(state->ctx, scop, scop_then);

	scop = pet_skip_info_add(&skip, scop);

	pet_context_free(pc);
	return scop;
}

/* Construct a pet_scop for an affine if statement within the context "pc".
 *
 * The condition is added to the iteration domains of the then branch,
 * while the opposite of the condition in added to the iteration domains
 * of the else branch, if any.
 *
 * If there are any breaks or continues in the then and/or else
 * branches, then we may have to compute a new skip condition.
 * This is handled using a pet_skip_info_if object.
 * On initialization, the object checks if skip conditions need
 * to be computed.  If so, it does so in pet_skip_info_if_extract_cond and
 * adds them in pet_skip_info_add.
 */
static struct pet_scop *scop_from_affine_if(__isl_keep pet_tree *tree,
	__isl_take isl_pw_aff *cond, __isl_take pet_context *pc,
	struct pet_state *state)
{
	int has_else;
	isl_ctx *ctx;
	isl_set *set, *complement;
	isl_set *valid;
	struct pet_skip_info skip;
	struct pet_scop *scop, *scop_then, *scop_else = NULL;
	pet_context *pc_body;

	ctx = pet_tree_get_ctx(tree);

	has_else = tree->type == pet_tree_if_else;

	valid = isl_pw_aff_domain(isl_pw_aff_copy(cond));
	set = isl_pw_aff_non_zero_set(isl_pw_aff_copy(cond));

	pc_body = pet_context_copy(pc);
	pc_body = pet_context_intersect_domain(pc_body, isl_set_copy(set));
	scop_then = scop_from_tree(tree->u.i.then_body, pc_body, state);
	pet_context_free(pc_body);
	if (has_else) {
		pc_body = pet_context_copy(pc);
		complement = isl_set_copy(valid);
		complement = isl_set_subtract(valid, isl_set_copy(set));
		pc_body = pet_context_intersect_domain(pc_body,
						    isl_set_copy(complement));
		scop_else = scop_from_tree(tree->u.i.else_body, pc_body, state);
		pet_context_free(pc_body);
	}

	pet_skip_info_if_init(&skip, ctx, scop_then, scop_else, has_else, 1);
	pet_skip_info_if_extract_cond(&skip, cond, pc, state);
	isl_pw_aff_free(cond);

	scop = pet_scop_restrict(scop_then, set);

	if (has_else) {
		scop_else = pet_scop_restrict(scop_else, complement);
		scop = pet_scop_add_par(ctx, scop, scop_else);
	}
	scop = pet_scop_resolve_nested(scop);
	scop = pet_scop_restrict_context(scop, valid);

	scop = pet_skip_info_add(&skip, scop);

	pet_context_free(pc);
	return scop;
}

/* Construct a pet_scop for an if statement within the context "pc".
 *
 * If the condition fits the pattern of a conditional assignment,
 * then it is handled by scop_from_conditional_assignment.
 * Note that the condition is only considered for a conditional assignment
 * if it is not static-affine.  However, it should still convert
 * to an affine expression when nesting is allowed.
 *
 * Otherwise, we check if the condition is affine.
 * If so, we construct the scop in scop_from_affine_if.
 * Otherwise, we construct the scop in scop_from_non_affine_if.
 *
 * We allow the condition to be dynamic, i.e., to refer to
 * scalars or array elements that may be written to outside
 * of the given if statement.  These nested accesses are then represented
 * as output dimensions in the wrapping iteration domain.
 * If it is also written _inside_ the then or else branch, then
 * we treat the condition as non-affine.
 * As explained in extract_non_affine_if, this will introduce
 * an extra statement.
 * For aesthetic reasons, we want this statement to have a statement
 * number that is lower than those of the then and else branches.
 * In order to evaluate if we will need such a statement, however, we
 * first construct scops for the then and else branches.
 * We therefore reserve a statement number if we might have to
 * introduce such an extra statement.
 */
static struct pet_scop *scop_from_if(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	int has_else;
	isl_pw_aff *cond;
	pet_expr *cond_expr;
	pet_context *pc_nested;

	if (!tree)
		return NULL;

	has_else = tree->type == pet_tree_if_else;

	pc = pet_context_copy(pc);
	pc = pet_context_clear_writes_in_tree(pc, tree->u.i.then_body);
	if (has_else)
		pc = pet_context_clear_writes_in_tree(pc, tree->u.i.else_body);

	cond_expr = pet_expr_copy(tree->u.i.cond);
	cond_expr = pet_context_evaluate_expr(pc, cond_expr);
	pc_nested = pet_context_copy(pc);
	pc_nested = pet_context_set_allow_nested(pc_nested, 1);
	cond = pet_expr_extract_affine_condition(cond_expr, pc_nested);
	pet_context_free(pc_nested);
	pet_expr_free(cond_expr);

	if (!cond) {
		pet_context_free(pc);
		return NULL;
	}

	if (isl_pw_aff_involves_nan(cond)) {
		isl_pw_aff_free(cond);
		return scop_from_non_affine_if(tree, pc, state);
	}

	if (is_conditional_assignment(tree, pc))
		return scop_from_conditional_assignment(tree, cond, pc, state);

	if ((!is_nested_allowed(cond, tree->u.i.then_body) ||
	     (has_else && !is_nested_allowed(cond, tree->u.i.else_body)))) {
		isl_pw_aff_free(cond);
		return scop_from_non_affine_if(tree, pc, state);
	}

	return scop_from_affine_if(tree, cond, pc, state);
}

/* Return a one-dimensional multi piecewise affine expression that is equal
 * to the constant 1 and is defined over the given domain.
 */
static __isl_give isl_multi_pw_aff *one_mpa(__isl_take isl_space *space)
{
	isl_local_space *ls;
	isl_aff *aff;

	ls = isl_local_space_from_space(space);
	aff = isl_aff_zero_on_domain(ls);
	aff = isl_aff_set_constant_si(aff, 1);

	return isl_multi_pw_aff_from_pw_aff(isl_pw_aff_from_aff(aff));
}

/* Construct a pet_scop for a continue statement with the given domain space.
 *
 * We simply create an empty scop with a universal pet_skip_now
 * skip condition.  This skip condition will then be taken into
 * account by the enclosing loop construct, possibly after
 * being incorporated into outer skip conditions.
 */
static struct pet_scop *scop_from_continue(__isl_keep pet_tree *tree,
	__isl_take isl_space *space)
{
	struct pet_scop *scop;

	scop = pet_scop_empty(isl_space_copy(space));

	scop = pet_scop_set_skip(scop, pet_skip_now, one_mpa(space));

	return scop;
}

/* Construct a pet_scop for a break statement with the given domain space.
 *
 * We simply create an empty scop with both a universal pet_skip_now
 * skip condition and a universal pet_skip_later skip condition.
 * These skip conditions will then be taken into
 * account by the enclosing loop construct, possibly after
 * being incorporated into outer skip conditions.
 */
static struct pet_scop *scop_from_break(__isl_keep pet_tree *tree,
	__isl_take isl_space *space)
{
	struct pet_scop *scop;
	isl_multi_pw_aff *skip;

	scop = pet_scop_empty(isl_space_copy(space));

	skip = one_mpa(space);
	scop = pet_scop_set_skip(scop, pet_skip_now,
				    isl_multi_pw_aff_copy(skip));
	scop = pet_scop_set_skip(scop, pet_skip_later, skip);

	return scop;
}

/* Extract a clone of the kill statement "stmt".
 * The domain of the clone is given by "domain".
 */
static struct pet_scop *extract_kill(__isl_keep isl_set *domain,
	struct pet_stmt *stmt, struct pet_state *state)
{
	pet_expr *kill;
	isl_space *space;
	isl_multi_pw_aff *mpa;
	pet_tree *tree;

	if (!domain || !stmt)
		return NULL;

	kill = pet_tree_expr_get_expr(stmt->body);
	space = pet_stmt_get_space(stmt);
	space = isl_space_map_from_set(space);
	mpa = isl_multi_pw_aff_identity(space);
	mpa = isl_multi_pw_aff_reset_tuple_id(mpa, isl_dim_in);
	kill = pet_expr_update_domain(kill, mpa);
	tree = pet_tree_new_expr(kill);
	tree = pet_tree_set_loc(tree, pet_loc_copy(stmt->loc));
	stmt = pet_stmt_from_pet_tree(isl_set_copy(domain),
			state->n_stmt++, tree);
	return pet_scop_from_pet_stmt(isl_set_get_space(domain), stmt);
}

/* Extract a clone of the kill statements in "scop".
 * The domain of each clone is given by "domain".
 * "scop" is expected to have been created from a DeclStmt
 * and should have (one of) the kill(s) as its first statement.
 * If "scop" was created from a declaration group, then there
 * may be multiple kill statements inside.
 */
static struct pet_scop *extract_kills(__isl_keep isl_set *domain,
	struct pet_scop *scop, struct pet_state *state)
{
	isl_ctx *ctx;
	struct pet_stmt *stmt;
	struct pet_scop *kill;
	int i;

	if (!domain || !scop)
		return NULL;
	ctx = isl_set_get_ctx(domain);
	if (scop->n_stmt < 1)
		isl_die(ctx, isl_error_internal,
			"expecting at least one statement", return NULL);
	stmt = scop->stmts[0];
	if (!pet_stmt_is_kill(stmt))
		isl_die(ctx, isl_error_internal,
			"expecting kill statement", return NULL);

	kill = extract_kill(domain, stmt, state);

	for (i = 1; i < scop->n_stmt; ++i) {
		struct pet_scop *kill_i;

		stmt = scop->stmts[i];
		if (!pet_stmt_is_kill(stmt))
			continue;

		kill_i = extract_kill(domain, stmt, state);
		kill = pet_scop_add_par(ctx, kill, kill_i);
	}

	return kill;
}

/* Has "tree" been created from a DeclStmt?
 * That is, is it either a declaration or a group of declarations?
 */
static int tree_is_decl(__isl_keep pet_tree *tree)
{
	int is_decl;
	int i;

	if (!tree)
		return -1;
	is_decl = pet_tree_is_decl(tree);
	if (is_decl < 0 || is_decl)
		return is_decl;

	if (tree->type != pet_tree_block)
		return 0;
	if (pet_tree_block_get_block(tree))
		return 0;
	if (tree->u.b.n == 0)
		return 0;

	for (i = 0; i < tree->u.b.n; ++i) {
		is_decl = tree_is_decl(tree->u.b.child[i]);
		if (is_decl < 0 || !is_decl)
			return is_decl;
	}

	return 1;
}

/* Does "tree" represent an assignment to a variable?
 *
 * The assignment may be one of
 * - a declaration with initialization
 * - an expression with a top-level assignment operator
 */
static int is_assignment(__isl_keep pet_tree *tree)
{
	if (!tree)
		return 0;
	if (tree->type == pet_tree_decl_init)
		return 1;
	return pet_tree_is_assign(tree);
}

/* Update "pc" by taking into account the assignment performed by "tree",
 * where "tree" satisfies is_assignment.
 *
 * In particular, if the lhs of the assignment is a scalar variable and
 * if the rhs is an affine expression, then keep track of this value in "pc"
 * so that we can plug it in when we later come across the same variable.
 *
 * Any previously assigned value to the variable has already been removed
 * by scop_handle_writes.
 */
static __isl_give pet_context *handle_assignment(__isl_take pet_context *pc,
	__isl_keep pet_tree *tree)
{
	pet_expr *var, *val;
	isl_id *id;
	isl_pw_aff *pa;

	if (pet_tree_get_type(tree) == pet_tree_decl_init) {
		var = pet_tree_decl_get_var(tree);
		val = pet_tree_decl_get_init(tree);
	} else {
		pet_expr *expr;
		expr = pet_tree_expr_get_expr(tree);
		var = pet_expr_get_arg(expr, 0);
		val = pet_expr_get_arg(expr, 1);
		pet_expr_free(expr);
	}

	if (!pet_expr_is_scalar_access(var)) {
		pet_expr_free(var);
		pet_expr_free(val);
		return pc;
	}

	pa = pet_expr_extract_affine(val, pc);
	if (!pa)
		pc = pet_context_free(pc);

	if (!isl_pw_aff_involves_nan(pa)) {
		id = pet_expr_access_get_id(var);
		pc = pet_context_set_value(pc, id, pa);
	} else {
		isl_pw_aff_free(pa);
	}
	pet_expr_free(var);
	pet_expr_free(val);

	return pc;
}

/* Mark all arrays in "scop" as being exposed.
 */
static struct pet_scop *mark_exposed(struct pet_scop *scop)
{
	int i;

	if (!scop)
		return NULL;
	for (i = 0; i < scop->n_array; ++i)
		scop->arrays[i]->exposed = 1;
	return scop;
}

/* Try and construct a pet_scop corresponding to (part of)
 * a sequence of statements within the context "pc".
 *
 * After extracting a statement, we update "pc"
 * based on the top-level assignments in the statement
 * so that we can exploit them in subsequent statements in the same block.
 * Top-level affine assumptions are also recorded in the context.
 *
 * If there are any breaks or continues in the individual statements,
 * then we may have to compute a new skip condition.
 * This is handled using a pet_skip_info object.
 * On initialization, the object checks if skip conditions need
 * to be computed.  If so, it does so in pet_skip_info_seq_extract and
 * adds them in pet_skip_info_add.
 *
 * If "block" is set, then we need to insert kill statements at
 * the end of the block for any array that has been declared by
 * one of the statements in the sequence.  Each of these declarations
 * results in the construction of a kill statement at the place
 * of the declaration, so we simply collect duplicates of
 * those kill statements and append these duplicates to the constructed scop.
 *
 * If "block" is not set, then any array declared by one of the statements
 * in the sequence is marked as being exposed.
 *
 * If autodetect is set, then we allow the extraction of only a subrange
 * of the sequence of statements.  However, if there is at least one statement
 * for which we could not construct a scop and the final range contains
 * either no statements or at least one kill, then we discard the entire
 * range.
 */
static struct pet_scop *scop_from_block(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	int i;
	isl_ctx *ctx;
	isl_space *space;
	isl_set *domain;
	struct pet_scop *scop, *kills;

	ctx = pet_tree_get_ctx(tree);

	space = pet_context_get_space(pc);
	domain = pet_context_get_domain(pc);
	pc = pet_context_copy(pc);
	scop = pet_scop_empty(isl_space_copy(space));
	kills = pet_scop_empty(space);
	for (i = 0; i < tree->u.b.n; ++i) {
		struct pet_scop *scop_i;

		if (pet_scop_has_affine_skip(scop, pet_skip_now))
			pc = apply_affine_continue(pc, scop);
		scop_i = scop_from_tree(tree->u.b.child[i], pc, state);
		if (pet_tree_is_assume(tree->u.b.child[i]))
			pc = scop_add_affine_assumption(scop_i, pc);
		pc = scop_handle_writes(scop_i, pc);
		if (is_assignment(tree->u.b.child[i]))
			pc = handle_assignment(pc, tree->u.b.child[i]);
		struct pet_skip_info skip;
		pet_skip_info_seq_init(&skip, ctx, scop, scop_i);
		pet_skip_info_seq_extract(&skip, pc, state);
		if (scop_i && tree_is_decl(tree->u.b.child[i])) {
			if (tree->u.b.block) {
				struct pet_scop *kill;
				kill = extract_kills(domain, scop_i, state);
				kills = pet_scop_add_par(ctx, kills, kill);
			} else
				scop_i = mark_exposed(scop_i);
		}
		scop = pet_scop_add_seq(ctx, scop, scop_i);

		scop = pet_skip_info_add(&skip, scop);

		if (!scop)
			break;
	}
	isl_set_free(domain);

	scop = pet_scop_add_seq(ctx, scop, kills);

	pet_context_free(pc);

	return scop;
}

/* Internal data structure for extract_declared_arrays.
 *
 * "pc" and "state" are used to create pet_array objects and kill statements.
 * "any" is initialized to 0 by the caller and set to 1 as soon as we have
 * found any declared array.
 * "scop" has been initialized by the caller and is used to attach
 * the created pet_array objects.
 * "kill_before" and "kill_after" are created and updated by
 * extract_declared_arrays to collect the kills of the arrays.
 */
struct pet_tree_extract_declared_arrays_data {
	pet_context *pc;
	struct pet_state *state;

	isl_ctx *ctx;

	int any;
	struct pet_scop *scop;
	struct pet_scop *kill_before;
	struct pet_scop *kill_after;
};

/* Check if the node "node" declares any array or scalar.
 * If so, create the corresponding pet_array and attach it to data->scop.
 * Additionally, create two kill statements for the array and add them
 * to data->kill_before and data->kill_after.
 */
static int extract_declared_arrays(__isl_keep pet_tree *node, void *user)
{
	enum pet_tree_type type;
	struct pet_tree_extract_declared_arrays_data *data = user;
	struct pet_array *array;
	struct pet_scop *scop_kill;
	pet_expr *var;

	type = pet_tree_get_type(node);
	if (type == pet_tree_decl || type == pet_tree_decl_init)
		var = node->u.d.var;
	else if (type == pet_tree_for && node->u.l.declared)
		var = node->u.l.iv;
	else
		return 0;

	array = extract_array(var, data->pc, data->state);
	if (array)
		array->declared = 1;
	data->scop = pet_scop_add_array(data->scop, array);

	scop_kill = kill(pet_tree_get_loc(node), array, data->pc, data->state);
	if (!data->any)
		data->kill_before = scop_kill;
	else
		data->kill_before = pet_scop_add_par(data->ctx,
						data->kill_before, scop_kill);

	scop_kill = kill(pet_tree_get_loc(node), array, data->pc, data->state);
	if (!data->any)
		data->kill_after = scop_kill;
	else
		data->kill_after = pet_scop_add_par(data->ctx,
						data->kill_after, scop_kill);

	data->any = 1;

	return 0;
}

/* Convert a pet_tree that consists of more than a single leaf
 * to a pet_scop with a single statement encapsulating the entire pet_tree.
 * Do so within the context of "pc", taking into account the writes inside
 * "tree".  That is, first clear any previously assigned values to variables
 * that are written by "tree".
 *
 * After constructing the core scop, we also look for any arrays (or scalars)
 * that are declared inside "tree".  Each of those arrays is marked as
 * having been declared and kill statements for these arrays
 * are introduced before and after the core scop.
 * Note that the input tree is not a leaf so that the declaration
 * cannot occur at the outer level.
 */
static struct pet_scop *scop_from_tree_macro(__isl_take pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	struct pet_tree_extract_declared_arrays_data data = { pc, state };

	data.pc = pet_context_copy(data.pc);
	data.pc = pet_context_clear_writes_in_tree(data.pc, tree);
	data.scop = scop_from_unevaluated_tree(pet_tree_copy(tree),
						state->n_stmt++, data.pc);

	data.any = 0;
	data.ctx = pet_context_get_ctx(data.pc);
	if (pet_tree_foreach_sub_tree(tree, &extract_declared_arrays,
					&data) < 0)
		data.scop = pet_scop_free(data.scop);
	pet_tree_free(tree);
	pet_context_free(data.pc);

	if (!data.any)
		return data.scop;

	data.scop = pet_scop_add_seq(data.ctx, data.kill_before, data.scop);
	data.scop = pet_scop_add_seq(data.ctx, data.scop, data.kill_after);

	return data.scop;
}

/* Construct a pet_scop that corresponds to the pet_tree "tree"
 * within the context "pc" by calling the appropriate function
 * based on the type of "tree".
 *
 * If the initially constructed pet_scop turns out to involve
 * dynamic control and if the user has requested an encapsulation
 * of all dynamic control, then this pet_scop is discarded and
 * a new pet_scop is created with a single statement representing
 * the entire "tree".
 * However, if the scop contains any active continue or break,
 * then we need to include the loop containing the continue or break
 * in the encapsulation.  We therefore postpone the encapsulation
 * until we have constructed a pet_scop for this enclosing loop.
 */
static struct pet_scop *scop_from_tree(__isl_keep pet_tree *tree,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	isl_ctx *ctx;
	struct pet_scop *scop = NULL;

	if (!tree)
		return NULL;

	ctx = pet_tree_get_ctx(tree);
	switch (tree->type) {
	case pet_tree_error:
		return NULL;
	case pet_tree_block:
		return scop_from_block(tree, pc, state);
	case pet_tree_break:
		return scop_from_break(tree, pet_context_get_space(pc));
	case pet_tree_continue:
		return scop_from_continue(tree, pet_context_get_space(pc));
	case pet_tree_decl:
	case pet_tree_decl_init:
		return scop_from_decl(tree, pc, state);
	case pet_tree_expr:
		return scop_from_tree_expr(tree, pc, state);
	case pet_tree_return:
		return scop_from_return(tree, pc, state);
	case pet_tree_if:
	case pet_tree_if_else:
		scop = scop_from_if(tree, pc, state);
		break;
	case pet_tree_for:
		scop = scop_from_for(tree, pc, state);
		break;
	case pet_tree_while:
		scop = scop_from_while(tree, pc, state);
		break;
	case pet_tree_infinite_loop:
		scop = scop_from_infinite_for(tree, pc, state);
		break;
	}

	if (!scop)
		return NULL;

	if (!pet_options_get_encapsulate_dynamic_control(ctx) ||
	    !pet_scop_has_data_dependent_conditions(scop) ||
	    pet_scop_has_var_skip(scop, pet_skip_now))
		return scop;

	pet_scop_free(scop);
	return scop_from_tree_macro(pet_tree_copy(tree), pc, state);
}

/* If "tree" has a label that is of the form S_<nr>, then make
 * sure that state->n_stmt is greater than nr to ensure that
 * we will not generate S_<nr> ourselves.
 */
static int set_first_stmt(__isl_keep pet_tree *tree, void *user)
{
	struct pet_state *state = user;
	const char *name;
	int nr;

	if (!tree)
		return -1;
	if (!tree->label)
		return 0;
	name = isl_id_get_name(tree->label);
	if (strncmp(name, "S_", 2) != 0)
		return 0;
	nr = atoi(name + 2);
	if (nr >= state->n_stmt)
		state->n_stmt = nr + 1;

	return 0;
}

/* Construct a pet_scop that corresponds to the pet_tree "tree".
 * "int_size" is the number of bytes need to represent an integer.
 * "extract_array" is a callback that we can use to create a pet_array
 * that corresponds to the variable accessed by an expression.
 *
 * Initialize the global state, construct a context and then
 * construct the pet_scop by recursively visiting the tree.
 *
 * state.n_stmt is initialized to point beyond any explicit S_<nr> label.
 */
struct pet_scop *pet_scop_from_pet_tree(__isl_take pet_tree *tree, int int_size,
	struct pet_array *(*extract_array)(__isl_keep pet_expr *access,
		__isl_keep pet_context *pc, void *user), void *user,
	__isl_keep pet_context *pc)
{
	struct pet_scop *scop;
	struct pet_state state = { 0 };

	if (!tree)
		return NULL;

	state.ctx = pet_tree_get_ctx(tree);
	state.int_size = int_size;
	state.extract_array = extract_array;
	state.user = user;
	if (pet_tree_foreach_sub_tree(tree, &set_first_stmt, &state) < 0)
		tree = pet_tree_free(tree);

	scop = scop_from_tree(tree, pc, &state);
	scop = pet_scop_set_loc(scop, pet_tree_get_loc(tree));

	pet_tree_free(tree);

	if (scop)
		scop->context = isl_set_params(scop->context);

	return scop;
}
