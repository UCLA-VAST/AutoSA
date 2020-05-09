/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2014      Ecole Normale Superieure. All rights reserved.
 * Copyright 2017      Sven Verdoolaege. All rights reserved.
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

#include <isl/ctx.h>
#include <isl/id.h>
#include <isl/val.h>
#include <isl/space.h>
#include <isl/aff.h>

#include "expr.h"
#include "loc.h"
#include "tree.h"

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(*array))

static const char *type_str[] = {
	[pet_tree_block] = "block",
	[pet_tree_break] = "break",
	[pet_tree_continue] = "continue",
	[pet_tree_decl] = "declaration",
	[pet_tree_decl_init] = "declaration-init",
	[pet_tree_expr] = "expression",
	[pet_tree_for] = "for",
	[pet_tree_infinite_loop] = "infinite-loop",
	[pet_tree_if] = "if",
	[pet_tree_if_else] = "if-else",
	[pet_tree_while] = "while",
	[pet_tree_return] = "return",
};

/* Return a textual representation of the type "type".
 */
const char *pet_tree_type_str(enum pet_tree_type type)
{
	if (type < 0)
		return "error";
	return type_str[type];
}

/* Extract a type from its textual representation "str".
 */
enum pet_tree_type pet_tree_str_type(const char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(type_str); ++i)
		if (!strcmp(type_str[i], str))
			return i;

	return pet_tree_error;
}

/* Return a new pet_tree of the given type.
 *
 * The location is initializaed to pet_loc_dummy.
 */
__isl_give pet_tree *pet_tree_alloc(isl_ctx *ctx, enum pet_tree_type type)
{
	pet_tree *tree;

	tree = isl_calloc_type(ctx, struct pet_tree);
	if (!tree)
		return NULL;

	tree->ctx = ctx;
	isl_ctx_ref(ctx);
	tree->ref = 1;
	tree->type = type;
	tree->loc = &pet_loc_dummy;

	return tree;
}

/* Return a new pet_tree representing the declaration (without initialization)
 * of the variable "var".
 */
__isl_give pet_tree *pet_tree_new_decl(__isl_take pet_expr *var)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!var)
		return NULL;
	ctx = pet_expr_get_ctx(var);
	tree = pet_tree_alloc(ctx, pet_tree_decl);
	if (!tree)
		goto error;

	tree->u.d.var = var;

	return tree;
error:
	pet_expr_free(var);
	return NULL;
}

/* Return a new pet_tree representing the declaration of the variable "var"
 * with initial value "init".
 */
__isl_give pet_tree *pet_tree_new_decl_init(__isl_take pet_expr *var,
	__isl_take pet_expr *init)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!var || !init)
		goto error;
	ctx = pet_expr_get_ctx(var);
	tree = pet_tree_alloc(ctx, pet_tree_decl_init);
	if (!tree)
		goto error;

	tree->u.d.var = var;
	tree->u.d.init = init;

	return tree;
error:
	pet_expr_free(var);
	pet_expr_free(init);
	return NULL;
}

/* Return a new pet_tree representing the expression "expr".
 */
__isl_give pet_tree *pet_tree_new_expr(__isl_take pet_expr *expr)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!expr)
		return NULL;
	ctx = pet_expr_get_ctx(expr);
	tree = pet_tree_alloc(ctx, pet_tree_expr);
	if (!tree)
		goto error;

	tree->u.e.expr = expr;

	return tree;
error:
	pet_expr_free(expr);
	return NULL;
}

/* Return a new pet_tree representing the return of expression "expr".
 */
__isl_give pet_tree *pet_tree_new_return(__isl_take pet_expr *expr)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!expr)
		return NULL;
	ctx = pet_expr_get_ctx(expr);
	tree = pet_tree_alloc(ctx, pet_tree_return);
	if (!tree)
		goto error;

	tree->u.e.expr = expr;

	return tree;
error:
	pet_expr_free(expr);
	return NULL;
}

/* Return a new pet_tree representing an initially empty sequence
 * of trees with room for "n" trees.
 * "block" indicates whether the sequence has its own scope.
 */
__isl_give pet_tree *pet_tree_new_block(isl_ctx *ctx, int block, int n)
{
	pet_tree *tree;

	tree = pet_tree_alloc(ctx, pet_tree_block);
	if (!tree)
		return NULL;
	tree->u.b.block = block;
	tree->u.b.n = 0;
	tree->u.b.max = n;
	tree->u.b.child = isl_calloc_array(ctx, pet_tree *, n);
	if (n && !tree->u.b.child)
		return pet_tree_free(tree);

	return tree;
}

/* Return a new pet_tree representing a break statement.
 */
__isl_give pet_tree *pet_tree_new_break(isl_ctx *ctx)
{
	return pet_tree_alloc(ctx, pet_tree_break);
}

/* Return a new pet_tree representing a continue statement.
 */
__isl_give pet_tree *pet_tree_new_continue(isl_ctx *ctx)
{
	return pet_tree_alloc(ctx, pet_tree_continue);
}

/* Return a new pet_tree representing a for loop
 * with induction variable "iv", initial value for the induction
 * variable "init", loop condition "cond", induction variable increment "inc"
 * and loop body "body".  "declared" indicates whether the induction variable
 * is declared by the loop.  "independent" is set if the for loop is marked
 * independent.
 *
 * The location of the loop is initialized to that of the body.
 */
__isl_give pet_tree *pet_tree_new_for(int independent, int declared,
	__isl_take pet_expr *iv, __isl_take pet_expr *init,
	__isl_take pet_expr *cond, __isl_take pet_expr *inc,
	__isl_take pet_tree *body)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!iv || !init || !cond || !inc || !body)
		goto error;
	ctx = pet_tree_get_ctx(body);
	tree = pet_tree_alloc(ctx, pet_tree_for);
	if (!tree)
		goto error;

	tree->u.l.independent = independent;
	tree->u.l.declared = declared;
	tree->u.l.iv = iv;
	tree->u.l.init = init;
	tree->u.l.cond = cond;
	tree->u.l.inc = inc;
	tree->u.l.body = body;
	tree->loc = pet_tree_get_loc(body);
	if (!tree->loc)
		return pet_tree_free(tree);

	return tree;
error:
	pet_expr_free(iv);
	pet_expr_free(init);
	pet_expr_free(cond);
	pet_expr_free(inc);
	pet_tree_free(body);
	return NULL;
}

