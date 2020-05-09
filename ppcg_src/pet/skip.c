/*
 * Copyright 2012      Leiden University. All rights reserved.
 * Copyright 2013-2014 Ecole Normale Superieure. All rights reserved.
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

#include <isl/val.h>
#include <isl/space.h>
#include <isl/local_space.h>
#include <isl/aff.h>
#include <isl/set.h>

#include "expr.h"
#include "loc.h"
#include "scop.h"
#include "skip.h"
#include "tree.h"

/* Do we need to construct a skip condition of the given type
 * on an if statement, given that the if condition is non-affine?
 *
 * pet_scop_filter_skip can only handle the case where the if condition
 * holds (the then branch) and the skip condition is universal.
 * In any other case, we need to construct a new skip condition.
 */
static int if_need_skip_non(struct pet_scop *scop_then,
	struct pet_scop *scop_else, int have_else, enum pet_skip type)
{
	if (have_else && scop_else && pet_scop_has_skip(scop_else, type))
		return 1;
	if (scop_then && pet_scop_has_skip(scop_then, type) &&
	    !pet_scop_has_universal_skip(scop_then, type))
		return 1;
	return 0;
}

/* Do we need to construct a skip condition of the given type
 * on an if statement, given that the if condition is affine?
 *
 * There is no need to construct a new skip condition if all
 * the skip conditions are affine.
 */
static int if_need_skip_aff(struct pet_scop *scop_then,
	struct pet_scop *scop_else, int have_else, enum pet_skip type)
{
	if (scop_then && pet_scop_has_var_skip(scop_then, type))
		return 1;
	if (have_else && scop_else && pet_scop_has_var_skip(scop_else, type))
		return 1;
	return 0;
}

/* Do we need to construct a skip condition of the given type
 * on an if statement?
 */
static int if_need_skip(struct pet_scop *scop_then, struct pet_scop *scop_else,
	int have_else, enum pet_skip type, int affine)
{
	if (affine)
		return if_need_skip_aff(scop_then, scop_else, have_else, type);
	else
		return if_need_skip_non(scop_then, scop_else, have_else, type);
}

/* Construct an affine expression pet_expr that evaluates
 * to the constant "val" on "space".
 */
static __isl_give pet_expr *universally(__isl_take isl_space *space, int val)
{
	isl_ctx *ctx;
	isl_local_space *ls;
	isl_aff *aff;
	isl_multi_pw_aff *mpa;

	ctx = isl_space_get_ctx(space);
	ls = isl_local_space_from_space(space);
	aff = isl_aff_val_on_domain(ls, isl_val_int_from_si(ctx, val));
	mpa = isl_multi_pw_aff_from_pw_aff(isl_pw_aff_from_aff(aff));

	return pet_expr_from_index(mpa);
}

/* Construct an affine expression pet_expr that evaluates
 * to the constant 1 on "space".
 */
static __isl_give pet_expr *universally_true(__isl_take isl_space *space)
{
	return universally(space, 1);
}

/* Construct an affine expression pet_expr that evaluates
 * to the constant 0 on "space".
 */
static __isl_give pet_expr *universally_false(__isl_take isl_space *space)
{
	return universally(space, 0);
}

/* Given an index expression "test_index" for the if condition,
 * an index expression "skip_index" for the skip condition and
 * scops for the then and else branches, construct a scop for
 * computing "skip_index" within the context "pc".
 *
 * The computed scop contains a single statement that essentially does
 *
 *	skip_index = test_cond ? skip_cond_then : skip_cond_else
 *
 * If the skip conditions of the then and/or else branch are not affine,
 * then they need to be filtered by test_index.
 * If they are missing, then this means the skip condition is false.
 *
 * Since we are constructing a skip condition for the if statement,
 * the skip conditions on the then and else branches are removed.
 */
static struct pet_scop *extract_skip_if(__isl_take isl_multi_pw_aff *test_index,
	__isl_take isl_multi_pw_aff *skip_index,
	struct pet_scop *scop_then, struct pet_scop *scop_else, int have_else,
	enum pet_skip type, __isl_keep pet_context *pc, struct pet_state *state)
{
	pet_expr *expr_then, *expr_else, *expr, *expr_skip;
	pet_tree *tree;
	struct pet_stmt *stmt;
	struct pet_scop *scop;
	isl_ctx *ctx;
	isl_set *domain;

	if (!scop_then)
		goto error;
	if (have_else && !scop_else)
		goto error;

	ctx = isl_multi_pw_aff_get_ctx(test_index);

