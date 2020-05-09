/*
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
 * THIS SOFTWARE IS PROVIDED BY ECOLE NORMALE SUPERIEURE ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ECOLE NORMALE SUPERIEURE OR
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
 * Ecole Normale Superieure.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <isl/arg.h>
#include <isl/aff.h>
#include <isl/options.h>
#include <isl/set.h>
#include <isl/union_set.h>
#include <isl/union_map.h>
#include <isl/id_to_pw_aff.h>
#include <isl/schedule_node.h>
#include <pet.h>

struct options {
	struct isl_options	*isl;
	struct pet_options	*pet;
	char *schedule;
	char *code;
	unsigned		 tree;
};

ISL_ARGS_START(struct options, options_args)
ISL_ARG_CHILD(struct options, isl, "isl", &isl_options_args, "isl options")
ISL_ARG_CHILD(struct options, pet, NULL, &pet_options_args, "pet options")
ISL_ARG_ARG(struct options, schedule, "schedule", NULL)
ISL_ARG_ARG(struct options, code, "code", NULL)
ISL_ARG_BOOL(struct options, tree, 0, "tree", 0,
	"input schedule is specified as schedule tree")
ISL_ARGS_END

ISL_ARG_DEF(options, struct options, options_args)

/* Extract an affine expression from "expr" in the form of an isl_map.
 *
 * The domain of the created expression is that of "pc".
 */
static __isl_give isl_map *expr_extract_map(__isl_keep pet_expr *expr,
	__isl_keep pet_context *pc)
{
	isl_pw_aff *pa;

	pa = pet_expr_extract_affine(expr, pc);
	return isl_map_from_pw_aff(pa);
}

/* Extract a call from "stmt".
 *
 * The returned map is of the form
 *
 *	{ domain -> function[arguments] }
 */
static __isl_give isl_map *stmt_extract_call(struct pet_stmt *stmt)
{
	int i, n;
	isl_set *domain;
	isl_map *call;
	const char *name;
	pet_context *pc;
	pet_expr *expr;

	expr = pet_tree_expr_get_expr(stmt->body);
	if (!expr)
		return NULL;
	if (pet_expr_get_type(expr) != pet_expr_call)
		isl_die(pet_expr_get_ctx(expr),
			isl_error_invalid, "expecting call statement",
			goto error);

	domain = isl_set_copy(stmt->domain);
	call = isl_map_from_domain(domain);

	pc = pet_context_alloc(isl_set_copy(stmt->domain));
	n = pet_expr_get_n_arg(expr);
	for (i = 0; i < n; ++i) {
		isl_map *map_i;
		pet_expr *arg;

		arg = pet_expr_get_arg(expr, i);
		map_i = expr_extract_map(arg, pc);
		pet_expr_free(arg);
		call = isl_map_flat_range_product(call, map_i);
	}
	pet_context_free(pc);

	name = pet_expr_call_get_name(expr);
	call = isl_map_set_tuple_name(call, isl_dim_out, name);

	pet_expr_free(expr);
	return call;
error:
	pet_expr_free(expr);
	return NULL;
}

/* Extract a mapping from the iterations domains of "scop" to
 * the calls in the corresponding statements.
 *
 * We skip assignment and kill statements.
 * Other than assignments and kill statements, all statements are assumed
 * to be function calls.
 */
static __isl_give isl_union_map *scop_collect_calls(struct pet_scop *scop)
{
	int i;
	isl_ctx *ctx;
	isl_map *call_i;
	isl_union_map *call;

	if (!scop)
		return NULL;

	call = isl_union_map_empty(isl_set_get_space(scop->context));
	ctx = isl_set_get_ctx(scop->context);

	for (i = 0; i < scop->n_stmt; ++i) {
		struct pet_stmt *stmt;

		stmt = scop->stmts[i];
		if (pet_stmt_is_assign(stmt))
			continue;
		if (pet_stmt_is_kill(stmt))
			continue;
		call_i = stmt_extract_call(scop->stmts[i]);
		call = isl_union_map_add_map(call, call_i);
	}

	return call;
}