/* Return a new pet_tree representing a while loop
 * with loop condition "cond" and loop body "body".
 *
 * The location of the loop is initialized to that of the body.
 */
__isl_give pet_tree *pet_tree_new_while(__isl_take pet_expr *cond,
	__isl_take pet_tree *body)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!cond || !body)
		goto error;
	ctx = pet_tree_get_ctx(body);
	tree = pet_tree_alloc(ctx, pet_tree_while);
	if (!tree)
		goto error;

	tree->u.l.cond = cond;
	tree->u.l.body = body;
	tree->loc = pet_tree_get_loc(body);
	if (!tree->loc)
		return pet_tree_free(tree);

	return tree;
error:
	pet_expr_free(cond);
	pet_tree_free(body);
	return NULL;
}

/* Return a new pet_tree representing an infinite loop
 * with loop body "body".
 *
 * The location of the loop is initialized to that of the body.
 */
__isl_give pet_tree *pet_tree_new_infinite_loop(__isl_take pet_tree *body)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!body)
		return NULL;
	ctx = pet_tree_get_ctx(body);
	tree = pet_tree_alloc(ctx, pet_tree_infinite_loop);
	if (!tree)
		return pet_tree_free(body);

	tree->u.l.body = body;
	tree->loc = pet_tree_get_loc(body);
	if (!tree->loc)
		return pet_tree_free(tree);

	return tree;
}

/* Return a new pet_tree representing an if statement
 * with condition "cond" and then branch "then_body".
 *
 * The location of the if statement is initialized to that of the body.
 */
__isl_give pet_tree *pet_tree_new_if(__isl_take pet_expr *cond,
	__isl_take pet_tree *then_body)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!cond || !then_body)
		goto error;
	ctx = pet_tree_get_ctx(then_body);
	tree = pet_tree_alloc(ctx, pet_tree_if);
	if (!tree)
		goto error;

	tree->u.i.cond = cond;
	tree->u.i.then_body = then_body;
	tree->loc = pet_tree_get_loc(then_body);
	if (!tree->loc)
		return pet_tree_free(tree);

	return tree;
error:
	pet_expr_free(cond);
	pet_tree_free(then_body);
	return NULL;
}

/* Return a new pet_tree representing an if statement
 * with condition "cond", then branch "then_body" and else branch "else_body".
 *
 * The location of the if statement is initialized to cover
 * those of the bodies.
 */
__isl_give pet_tree *pet_tree_new_if_else(__isl_take pet_expr *cond,
	__isl_take pet_tree *then_body, __isl_take pet_tree *else_body)
{
	isl_ctx *ctx;
	pet_tree *tree;

	if (!cond || !then_body || !else_body)
		goto error;
	ctx = pet_tree_get_ctx(then_body);
	tree = pet_tree_alloc(ctx, pet_tree_if_else);
	if (!tree)
		goto error;

	tree->u.i.cond = cond;
	tree->u.i.then_body = then_body;
	tree->u.i.else_body = else_body;
	tree->loc = pet_tree_get_loc(then_body);
	tree->loc = pet_loc_update_start_end_from_loc(tree->loc,
							else_body->loc);
	if (!tree->loc)
		return pet_tree_free(tree);

	return tree;
error:
	pet_expr_free(cond);
	pet_tree_free(then_body);
	pet_tree_free(else_body);
	return NULL;
}

/* Return an independent duplicate of "tree".
 */
static __isl_give pet_tree *pet_tree_dup(__isl_keep pet_tree *tree)
{
	int i;
	pet_tree *dup;

	if (!tree)
		return NULL;

	switch (tree->type) {
	case pet_tree_error:
		return NULL;
	case pet_tree_block:
		dup = pet_tree_new_block(tree->ctx, tree->u.b.block,
					tree->u.b.n);
		for (i = 0; i < tree->u.b.n; ++i)
			dup = pet_tree_block_add_child(dup,
					pet_tree_copy(tree->u.b.child[i]));
		break;
	case pet_tree_break:
		dup = pet_tree_new_break(tree->ctx);
		break;
	case pet_tree_continue:
		dup = pet_tree_new_continue(tree->ctx);
		break;
	case pet_tree_decl:
		dup = pet_tree_new_decl(pet_expr_copy(tree->u.d.var));
		break;
	case pet_tree_decl_init:
		dup = pet_tree_new_decl_init(pet_expr_copy(tree->u.d.var),
					    pet_expr_copy(tree->u.d.init));
		break;
	case pet_tree_expr:
		dup = pet_tree_new_expr(pet_expr_copy(tree->u.e.expr));
		break;
	case pet_tree_return:
		dup = pet_tree_new_return(pet_expr_copy(tree->u.e.expr));
		break;
	case pet_tree_for:
		dup = pet_tree_new_for(tree->u.l.independent,
		    tree->u.l.declared,
		    pet_expr_copy(tree->u.l.iv), pet_expr_copy(tree->u.l.init),
		    pet_expr_copy(tree->u.l.cond), pet_expr_copy(tree->u.l.inc),
		    pet_tree_copy(tree->u.l.body));
		break;
	case pet_tree_while:
		dup = pet_tree_new_while(pet_expr_copy(tree->u.l.cond),
					pet_tree_copy(tree->u.l.body));
		break;
	case pet_tree_infinite_loop:
		dup = pet_tree_new_infinite_loop(pet_tree_copy(tree->u.l.body));
		break;
	case pet_tree_if:
		dup = pet_tree_new_if(pet_expr_copy(tree->u.i.cond),
					pet_tree_copy(tree->u.i.then_body));
		break;
	case pet_tree_if_else:
		dup = pet_tree_new_if_else(pet_expr_copy(tree->u.i.cond),
					pet_tree_copy(tree->u.i.then_body),
					pet_tree_copy(tree->u.i.else_body));
		break;
	}

	if (!dup)
		return NULL;
	pet_loc_free(dup->loc);
	dup->loc = pet_loc_copy(tree->loc);
	if (!dup->loc)
		return pet_tree_free(dup);
	if (tree->label) {
		dup->label = isl_id_copy(tree->label);
		if (!dup->label)
			return pet_tree_free(dup);
	}

	return dup;
}

/* Return a pet_tree that is equal to "tree" and that has only one reference.
 */