	if (pet_scop_has_skip(scop_then, type)) {
		expr_then = pet_scop_get_skip_expr(scop_then, type);
		pet_scop_reset_skip(scop_then, type);
		if (!pet_expr_is_affine(expr_then))
			expr_then = pet_expr_filter(expr_then,
					isl_multi_pw_aff_copy(test_index), 1);
	} else
		expr_then = universally_false(pet_context_get_space(pc));

	if (have_else && pet_scop_has_skip(scop_else, type)) {
		expr_else = pet_scop_get_skip_expr(scop_else, type);
		pet_scop_reset_skip(scop_else, type);
		if (!pet_expr_is_affine(expr_else))
			expr_else = pet_expr_filter(expr_else,
					isl_multi_pw_aff_copy(test_index), 0);
	} else
		expr_else = universally_false(pet_context_get_space(pc));

	expr = pet_expr_from_index(test_index);
	expr = pet_expr_new_ternary(expr, expr_then, expr_else);
	expr_skip = pet_expr_from_index(isl_multi_pw_aff_copy(skip_index));
	expr_skip = pet_expr_access_set_write(expr_skip, 1);
	expr_skip = pet_expr_access_set_read(expr_skip, 0);
	expr = pet_expr_new_binary(1, pet_op_assign, expr_skip, expr);
	domain = pet_context_get_domain(pc);
	tree = pet_tree_new_expr(expr);
	stmt = pet_stmt_from_pet_tree(isl_set_copy(domain),
					state->n_stmt++, tree);

	scop = pet_scop_from_pet_stmt(pet_context_get_space(pc), stmt);
	scop = pet_scop_add_boolean_array(scop, domain, skip_index,
					state->int_size);

	return scop;
error:
	isl_multi_pw_aff_free(test_index);
	isl_multi_pw_aff_free(skip_index);
	return NULL;
}

/* Is scop's skip_now condition equal to its skip_later condition?
 * In particular, this means that it either has no skip_now condition
 * or both a skip_now and a skip_later condition (that are equal to each other).
 */
static int skip_equals_skip_later(struct pet_scop *scop)
{
	int has_skip_now, has_skip_later;
	int equal;
	isl_multi_pw_aff *skip_now, *skip_later;

	if (!scop)
		return 0;
	has_skip_now = pet_scop_has_skip(scop, pet_skip_now);
	has_skip_later = pet_scop_has_skip(scop, pet_skip_later);
	if (has_skip_now != has_skip_later)
		return 0;
	if (!has_skip_now)
		return 1;

	skip_now = pet_scop_get_skip(scop, pet_skip_now);
	skip_later = pet_scop_get_skip(scop, pet_skip_later);
	equal = isl_multi_pw_aff_is_equal(skip_now, skip_later);
	isl_multi_pw_aff_free(skip_now);
	isl_multi_pw_aff_free(skip_later);

	return equal;
}

/* Drop the skip conditions of type pet_skip_later from scop1 and scop2.
 */
static void drop_skip_later(struct pet_scop *scop1, struct pet_scop *scop2)
{
	pet_scop_reset_skip(scop1, pet_skip_later);
	pet_scop_reset_skip(scop2, pet_skip_later);
}

/* Do we need to construct any skip condition?
 */
int pet_skip_info_has_skip(struct pet_skip_info *skip)
{
	return skip->skip[pet_skip_now] || skip->skip[pet_skip_later];
}

/* Initialize a pet_skip_info_if structure based on the then and else branches
 * and based on whether the if condition is affine or not.
 */
void pet_skip_info_if_init(struct pet_skip_info *skip, isl_ctx *ctx,
	struct pet_scop *scop_then, struct pet_scop *scop_else,
	int have_else, int affine)
{
	skip->ctx = ctx;
	skip->type = have_else ? pet_skip_if_else : pet_skip_if;
	skip->u.i.scop_then = scop_then;
	skip->u.i.scop_else = scop_else;

	skip->skip[pet_skip_now] =
	    if_need_skip(scop_then, scop_else, have_else, pet_skip_now, affine);
	skip->equal = skip->skip[pet_skip_now] &&
		skip_equals_skip_later(scop_then) &&
		(!have_else || skip_equals_skip_later(scop_else));
	skip->skip[pet_skip_later] = skip->skip[pet_skip_now] &&
		!skip->equal &&
		if_need_skip(scop_then, scop_else, have_else,
			    pet_skip_later, affine);
}

/* If we need to construct a skip condition of the given type,
 * then do so now, within the context "pc".
 *
 * "mpa" represents the if condition.
 */
static void pet_skip_info_if_extract_type(struct pet_skip_info *skip,
	__isl_keep isl_multi_pw_aff *mpa, enum pet_skip type,
	 __isl_keep pet_context *pc, struct pet_state *state)
{
	isl_space *space;

	if (!skip->skip[type])
		return;

	space = pet_context_get_space(pc);
	skip->index[type] = pet_create_test_index(space, state->n_test++);
	skip->scop[type] = extract_skip_if(isl_multi_pw_aff_copy(mpa),
				isl_multi_pw_aff_copy(skip->index[type]),
				skip->u.i.scop_then, skip->u.i.scop_else,
				skip->type == pet_skip_if_else, type,
				pc, state);
}