/* Extract a schedule on the original domains from "scop".
 * The original domain elements appear as calls in "scop".
 *
 * We first extract a schedule on the code iteration domains
 * and a mapping from the code iteration domains to the calls
 * (i.e., the original domain) and then combine the two.
 */
static __isl_give isl_union_map *extract_code_schedule(struct pet_scop *scop)
{
	isl_schedule *schedule;
	isl_union_map *schedule_map;
	isl_union_map *calls;

	schedule = pet_scop_get_schedule(scop);
	schedule_map = isl_schedule_get_map(schedule);
	isl_schedule_free(schedule);

	calls = scop_collect_calls(scop);

	schedule_map = isl_union_map_apply_domain(schedule_map, calls);

	return schedule_map;
}

/* Check that schedule and code_schedule have the same domain,
 * i.e., that they execute the same statement instances.
 */
static int check_domain(__isl_keep isl_union_map *schedule,
	__isl_keep isl_union_map *code_schedule)
{
	isl_union_set *dom1, *dom2;
	int equal;
	int r = 0;

	dom1 = isl_union_map_domain(isl_union_map_copy(schedule));
	dom2 = isl_union_map_domain(isl_union_map_copy(code_schedule));
	equal = isl_union_set_is_equal(dom1, dom2);

	if (equal < 0)
		r =  -1;
	else if (!equal) {
		isl_union_set_dump(dom1);
		isl_union_set_dump(dom2);
		isl_die(isl_union_map_get_ctx(schedule), isl_error_unknown,
			"domains not identical", r = -1);
	}

	isl_union_set_free(dom1);
	isl_union_set_free(dom2);

	return r;
}

/* Check that the relative order specified by the input schedule is respected
 * by the schedule extracted from the code, in case the original schedule
 * is single valued.
 *
 * In particular, check that there is no pair of statement instances
 * such that the first should be scheduled _before_ the second,
 * but is actually scheduled _after_ the second in the code.
 */
static int check_order_sv(__isl_keep isl_union_map *schedule,
	__isl_keep isl_union_map *code_schedule)
{
	isl_union_map *t1;
	isl_union_map *t2;
	int empty;

	t1 = isl_union_map_lex_lt_union_map(isl_union_map_copy(schedule),
					    isl_union_map_copy(schedule));
	t2 = isl_union_map_lex_gt_union_map(isl_union_map_copy(code_schedule),
					    isl_union_map_copy(code_schedule));
	t1 = isl_union_map_intersect(t1, t2);
	empty = isl_union_map_is_empty(t1);
	isl_union_map_free(t1);

	if (empty < 0)
		return -1;
	if (!empty)
		isl_die(isl_union_map_get_ctx(schedule), isl_error_unknown,
			"order not respected", return -1);

	return 0;
}

/* Check that the relative order specified by the input schedule is respected
 * by the schedule extracted from the code, in case the original schedule
 * is not single valued.
 *
 * In particular, check that the order imposed by the schedules on pairs
 * of statement instances is the same.
 */
static int check_order_not_sv(__isl_keep isl_union_map *schedule,
	__isl_keep isl_union_map *code_schedule)
{
	isl_union_map *t1;
	isl_union_map *t2;
	int equal;

	t1 = isl_union_map_lex_lt_union_map(isl_union_map_copy(schedule),
					    isl_union_map_copy(schedule));
	t2 = isl_union_map_lex_lt_union_map(isl_union_map_copy(code_schedule),
					    isl_union_map_copy(code_schedule));
	equal = isl_union_map_is_equal(t1, t2);
	isl_union_map_free(t1);
	isl_union_map_free(t2);

	if (equal < 0)
		return -1;
	if (!equal)
		isl_die(isl_union_map_get_ctx(schedule), isl_error_unknown,
			"order not respected", return -1);

	return 0;
}