__isl_give pet_tree *pet_tree_cow(__isl_take pet_tree *tree)
{
	if (!tree)
		return NULL;

	if (tree->ref == 1)
		return tree;
	tree->ref--;
	return pet_tree_dup(tree);
}

/* Return an extra reference to "tree".
 */
__isl_give pet_tree *pet_tree_copy(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;

	tree->ref++;
	return tree;
}

/* Free a reference to "tree".
 */
__isl_null pet_tree *pet_tree_free(__isl_take pet_tree *tree)
{
	int i;

	if (!tree)
		return NULL;
	if (--tree->ref > 0)
		return NULL;

	pet_loc_free(tree->loc);
	isl_id_free(tree->label);

	switch (tree->type) {
	case pet_tree_error:
		break;
	case pet_tree_block:
		for (i = 0; i < tree->u.b.n; ++i)
			pet_tree_free(tree->u.b.child[i]);
		free(tree->u.b.child);
		break;
	case pet_tree_break:
	case pet_tree_continue:
		break;
	case pet_tree_decl_init:
		pet_expr_free(tree->u.d.init);
	case pet_tree_decl:
		pet_expr_free(tree->u.d.var);
		break;
	case pet_tree_expr:
	case pet_tree_return:
		pet_expr_free(tree->u.e.expr);
		break;
	case pet_tree_for:
		pet_expr_free(tree->u.l.iv);
		pet_expr_free(tree->u.l.init);
		pet_expr_free(tree->u.l.inc);
	case pet_tree_while:
		pet_expr_free(tree->u.l.cond);
	case pet_tree_infinite_loop:
		pet_tree_free(tree->u.l.body);
		break;
	case pet_tree_if_else:
		pet_tree_free(tree->u.i.else_body);
	case pet_tree_if:
		pet_expr_free(tree->u.i.cond);
		pet_tree_free(tree->u.i.then_body);
		break;
	}

	isl_ctx_deref(tree->ctx);
	free(tree);
	return NULL;
}

/* Return the isl_ctx in which "tree" was created.
 */
isl_ctx *pet_tree_get_ctx(__isl_keep pet_tree *tree)
{
	return tree ? tree->ctx : NULL;
}

/* Return the location of "tree".
 */
__isl_give pet_loc *pet_tree_get_loc(__isl_keep pet_tree *tree)
{
	return tree ? pet_loc_copy(tree->loc) : NULL;
}

/* Return the type of "tree".
 */
enum pet_tree_type pet_tree_get_type(__isl_keep pet_tree *tree)
{
	if (!tree)
		return pet_tree_error;

	return tree->type;
}

/* Replace the location of "tree" by "loc".
 */
__isl_give pet_tree *pet_tree_set_loc(__isl_take pet_tree *tree,
	__isl_take pet_loc *loc)
{
	tree = pet_tree_cow(tree);
	if (!tree || !loc)
		goto error;

	pet_loc_free(tree->loc);
	tree->loc = loc;

	return tree;
error:
	pet_loc_free(loc);
	pet_tree_free(tree);
	return NULL;
}

/* Replace the label of "tree" by "label".
 */
__isl_give pet_tree *pet_tree_set_label(__isl_take pet_tree *tree,
	__isl_take isl_id *label)
{
	tree = pet_tree_cow(tree);
	if (!tree || !label)
		goto error;

	isl_id_free(tree->label);
	tree->label = label;

	return tree;
error:
	isl_id_free(label);
	return pet_tree_free(tree);
}

/* Given an expression tree "tree", return the associated expression.
 */
__isl_give pet_expr *pet_tree_expr_get_expr(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_expr)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not an expression tree", return NULL);

	return pet_expr_copy(tree->u.e.expr);
}

/* Given a return tree "tree", return the returned expression.
 */
__isl_give pet_expr *pet_tree_return_get_expr(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_return)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a return tree", return NULL);

	return pet_expr_copy(tree->u.e.expr);
}

/* Given a block tree "tree", return the number of children in the sequence.
 */
int pet_tree_block_n_child(__isl_keep pet_tree *tree)
{
	if (!tree)
		return -1;
	if (pet_tree_get_type(tree) != pet_tree_block)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a block tree", return -1);

	return tree->u.b.n;
}

/* Add "child" to the sequence of trees represented by "block".
 *
 * Update the location of "block" to include that of "child".
 */
__isl_give pet_tree *pet_tree_block_add_child(__isl_take pet_tree *block,
	__isl_take pet_tree *child)
{
	block = pet_tree_cow(block);
	if (!block || !child)
		goto error;
	if (block->type != pet_tree_block)
		isl_die(pet_tree_get_ctx(block), isl_error_invalid,
			"not a block tree", goto error);
	if (block->u.b.n >= block->u.b.max)
		isl_die(pet_tree_get_ctx(block), isl_error_invalid,
			"out of space in block", goto error);

	block->loc = pet_loc_update_start_end_from_loc(block->loc, child->loc);
	block->u.b.child[block->u.b.n++] = child;

	if (!block->loc)
		return pet_tree_free(block);

	return block;
error:
	pet_tree_free(block);
	pet_tree_free(child);
	return NULL;
}

/* Does the block tree "tree" have its own scope?
 */
int pet_tree_block_get_block(__isl_keep pet_tree *tree)
{
	if (!tree)
		return -1;
	if (pet_tree_get_type(tree) != pet_tree_block)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a block tree", return -1);

	return tree->u.b.block;
}

/* Set the block property (whether or not the block tree has its own scope)
 * of "tree" to "is_block".
 */
__isl_give pet_tree *pet_tree_block_set_block(__isl_take pet_tree *tree,
	int is_block)
{
	if (!tree)
		return NULL;
	if (tree->type != pet_tree_block)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a block tree", return pet_tree_free(tree));
	if (tree->u.b.block == is_block)
		return tree;
	tree = pet_tree_cow(tree);
	if (!tree)
		return NULL;
	tree->u.b.block = is_block;
	return tree;
}

/* Given a block tree "tree", return the child at position "pos".
 */
__isl_give pet_tree *pet_tree_block_get_child(__isl_keep pet_tree *tree,
	int pos)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_block)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a block tree", return NULL);
	if (pos < 0 || pos >= tree->u.b.n)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"position out of bounds", return NULL);

	return pet_tree_copy(tree->u.b.child[pos]);
}