/* Construct the required skip conditions within the context "pc",
 * given the if condition "index".
 */
void pet_skip_info_if_extract_index(struct pet_skip_info *skip,
	__isl_keep isl_multi_pw_aff *index, __isl_keep pet_context *pc,
	struct pet_state *state)
{
	pet_skip_info_if_extract_type(skip, index, pet_skip_now, pc, state);
	pet_skip_info_if_extract_type(skip, index, pet_skip_later, pc, state);
	if (skip->equal)
		drop_skip_later(skip->u.i.scop_then, skip->u.i.scop_else);
}

/* Construct the required skip conditions within the context "pc",
 * given the if condition "cond".
 */
void pet_skip_info_if_extract_cond(struct pet_skip_info *skip,
	__isl_keep isl_pw_aff *cond, __isl_keep pet_context *pc,
	struct pet_state *state)
{
	isl_multi_pw_aff *test;

	if (!skip->skip[pet_skip_now] && !skip->skip[pet_skip_later])
		return;

	test = isl_multi_pw_aff_from_pw_aff(isl_pw_aff_copy(cond));
	pet_skip_info_if_extract_index(skip, test, pc, state);
	isl_multi_pw_aff_free(test);
}

/* Add the scops for computing the skip conditions to "main".
 *
 * If two scops are added, then they can be executed in parallel,
 * but both scops need to be executed after the original statement.
 * However, if "main" has any skip conditions, then they do not
 * apply to the scops for computing skip conditions, so we temporarily
 * remove the skip conditions while adding the scops.
 */
struct pet_scop *pet_skip_info_add_scops(struct pet_skip_info *skip,
	struct pet_scop *main)
{
	int has_skip_now;
	isl_multi_pw_aff *skip_now;
	struct pet_scop *scop_skip;

	if (!skip->skip[pet_skip_now] && !skip->skip[pet_skip_later])
		return main;

	has_skip_now = pet_scop_has_skip(main, pet_skip_now);
	if (has_skip_now)
		skip_now = pet_scop_get_skip(main, pet_skip_now);
	pet_scop_reset_skip(main, pet_skip_now);

	if (skip->skip[pet_skip_now] && skip->skip[pet_skip_later])
		scop_skip = pet_scop_add_par(skip->ctx,
					skip->scop[pet_skip_now],
					skip->scop[pet_skip_later]);
	else if (skip->skip[pet_skip_now])
		scop_skip = skip->scop[pet_skip_now];
	else
		scop_skip = skip->scop[pet_skip_later];
	main = pet_scop_add_seq(skip->ctx, main, scop_skip);
	skip->scop[pet_skip_now] = NULL;
	skip->scop[pet_skip_later] = NULL;

	if (has_skip_now)
		main = pet_scop_set_skip(main, pet_skip_now, skip_now);

	return main;
}

/* Add the computed skip condition of the give type to "main".
 *
 * If equal is set, then we only computed a skip condition for pet_skip_now,
 * but we also need to set it as main's pet_skip_later.
 */
struct pet_scop *pet_skip_info_add_type(struct pet_skip_info *skip,
	struct pet_scop *main, enum pet_skip type)
{
	if (!skip->skip[type])
		return main;

	if (skip->equal)
		main = pet_scop_set_skip(main, pet_skip_later,
				    isl_multi_pw_aff_copy(skip->index[type]));

	main = pet_scop_set_skip(main, type, skip->index[type]);
	skip->index[type] = NULL;

	return main;
}

/* Add the computed skip conditions to "main" and
 * add the scops for computing the conditions.
 */
struct pet_scop *pet_skip_info_add(struct pet_skip_info *skip,
	struct pet_scop *scop)
{
	scop = pet_skip_info_add_scops(skip, scop);
	scop = pet_skip_info_add_type(skip, scop, pet_skip_now);
	scop = pet_skip_info_add_type(skip, scop, pet_skip_later);

	return scop;
}

/* Do we need to construct a skip condition of the given type
 * on a sequence of statements?
 *
 * There is no need to construct a new skip condition if only
 * only of the two statements has a skip condition or if both
 * of their skip conditions are affine.
 *
 * In principle we also don't need a new continuation variable if
 * the continuation of scop2 is affine, but then we would need
 * to allow more complicated forms of continuations.
 */