/* Check that the relative order specified by the input schedule is respected
 * by the schedule extracted from the code.
 *
 * "sv" indicated whether the original schedule is single valued.
 * If so, we use a cheaper test.  Otherwise, we fall back on a more
 * expensive test.
 */
static int check_order(__isl_keep isl_union_map *schedule,
	__isl_keep isl_union_map *code_schedule, int sv)
{
	if (sv)
		return check_order_sv(schedule, code_schedule);
	else
		return check_order_not_sv(schedule, code_schedule);
}

/* If the original schedule was single valued ("sv" is set),
 * then the schedule extracted from the code should be single valued as well.
 */
static int check_single_valued(__isl_keep isl_union_map *code_schedule, int sv)
{
	if (!sv)
		return 0;

	sv = isl_union_map_is_single_valued(code_schedule);
	if (sv < 0)
		return -1;

	if (!sv)
		isl_die(isl_union_map_get_ctx(code_schedule), isl_error_unknown,
			"schedule not single valued", return -1);

	return 0;
}

/* Read a schedule and a context from the first argument and
 * C code from the second argument and check that the C code
 * corresponds to the schedule on the context.
 *
 * In particular, check that
 * - the domains are identical, i.e., the calls in the C code
 *   correspond to the domain elements of the schedule
 * - no function is called twice with the same arguments, provided
 *   the schedule is single-valued
 * - the calls are performed in an order that is compatible
 *   with the schedule
 *
 * If the schedule is not single-valued then we would have to check
 * that each function with a given set of arguments is called
 * the same number of times as there are images in the schedule,
 * but this is considerably more difficult.
 */
int main(int argc, char **argv)
{
	isl_ctx *ctx;
	isl_set *context;
	isl_union_map *input_schedule, *code_schedule;
	struct pet_scop *scop;
	struct options *options;
	FILE *file;
	int r;
	int sv;

	options = options_new_with_defaults();
	assert(options);
	ctx = isl_ctx_alloc_with_options(&options_args, options);
	pet_options_set_signed_overflow(ctx, PET_OVERFLOW_IGNORE);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	file = fopen(options->schedule, "r");
	assert(file);
	if (options->tree) {
		isl_schedule *schedule;
		isl_schedule_node *node;
		enum isl_schedule_node_type type;

		schedule = isl_schedule_read_from_file(ctx, file);
		node = isl_schedule_get_root(schedule);
		isl_options_set_schedule_separate_components(ctx, 0);
		input_schedule =
			isl_schedule_node_get_subtree_schedule_union_map(node);
		node = isl_schedule_node_child(node, 0);
		type = isl_schedule_node_get_type(node);
		if (type == isl_schedule_node_context) {
			context = isl_schedule_node_context_get_context(node);
		} else {
			isl_space *space;
			space = isl_union_map_get_space(input_schedule);
			context = isl_set_universe(space);
		}
		isl_schedule_node_free(node);
		isl_schedule_free(schedule);
	} else {
		input_schedule = isl_union_map_read_from_file(ctx, file);
		context = isl_set_read_from_file(ctx, file);
	}
	fclose(file);

	scop = pet_scop_extract_from_C_source(ctx, options->code, NULL);

	input_schedule = isl_union_map_intersect_params(input_schedule,
						isl_set_copy(context));
	code_schedule = extract_code_schedule(scop);
	code_schedule = isl_union_map_intersect_params(code_schedule, context);

	sv = isl_union_map_is_single_valued(input_schedule);
	r = sv < 0 ||
	    check_domain(input_schedule, code_schedule) ||
	    check_single_valued(code_schedule, sv) ||
	    check_order(input_schedule, code_schedule, sv);

	pet_scop_free(scop);
	isl_union_map_free(input_schedule);
	isl_union_map_free(code_schedule);
	isl_ctx_free(ctx);

	return r;
}