/* Does "tree" represent a declaration (with or without initialization)?
 */
int pet_tree_is_decl(__isl_keep pet_tree *tree)
{
	if (!tree)
		return -1;

	switch (pet_tree_get_type(tree)) {
	case pet_tree_decl:
	case pet_tree_decl_init:
		return 1;
	default:
		return 0;
	}
}

/* Given a declaration tree "tree", return the variable that is being
 * declared.
 */
__isl_give pet_expr *pet_tree_decl_get_var(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (!pet_tree_is_decl(tree))
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a decl tree", return NULL);

	return pet_expr_copy(tree->u.d.var);
}

/* Given a declaration tree with initialization "tree",
 * return the initial value of the declared variable.
 */
__isl_give pet_expr *pet_tree_decl_get_init(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_decl_init)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a decl_init tree", return NULL);

	return pet_expr_copy(tree->u.d.init);
}

/* Given an if tree "tree", return the if condition.
 */
__isl_give pet_expr *pet_tree_if_get_cond(__isl_keep pet_tree *tree)
{
	enum pet_tree_type type;

	if (!tree)
		return NULL;
	type = pet_tree_get_type(tree);
	if (type != pet_tree_if && type != pet_tree_if_else)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not an if tree", return NULL);

	return pet_expr_copy(tree->u.i.cond);
}

/* Given an if tree "tree", return the body of the then branch.
 */
__isl_give pet_tree *pet_tree_if_get_then(__isl_keep pet_tree *tree)
{
	enum pet_tree_type type;

	if (!tree)
		return NULL;
	type = pet_tree_get_type(tree);
	if (type != pet_tree_if && type != pet_tree_if_else)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not an if tree", return NULL);

	return pet_tree_copy(tree->u.i.then_body);
}

/* Given an if tree with an else branch "tree",
 * return the body of that else branch.
 */
__isl_give pet_tree *pet_tree_if_get_else(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_if_else)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not an if tree with an else branch", return NULL);

	return pet_tree_copy(tree->u.i.else_body);
}

/* Does "tree" represent some type of loop?
 */
int pet_tree_is_loop(__isl_keep pet_tree *tree)
{
	if (!tree)
		return -1;

	switch (pet_tree_get_type(tree)) {
	case pet_tree_for:
	case pet_tree_infinite_loop:
	case pet_tree_while:
		return 1;
	default:
		return 0;
	}
}

/* Given a for loop "tree", return the induction variable.
 */
__isl_give pet_expr *pet_tree_loop_get_var(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_for)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a for tree", return NULL);

	return pet_expr_copy(tree->u.l.iv);
}

/* Given a for loop "tree", return the initial value of the induction variable.
 */
__isl_give pet_expr *pet_tree_loop_get_init(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_for)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a for tree", return NULL);

	return pet_expr_copy(tree->u.l.init);
}

/* Given a loop "tree", return the loop condition.
 *
 * For an infinite loop, the loop condition always holds,
 * so we simply return "1".
 */
__isl_give pet_expr *pet_tree_loop_get_cond(__isl_keep pet_tree *tree)
{
	enum pet_tree_type type;

	if (!tree)
		return NULL;
	type = pet_tree_get_type(tree);
	if (type == pet_tree_infinite_loop)
		return pet_expr_new_int(isl_val_one(pet_tree_get_ctx(tree)));
	if (type != pet_tree_for && type != pet_tree_while)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a for or while tree", return NULL);

	return pet_expr_copy(tree->u.l.cond);
}

/* Given a for loop "tree", return the increment of the induction variable.
 */
__isl_give pet_expr *pet_tree_loop_get_inc(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (pet_tree_get_type(tree) != pet_tree_for)
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a for tree", return NULL);

	return pet_expr_copy(tree->u.l.inc);
}

/* Given a loop tree "tree", return the body.
 */
__isl_give pet_tree *pet_tree_loop_get_body(__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;

	if (!pet_tree_is_loop(tree))
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not a loop tree", return NULL);

	return pet_tree_copy(tree->u.l.body);
}

/* Call "fn" on each node of "tree", including "tree" itself.
 *
 * Return 0 on success and -1 on error, where "fn" returning a negative
 * value is treated as an error.
 */
int pet_tree_foreach_sub_tree(__isl_keep pet_tree *tree,
	int (*fn)(__isl_keep pet_tree *tree, void *user), void *user)
{
	int i;

	if (!tree)
		return -1;

	if (fn(tree, user) < 0)
		return -1;

	switch (tree->type) {
	case pet_tree_error:
		return -1;
	case pet_tree_block:
		for (i = 0; i < tree->u.b.n; ++i)
			if (pet_tree_foreach_sub_tree(tree->u.b.child[i],
							fn, user) < 0)
				return -1;
		break;
	case pet_tree_break:
	case pet_tree_continue:
	case pet_tree_decl:
	case pet_tree_decl_init:
	case pet_tree_expr:
	case pet_tree_return:
		break;
	case pet_tree_if:
		if (pet_tree_foreach_sub_tree(tree->u.i.then_body,
						fn, user) < 0)
			return -1;
		break;
	case pet_tree_if_else:
		if (pet_tree_foreach_sub_tree(tree->u.i.then_body,
						fn, user) < 0)
			return -1;
		if (pet_tree_foreach_sub_tree(tree->u.i.else_body,
						fn, user) < 0)
			return -1;
		break;
	case pet_tree_while:
	case pet_tree_for:
	case pet_tree_infinite_loop:
		if (pet_tree_foreach_sub_tree(tree->u.l.body, fn, user) < 0)
			return -1;
		break;
	}

	return 0;
}

/* Intermediate data structure for foreach_expr.
 *
 * "fn" is the function that needs to be called on each expression.
 * "user" is the user argument to be passed to "fn".
 */
struct pet_tree_foreach_expr_data {
	int (*fn)(__isl_keep pet_expr *expr, void *user);
	void *user;
};

/* Call data->fn on each expression in the "tree" object.
 * This function is used as a callback to pet_tree_foreach_sub_tree
 * to implement pet_tree_foreach_expr.
 *
 * Return 0 on success and -1 on error, where data->fn returning a negative
 * value is treated as an error.
 */