static int seq_need_skip(struct pet_scop *scop1, struct pet_scop *scop2,
	enum pet_skip type)
{
	if (!scop1 || !pet_scop_has_skip(scop1, type))
		return 0;
	if (!scop2 || !pet_scop_has_skip(scop2, type))
		return 0;
	if (pet_scop_has_affine_skip(scop1, type) &&
	    pet_scop_has_affine_skip(scop2, type))
		return 0;
	return 1;
}

/* Construct a scop for computing the skip condition of the given type and
 * with index expression "skip_index" for a sequence of two scops "scop1"
 * and "scop2".  Do so within the context "pc".
 *
 * The computed scop contains a single statement that essentially does
 *
 *	skip_index = skip_cond_1 ? 1 : skip_cond_2
 *
 * or, in other words, skip_cond1 || skip_cond2.
 * In this expression, skip_cond_2 is filtered to reflect that it is
 * only evaluated when skip_cond_1 is false.
 *
 * The skip condition on scop1 is not removed because it still needs
 * to be applied to scop2 when these two scops are combined.
 */
static struct pet_scop *extract_skip_seq(
	__isl_take isl_multi_pw_aff *skip_index,
	struct pet_scop *scop1, struct pet_scop *scop2, enum pet_skip type,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	pet_expr *expr1, *expr2, *expr, *expr_skip;
	pet_tree *tree;
	struct pet_stmt *stmt;
	struct pet_scop *scop;
	isl_ctx *ctx;
	isl_set *domain;

	if (!scop1 || !scop2)
		goto error;

	ctx = isl_multi_pw_aff_get_ctx(skip_index);

	expr1 = pet_scop_get_skip_expr(scop1, type);
	expr2 = pet_scop_get_skip_expr(scop2, type);
	pet_scop_reset_skip(scop2, type);

	expr2 = pet_expr_filter(expr2, pet_expr_access_get_index(expr1), 0);

	expr = universally_true(pet_context_get_space(pc));
	expr = pet_expr_new_ternary(expr1, expr, expr2);
	expr_skip = pet_expr_from_index(isl_multi_pw_aff_copy(skip_index));
	expr_skip = pet_expr_access_set_write(expr_skip, 1);
	expr_skip = pet_expr_access_set_read(expr_skip, 0);
	expr = pet_expr_new_binary(1, pet_op_assign, expr_skip, expr);
	domain = pet_context_get_domain(pc);
	tree = pet_tree_new_expr(expr);
	stmt = pet_stmt_from_pet_tree(isl_set_copy(domain),
					state->n_stmt++, tree);

	scop = pet_scop_from_pet_stmt(pet_context_get_space(pc), stmt);
	scop = pet_scop_add_boolean_array(scop, domain, skip_index,
					state->int_size);

	return scop;
error:
	isl_multi_pw_aff_free(skip_index);
	return NULL;
}

/* Initialize a pet_skip_info_seq structure based on
 * on the two statements that are going to be combined.
 */
void pet_skip_info_seq_init(struct pet_skip_info *skip, isl_ctx *ctx,
	struct pet_scop *scop1, struct pet_scop *scop2)
{
	skip->ctx = ctx;
	skip->type = pet_skip_seq;
	skip->u.s.scop1 = scop1;
	skip->u.s.scop2 = scop2;

	skip->skip[pet_skip_now] = seq_need_skip(scop1, scop2, pet_skip_now);
	skip->equal = skip->skip[pet_skip_now] &&
		skip_equals_skip_later(scop1) && skip_equals_skip_later(scop2);
	skip->skip[pet_skip_later] = skip->skip[pet_skip_now] && !skip->equal &&
			seq_need_skip(scop1, scop2, pet_skip_later);
}

/* If we need to construct a skip condition of the given type,
 * then do so now, within the context "pc".
 */
static void pet_skip_info_seq_extract_type(struct pet_skip_info *skip,
	enum pet_skip type, __isl_keep pet_context *pc, struct pet_state *state)
{
	isl_space *space;

	if (!skip->skip[type])
		return;

	space = pet_context_get_space(pc);
	skip->index[type] = pet_create_test_index(space, state->n_test++);
	skip->scop[type] = extract_skip_seq(
				isl_multi_pw_aff_copy(skip->index[type]),
				skip->u.s.scop1, skip->u.s.scop2, type,
				pc, state);
}

/* Construct the required skip conditions within the context "pc".
 */
void pet_skip_info_seq_extract(struct pet_skip_info *skip,
	__isl_keep pet_context *pc, struct pet_state *state)
{
	pet_skip_info_seq_extract_type(skip, pet_skip_now, pc, state);
	pet_skip_info_seq_extract_type(skip, pet_skip_later, pc, state);
	if (skip->equal)
		drop_skip_later(skip->u.s.scop1, skip->u.s.scop2);
}