static int foreach_expr(__isl_keep pet_tree *tree, void *user)
{
	struct pet_tree_foreach_expr_data *data = user;

	if (!tree)
		return -1;

	switch (tree->type) {
	case pet_tree_error:
		return -1;
	case pet_tree_block:
	case pet_tree_break:
	case pet_tree_continue:
	case pet_tree_infinite_loop:
		break;
	case pet_tree_decl:
		if (data->fn(tree->u.d.var, data->user) < 0)
			return -1;
		break;
	case pet_tree_decl_init:
		if (data->fn(tree->u.d.var, data->user) < 0)
			return -1;
		if (data->fn(tree->u.d.init, data->user) < 0)
			return -1;
		break;
	case pet_tree_expr:
	case pet_tree_return:
		if (data->fn(tree->u.e.expr, data->user) < 0)
			return -1;
		break;
	case pet_tree_if:
		if (data->fn(tree->u.i.cond, data->user) < 0)
			return -1;
		break;
	case pet_tree_if_else:
		if (data->fn(tree->u.i.cond, data->user) < 0)
			return -1;
		break;
	case pet_tree_while:
		if (data->fn(tree->u.l.cond, data->user) < 0)
			return -1;
		break;
	case pet_tree_for:
		if (data->fn(tree->u.l.iv, data->user) < 0)
			return -1;
		if (data->fn(tree->u.l.init, data->user) < 0)
			return -1;
		if (data->fn(tree->u.l.cond, data->user) < 0)
			return -1;
		if (data->fn(tree->u.l.inc, data->user) < 0)
			return -1;
		break;
	}

	return 0;
}

/* Call "fn" on each top-level expression in the nodes of "tree"
 *
 * Return 0 on success and -1 on error, where "fn" returning a negative
 * value is treated as an error.
 *
 * We run over all nodes in "tree" and call "fn"
 * on each expression in those nodes.
 */
int pet_tree_foreach_expr(__isl_keep pet_tree *tree,
	int (*fn)(__isl_keep pet_expr *expr, void *user), void *user)
{
	struct pet_tree_foreach_expr_data data = { fn, user };

	return pet_tree_foreach_sub_tree(tree, &foreach_expr, &data);
}

/* Intermediate data structure for foreach_access_expr.
 *
 * "fn" is the function that needs to be called on each access subexpression.
 * "user" is the user argument to be passed to "fn".
 */
struct pet_tree_foreach_access_expr_data {
	int (*fn)(__isl_keep pet_expr *expr, void *user);
	void *user;
};

/* Call data->fn on each access subexpression of "expr".
 * This function is used as a callback to pet_tree_foreach_expr
 * to implement pet_tree_foreach_access_expr.
 *
 * Return 0 on success and -1 on error, where data->fn returning a negative
 * value is treated as an error.
 */
static int foreach_access_expr(__isl_keep pet_expr *expr, void *user)
{
	struct pet_tree_foreach_access_expr_data *data = user;

	return pet_expr_foreach_access_expr(expr, data->fn, data->user);
}

/* Call "fn" on each access subexpression in the nodes of "tree"
 *
 * Return 0 on success and -1 on error, where "fn" returning a negative
 * value is treated as an error.
 *
 * We run over all expressions in the nodes of "tree" and call "fn"
 * on each access subexpression of those expressions.
 */
int pet_tree_foreach_access_expr(__isl_keep pet_tree *tree,
	int (*fn)(__isl_keep pet_expr *expr, void *user), void *user)
{
	struct pet_tree_foreach_access_expr_data data = { fn, user };

	return pet_tree_foreach_expr(tree, &foreach_access_expr, &data);
}

/* Modify all subtrees of "tree", include "tree" itself,
 * by calling "fn" on them.
 * The subtrees are traversed in depth first preorder.
 */
__isl_give pet_tree *pet_tree_map_top_down(__isl_take pet_tree *tree,
	__isl_give pet_tree *(*fn)(__isl_take pet_tree *tree, void *user),
	void *user)
{
	int i;

	if (!tree)
		return NULL;

	tree = fn(tree, user);
	tree = pet_tree_cow(tree);
	if (!tree)
		return NULL;

	switch (tree->type) {
	case pet_tree_error:
		return pet_tree_free(tree);
	case pet_tree_block:
		for (i = 0; i < tree->u.b.n; ++i) {
			tree->u.b.child[i] =
			    pet_tree_map_top_down(tree->u.b.child[i], fn, user);
			if (!tree->u.b.child[i])
				return pet_tree_free(tree);
		}
		break;
	case pet_tree_break:
	case pet_tree_continue:
	case pet_tree_decl:
	case pet_tree_decl_init:
	case pet_tree_expr:
	case pet_tree_return:
		break;
	case pet_tree_if:
		tree->u.i.then_body =
			pet_tree_map_top_down(tree->u.i.then_body, fn, user);
		if (!tree->u.i.then_body)
			return pet_tree_free(tree);
		break;
	case pet_tree_if_else:
		tree->u.i.then_body =
			pet_tree_map_top_down(tree->u.i.then_body, fn, user);
		tree->u.i.else_body =
			pet_tree_map_top_down(tree->u.i.else_body, fn, user);
		if (!tree->u.i.then_body || !tree->u.i.else_body)
			return pet_tree_free(tree);
		break;
	case pet_tree_while:
	case pet_tree_for:
	case pet_tree_infinite_loop:
		tree->u.l.body =
			pet_tree_map_top_down(tree->u.l.body, fn, user);
		if (!tree->u.l.body)
			return pet_tree_free(tree);
		break;
	}

	return tree;
}

/* Modify all top-level expressions in the nodes of "tree"
 * by calling "fn" on them.
 */
__isl_give pet_tree *pet_tree_map_expr(__isl_take pet_tree *tree,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user)
{
	int i;

	tree = pet_tree_cow(tree);
	if (!tree)
		return NULL;

	switch (tree->type) {
	case pet_tree_error:
		return pet_tree_free(tree);
	case pet_tree_block:
		for (i = 0; i < tree->u.b.n; ++i) {
			tree->u.b.child[i] =
			    pet_tree_map_expr(tree->u.b.child[i], fn, user);
			if (!tree->u.b.child[i])
				return pet_tree_free(tree);
		}
		break;
	case pet_tree_break:
	case pet_tree_continue:
		break;
	case pet_tree_decl:
		tree->u.d.var = fn(tree->u.d.var, user);
		if (!tree->u.d.var)
			return pet_tree_free(tree);
		break;
	case pet_tree_decl_init:
		tree->u.d.var = fn(tree->u.d.var, user);
		tree->u.d.init = fn(tree->u.d.init, user);
		if (!tree->u.d.var || !tree->u.d.init)
			return pet_tree_free(tree);
		break;
	case pet_tree_expr:
	case pet_tree_return:
		tree->u.e.expr = fn(tree->u.e.expr, user);
		if (!tree->u.e.expr)
			return pet_tree_free(tree);
		break;
	case pet_tree_if:
		tree->u.i.cond = fn(tree->u.i.cond, user);
		tree->u.i.then_body =
			pet_tree_map_expr(tree->u.i.then_body, fn, user);
		if (!tree->u.i.cond || !tree->u.i.then_body)
			return pet_tree_free(tree);
		break;
	case pet_tree_if_else:
		tree->u.i.cond = fn(tree->u.i.cond, user);
		tree->u.i.then_body =
			pet_tree_map_expr(tree->u.i.then_body, fn, user);
		tree->u.i.else_body =
			pet_tree_map_expr(tree->u.i.else_body, fn, user);
		if (!tree->u.i.cond ||
		    !tree->u.i.then_body || !tree->u.i.else_body)
			return pet_tree_free(tree);
		break;
	case pet_tree_while:
		tree->u.l.cond = fn(tree->u.l.cond, user);
		tree->u.l.body = pet_tree_map_expr(tree->u.l.body, fn, user);
		if (!tree->u.l.cond || !tree->u.l.body)
			return pet_tree_free(tree);
		break;
	case pet_tree_for:
		tree->u.l.iv = fn(tree->u.l.iv, user);
		tree->u.l.init = fn(tree->u.l.init, user);
		tree->u.l.cond = fn(tree->u.l.cond, user);
		tree->u.l.inc = fn(tree->u.l.inc, user);
		tree->u.l.body = pet_tree_map_expr(tree->u.l.body, fn, user);
		if (!tree->u.l.iv || !tree->u.l.init || !tree->u.l.cond ||
		    !tree->u.l.inc || !tree->u.l.body)
			return pet_tree_free(tree);
		break;
	case pet_tree_infinite_loop:
		tree->u.l.body = pet_tree_map_expr(tree->u.l.body, fn, user);
		if (!tree->u.l.body)
			return pet_tree_free(tree);
		break;
	}

	return tree;
}

/* Intermediate data structure for map_expr.
 *
 * "map" is a function that modifies subexpressions of a given type.
 * "fn" is the function that needs to be called on each of those subexpressions.
 * "user" is the user argument to be passed to "fn".
 */
struct pet_tree_map_expr_data {
	__isl_give pet_expr *(*map)(__isl_take pet_expr *expr,
		__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr,
		void *user), void *user);
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user);
	void *user;
};

/* This function is called on each top-level expressions in the nodes
 * of a tree.  Call data->map to modify subexpressions of the top-level
 * expression by calling data->fn on them.
 *
 * This is a wrapper around data->map for use as a callback
 * to pet_tree_map_expr.
 */
static __isl_give pet_expr *map_expr(__isl_take pet_expr *expr,
	void *user)
{
	struct pet_tree_map_expr_data *data = user;

	return data->map(expr, data->fn, data->user);
}

/* Modify all access subexpressions in the nodes of "tree"
 * by calling "fn" on them.
 *
 * We run over all expressions in the nodes of "tree" and call "fn"
 * on each access subexpression of those expressions.
 */
__isl_give pet_tree *pet_tree_map_access_expr(__isl_take pet_tree *tree,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user)
{
	struct pet_tree_map_expr_data data = { &pet_expr_map_access, fn, user };

	return pet_tree_map_expr(tree, &map_expr, &data);
}

/* Modify all call subexpressions in the nodes of "tree"
 * by calling "fn" on them.
 *
 * We run over all expressions in the nodes of "tree" and call "fn"
 * on each call subexpression of those expressions.
 */
__isl_give pet_tree *pet_tree_map_call_expr(__isl_take pet_tree *tree,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user)
{
	struct pet_tree_map_expr_data data = { &pet_expr_map_call, fn, user };

	return pet_tree_map_expr(tree, &map_expr, &data);
}

/* Wrapper around pet_expr_align_params
 * for use as a pet_tree_map_expr callback.
 */
static __isl_give pet_expr *align_params(__isl_take pet_expr *expr,
	void *user)
{
	isl_space *space = user;

	return pet_expr_align_params(expr, isl_space_copy(space));
}

/* Add all parameters in "space" to all access relations and index expressions
 * in "tree".
 */
__isl_give pet_tree *pet_tree_align_params(__isl_take pet_tree *tree,
	__isl_take isl_space *space)
{
	tree = pet_tree_map_expr(tree, &align_params, space);
	isl_space_free(space);
	return tree;
}

/* Wrapper around pet_expr_add_ref_ids
 * for use as a pet_tree_map_expr callback.
 */
static __isl_give pet_expr *add_ref_ids(__isl_take pet_expr *expr, void *user)
{
	int *n_ref = user;

	return pet_expr_add_ref_ids(expr, n_ref);
}

/* Add reference identifiers to all access expressions in "tree".
 * "n_ref" points to an integer that contains the sequence number
 * of the next reference.
 */
__isl_give pet_tree *pet_tree_add_ref_ids(__isl_take pet_tree *tree,
	int *n_ref)
{
	return pet_tree_map_expr(tree, &add_ref_ids, n_ref);
}

/* Wrapper around pet_expr_anonymize
 * for use as a pet_tree_map_expr callback.
 */
static __isl_give pet_expr *anonymize(__isl_take pet_expr *expr, void *user)
{
	return pet_expr_anonymize(expr);
}

/* Reset the user pointer on all parameter and tuple ids in "tree".
 */
__isl_give pet_tree *pet_tree_anonymize(__isl_take pet_tree *tree)
{
	return pet_tree_map_expr(tree, &anonymize, NULL);
}

/* Arguments to be passed to pet_expr_gist from the gist wrapper.
 */
struct pet_tree_gist_data {
	isl_set *domain;
	isl_union_map *value_bounds;
};

/* Wrapper around pet_expr_gist for use as a pet_tree_map_expr callback.
 */
static __isl_give pet_expr *gist(__isl_take pet_expr *expr, void *user)
{
	struct pet_tree_gist_data *data = user;

	return pet_expr_gist(expr, data->domain, data->value_bounds);
}

/* Compute the gist of all access relations and index expressions inside
 * "tree" based on the constraints on the parameters specified by "context"
 * and the constraints on the values of nested accesses specified
 * by "value_bounds".
 */
__isl_give pet_tree *pet_tree_gist(__isl_take pet_tree *tree,
	__isl_keep isl_set *context, __isl_keep isl_union_map *value_bounds)
{
	struct pet_tree_gist_data data = { context, value_bounds };

	return pet_tree_map_expr(tree, &gist, &data);
}

/* Return 1 if the two pet_tree objects are equivalent.
 *
 * We ignore the locations of the trees.
 */
int pet_tree_is_equal(__isl_keep pet_tree *tree1, __isl_keep pet_tree *tree2)
{
	int i;
	int equal;

	if (!tree1 || !tree2)
		return 0;

	if (tree1 == tree2)
		return 1;

	if (tree1->type != tree2->type)
		return 0;
	if (tree1->label != tree2->label)
		return 0;

	switch (tree1->type) {
	case pet_tree_error:
		return -1;
	case pet_tree_block:
		if (tree1->u.b.block != tree2->u.b.block)
			return 0;
		if (tree1->u.b.n != tree2->u.b.n)
			return 0;
		for (i = 0; i < tree1->u.b.n; ++i) {
			equal = pet_tree_is_equal(tree1->u.b.child[i],
						tree2->u.b.child[i]);
			if (equal < 0 || !equal)
				return equal;
		}
		break;
	case pet_tree_break:
	case pet_tree_continue:
		break;
	case pet_tree_decl:
		return pet_expr_is_equal(tree1->u.d.var, tree2->u.d.var);
	case pet_tree_decl_init:
		equal = pet_expr_is_equal(tree1->u.d.var, tree2->u.d.var);
		if (equal < 0 || !equal)
			return equal;
		return pet_expr_is_equal(tree1->u.d.init, tree2->u.d.init);
	case pet_tree_expr:
	case pet_tree_return:
		return pet_expr_is_equal(tree1->u.e.expr, tree2->u.e.expr);
	case pet_tree_for:
		if (tree1->u.l.declared != tree2->u.l.declared)
			return 0;
		equal = pet_expr_is_equal(tree1->u.l.iv, tree2->u.l.iv);
		if (equal < 0 || !equal)
			return equal;
		equal = pet_expr_is_equal(tree1->u.l.init, tree2->u.l.init);
		if (equal < 0 || !equal)
			return equal;
		equal = pet_expr_is_equal(tree1->u.l.cond, tree2->u.l.cond);
		if (equal < 0 || !equal)
			return equal;
		equal = pet_expr_is_equal(tree1->u.l.inc, tree2->u.l.inc);
		if (equal < 0 || !equal)
			return equal;
		return pet_tree_is_equal(tree1->u.l.body, tree2->u.l.body);
	case pet_tree_while:
		equal = pet_expr_is_equal(tree1->u.l.cond, tree2->u.l.cond);
		if (equal < 0 || !equal)
			return equal;
		return pet_tree_is_equal(tree1->u.l.body, tree2->u.l.body);
	case pet_tree_infinite_loop:
		return pet_tree_is_equal(tree1->u.l.body, tree2->u.l.body);
	case pet_tree_if:
		equal = pet_expr_is_equal(tree1->u.i.cond, tree2->u.i.cond);
		if (equal < 0 || !equal)
			return equal;
		return pet_tree_is_equal(tree1->u.i.then_body,
					tree2->u.i.then_body);
	case pet_tree_if_else:
		equal = pet_expr_is_equal(tree1->u.i.cond, tree2->u.i.cond);
		if (equal < 0 || !equal)
			return equal;
		equal = pet_tree_is_equal(tree1->u.i.then_body,
					tree2->u.i.then_body);
		if (equal < 0 || !equal)
			return equal;
		return pet_tree_is_equal(tree1->u.i.else_body,
					tree2->u.i.else_body);
	}

	return 1;
}

/* Is "tree" an expression tree that performs the operation "type"?
 */
static int pet_tree_is_op_of_type(__isl_keep pet_tree *tree,
	enum pet_op_type type)
{
	if (!tree)
		return 0;
	if (tree->type != pet_tree_expr)
		return 0;
	if (pet_expr_get_type(tree->u.e.expr) != pet_expr_op)
		return 0;
	return pet_expr_op_get_type(tree->u.e.expr) == type;
}

/* Is "tree" an expression tree that performs a kill operation?
 */
int pet_tree_is_kill(__isl_keep pet_tree *tree)
{
	return pet_tree_is_op_of_type(tree, pet_op_kill);
}

/* Is "tree" an expression tree that performs an assignment operation?
 */
int pet_tree_is_assign(__isl_keep pet_tree *tree)
{
	return pet_tree_is_op_of_type(tree, pet_op_assign);
}

/* Is "tree" an expression tree that performs an assume operation?
 */
int pet_tree_is_assume(__isl_keep pet_tree *tree)
{
	return pet_tree_is_op_of_type(tree, pet_op_assume);
}

/* Is "tree" an expression tree that performs an assume operation
 * such that the assumed expression is affine?
 */
isl_bool pet_tree_is_affine_assume(__isl_keep pet_tree *tree)
{
	if (!pet_tree_is_assume(tree))
		return isl_bool_false;
	return pet_expr_is_affine(tree->u.e.expr->args[0]);
}

/* Given a tree that represent an assume operation expression
 * with an access as argument (possibly an affine expression),
 * return the index expression of that access.
 */
__isl_give isl_multi_pw_aff *pet_tree_assume_get_index(
	__isl_keep pet_tree *tree)
{
	if (!tree)
		return NULL;
	if (!pet_tree_is_assume(tree))
		isl_die(pet_tree_get_ctx(tree), isl_error_invalid,
			"not an assume tree", return NULL);
	return pet_expr_access_get_index(tree->u.e.expr->args[0]);
}

/* Internal data structure for pet_tree_writes.
 * "id" is the identifier that we are looking for.
 * "writes" is set if we have found the identifier being written to.
 */
struct pet_tree_writes_data {
	isl_id *id;
	int writes;
};

/* Check if expr writes to data->id.
 * If so, set data->writes and abort the search.
 */
static int check_write(__isl_keep pet_expr *expr, void *user)
{
	struct pet_tree_writes_data *data = user;

	data->writes = pet_expr_writes(expr, data->id);
	if (data->writes < 0 || data->writes)
		return -1;

	return 0;
}

/* Is there any write access in "tree" that accesses "id"?
 */
int pet_tree_writes(__isl_keep pet_tree *tree, __isl_keep isl_id *id)
{
	struct pet_tree_writes_data data;

	data.id = id;
	data.writes = 0;
	if (pet_tree_foreach_expr(tree, &check_write, &data) < 0 &&
	    !data.writes)
		return -1;

	return data.writes;
}

/* Wrapper around pet_expr_update_domain
 * for use as a pet_tree_map_expr callback.
 */
static __isl_give pet_expr *update_domain(__isl_take pet_expr *expr, void *user)
{
	isl_multi_pw_aff *update = user;

	return pet_expr_update_domain(expr, isl_multi_pw_aff_copy(update));
}

/* Modify all access relations in "tree" by precomposing them with
 * the given iteration space transformation.
 */
__isl_give pet_tree *pet_tree_update_domain(__isl_take pet_tree *tree,
	__isl_take isl_multi_pw_aff *update)
{
	tree = pet_tree_map_expr(tree, &update_domain, update);
	isl_multi_pw_aff_free(update);
	return tree;
}

/* Does "tree" contain a continue or break node (not contained in any loop
 * subtree of "tree")?
 */
int pet_tree_has_continue_or_break(__isl_keep pet_tree *tree)
{
	int i;
	int found;

	if (!tree)
		return -1;

	switch (tree->type) {
	case pet_tree_error:
		return -1;
	case pet_tree_continue:
	case pet_tree_break:
		return 1;
	case pet_tree_decl:
	case pet_tree_decl_init:
	case pet_tree_expr:
	case pet_tree_return:
	case pet_tree_for:
	case pet_tree_while:
	case pet_tree_infinite_loop:
		return 0;
	case pet_tree_block:
		for (i = 0; i < tree->u.b.n; ++i) {
			found =
			    pet_tree_has_continue_or_break(tree->u.b.child[i]);
			if (found < 0 || found)
				return found;
		}
		return 0;
	case pet_tree_if:
		return pet_tree_has_continue_or_break(tree->u.i.then_body);
	case pet_tree_if_else:
		found = pet_tree_has_continue_or_break(tree->u.i.then_body);
		if (found < 0 || found)
			return found;
		return pet_tree_has_continue_or_break(tree->u.i.else_body);
	}
}

static void print_indent(int indent)
{
	fprintf(stderr, "%*s", indent, "");
}

void pet_tree_dump_with_indent(__isl_keep pet_tree *tree, int indent)
{
	int i;

	if (!tree)
		return;

	print_indent(indent);
	fprintf(stderr, "%s\n", pet_tree_type_str(tree->type));
	print_indent(indent);
	fprintf(stderr, "line: %d\n", pet_loc_get_line(tree->loc));
	print_indent(indent);
	fprintf(stderr, "start: %d\n", pet_loc_get_start(tree->loc));
	print_indent(indent);
	fprintf(stderr, "end: %d\n", pet_loc_get_end(tree->loc));
	if (tree->label) {
		print_indent(indent);
		fprintf(stderr, "label: ");
		isl_id_dump(tree->label);
	}
	switch (tree->type) {
	case pet_tree_block:
		print_indent(indent);
		fprintf(stderr, "block: %d\n", tree->u.b.block);
		for (i = 0; i < tree->u.b.n; ++i)
			pet_tree_dump_with_indent(tree->u.b.child[i],
							indent + 2);
		break;
	case pet_tree_expr:
		pet_expr_dump_with_indent(tree->u.e.expr, indent);
		break;
	case pet_tree_return:
		print_indent(indent);
		fprintf(stderr, "return:\n");
		pet_expr_dump_with_indent(tree->u.e.expr, indent);
		break;
	case pet_tree_break:
	case pet_tree_continue:
		break;
	case pet_tree_decl:
	case pet_tree_decl_init:
		print_indent(indent);
		fprintf(stderr, "var:\n");
		pet_expr_dump_with_indent(tree->u.d.var, indent + 2);
		if (tree->type != pet_tree_decl_init)
			break;
		print_indent(indent);
		fprintf(stderr, "init:\n");
		pet_expr_dump_with_indent(tree->u.d.init, indent + 2);
		break;
	case pet_tree_if:
	case pet_tree_if_else:
		print_indent(indent);
		fprintf(stderr, "condition:\n");
		pet_expr_dump_with_indent(tree->u.i.cond, indent + 2);
		print_indent(indent);
		fprintf(stderr, "then:\n");
		pet_tree_dump_with_indent(tree->u.i.then_body, indent + 2);
		if (tree->type != pet_tree_if_else)
			break;
		print_indent(indent);
		fprintf(stderr, "else:\n");
		pet_tree_dump_with_indent(tree->u.i.else_body, indent + 2);
		break;
	case pet_tree_for:
		print_indent(indent);
		fprintf(stderr, "declared: %d\n", tree->u.l.declared);
		print_indent(indent);
		fprintf(stderr, "var:\n");
		pet_expr_dump_with_indent(tree->u.l.iv, indent + 2);
		print_indent(indent);
		fprintf(stderr, "init:\n");
		pet_expr_dump_with_indent(tree->u.l.init, indent + 2);
		print_indent(indent);
		fprintf(stderr, "inc:\n");
		pet_expr_dump_with_indent(tree->u.l.inc, indent + 2);
	case pet_tree_while:
		print_indent(indent);
		fprintf(stderr, "condition:\n");
		pet_expr_dump_with_indent(tree->u.l.cond, indent + 2);
	case pet_tree_infinite_loop:
		print_indent(indent);
		fprintf(stderr, "body:\n");
		pet_tree_dump_with_indent(tree->u.l.body, indent + 2);
		break;
	case pet_tree_error:
		print_indent(indent);
		fprintf(stderr, "ERROR\n");
		break;
	}
}

void pet_tree_dump(__isl_keep pet_tree *tree)
{
	pet_tree_dump_with_indent(tree, 0);
}
