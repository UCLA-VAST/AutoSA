/*
 * Copyright 2011      INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include <assert.h>
#include <stdio.h>
#include <isl/ctx.h>
#include <isl/flow.h>
#include <isl/options.h>
#include <isl/schedule.h>
#include <isl/ast_build.h>
#include <pet.h>
#include "ppcg.h"
#include "ppcg_options.h"
#include "cuda.h"
#include "cpu.h"

struct options {
	struct isl_options *isl;
	struct pet_options *pet;
	struct ppcg_options *ppcg;
	char *input;
	char *output;
};

const char *ppcg_version(void);
static void print_version(void)
{
	printf("%s", ppcg_version());
}

ISL_ARGS_START(struct options, options_args)
ISL_ARG_CHILD(struct options, isl, "isl", &isl_options_args, "isl options")
ISL_ARG_CHILD(struct options, pet, "pet", &pet_options_args, "pet options")
ISL_ARG_CHILD(struct options, ppcg, NULL, &ppcg_options_args, "ppcg options")
ISL_ARG_STR(struct options, output, 'o', NULL,
	"filename", NULL, "output filename (c target)")
ISL_ARG_ARG(struct options, input, "input", NULL)
ISL_ARG_VERSION(print_version)
ISL_ARGS_END

ISL_ARG_DEF(options, struct options, options_args)

/* Is "stmt" a kill statement?
 */
static int is_kill(struct pet_stmt *stmt)
{
	if (stmt->body->type != pet_expr_unary)
		return 0;
	return stmt->body->op == pet_op_kill;
}

/* Is "stmt" not a kill statement?
 */
static int is_not_kill(struct pet_stmt *stmt)
{
	return !is_kill(stmt);
}

/* Collect the iteration domains of the statements in "scop" that
 * satisfy "pred".
 */
static __isl_give isl_union_set *collect_domains(struct pet_scop *scop,
	int (*pred)(struct pet_stmt *stmt))
{
	int i;
	isl_set *domain_i;
	isl_union_set *domain;

	if (!scop)
		return NULL;

	domain = isl_union_set_empty(isl_set_get_space(scop->context));

	for (i = 0; i < scop->n_stmt; ++i) {
		struct pet_stmt *stmt = scop->stmts[i];

		if (!pred(stmt))
			continue;
		domain_i = isl_set_copy(scop->stmts[i]->domain);
		domain = isl_union_set_add_set(domain, domain_i);
	}

	return domain;
}

/* Collect the iteration domains of the statements in "scop",
 * skipping kill statements.
 */
static __isl_give isl_union_set *collect_non_kill_domains(struct pet_scop *scop)
{
	return collect_domains(scop, &is_not_kill);
}

/* Does "expr" contain any call expressions?
 */
static int expr_has_call(struct pet_expr *expr)
{
	int i;

	if (expr->type == pet_expr_call)
		return 1;

	for (i = 0; i < expr->n_arg; ++i)
		if (expr_has_call(expr->args[i]))
			return 1;

	return 0;
}

/* Does "stmt" contain any call expressions?
 */
static int has_call(struct pet_stmt *stmt)
{
	return expr_has_call(stmt->body);
}

/* Collect the iteration domains of the statements in "scop"
 * that contain a call expression.
 */
static __isl_give isl_union_set *collect_call_domains(struct pet_scop *scop)
{
	return collect_domains(scop, &has_call);
}

/* Collect all kill accesses in "scop".
 */
static __isl_give isl_union_map *collect_kills(struct pet_scop *scop)
{
	int i;
	isl_union_map *kills;

	if (!scop)
		return NULL;

	kills = isl_union_map_empty(isl_set_get_space(scop->context));

	for (i = 0; i < scop->n_stmt; ++i) {
		struct pet_stmt *stmt = scop->stmts[i];
		isl_map *kill_i;

		if (!is_kill(stmt))
			continue;
		kill_i = isl_map_copy(stmt->body->args[0]->acc.access);
		kill_i = isl_map_intersect_domain(kill_i,
						    isl_set_copy(stmt->domain));
		kills = isl_union_map_add_map(kills, kill_i);
	}

	return kills;
}

/* Compute the dependences of the program represented by "scop".
 * Store the computed flow dependences
 * in scop->dep_flow and the reads with no corresponding writes in
 * scop->live_in.
 * Store the false (anti and output) dependences in scop->dep_false.
 */
static void compute_dependences(struct ppcg_scop *scop)
{
	isl_union_map *empty;
	isl_union_map *dep1, *dep2;

	if (!scop)
		return;

	empty = isl_union_map_empty(isl_union_set_get_space(scop->domain));
	isl_union_map_compute_flow(isl_union_map_copy(scop->reads),
				isl_union_map_copy(scop->writes), empty,
				isl_union_map_copy(scop->schedule),
				&scop->dep_flow, NULL, &scop->live_in, NULL);

	isl_union_map_compute_flow(isl_union_map_copy(scop->writes),
				isl_union_map_copy(scop->writes),
				isl_union_map_copy(scop->reads),
				isl_union_map_copy(scop->schedule),
				&dep1, &dep2, NULL, NULL);

	scop->dep_false = isl_union_map_union(dep1, dep2);
	scop->dep_false = isl_union_map_coalesce(scop->dep_false);
}

/* Eliminate dead code from ps->domain.
 *
 * In particular, intersect ps->domain with the (parts of) iteration
 * domains that are needed to produce the output or for statement
 * iterations that call functions.
 *
 * We start with the iteration domains that call functions
 * and the set of iterations that last write to an array
 * (except those that are later killed).
 *
 * Then we add those statement iterations that produce
 * something needed by the "live" statements iterations.
 * We keep doing this until no more statement iterations can be added.
 * To ensure that the procedure terminates, we compute the affine
 * hull of the live iterations (bounded to the original iteration
 * domains) each time we have added extra iterations.
 */
static void eliminate_dead_code(struct ppcg_scop *ps)
{
	isl_union_map *exposed;
	isl_union_set *live;
	isl_union_map *dep;

	exposed = isl_union_map_union(isl_union_map_copy(ps->writes),
					    isl_union_map_copy(ps->kills));
	exposed = isl_union_map_reverse(exposed);
	exposed = isl_union_map_apply_range(exposed,
					    isl_union_map_copy(ps->schedule));
	exposed = isl_union_map_lexmax(exposed);
	exposed = isl_union_map_apply_range(exposed,
		    isl_union_map_reverse(isl_union_map_copy(ps->schedule)));

	live = isl_union_map_range(exposed);
	live = isl_union_set_union(live, isl_union_set_copy(ps->call));

	dep = isl_union_map_copy(ps->dep_flow);
	dep = isl_union_map_reverse(dep);

	for (;;) {
		isl_union_set *extra;

		extra = isl_union_set_apply(isl_union_set_copy(live),
					    isl_union_map_copy(dep));
		if (isl_union_set_is_subset(extra, live)) {
			isl_union_set_free(extra);
			break;
		}

		live = isl_union_set_union(live, extra);
		live = isl_union_set_affine_hull(live);
		live = isl_union_set_intersect(live,
					    isl_union_set_copy(ps->domain));
	}

	isl_union_map_free(dep);

	ps->domain = isl_union_set_intersect(ps->domain, live);
}

/* Intersect "set" with the set described by "str", taking the NULL
 * string to represent the universal set.
 */
static __isl_give isl_set *set_intersect_str(__isl_take isl_set *set,
	const char *str)
{
	isl_ctx *ctx;
	isl_set *set2;

	if (!str)
		return set;

	ctx = isl_set_get_ctx(set);
	set2 = isl_set_read_from_str(ctx, str);
	set = isl_set_intersect(set, set2);

	return set;
}

/* Extract a ppcg_scop from a pet_scop.
 *
 * The constructed ppcg_scop refers to elements from the pet_scop
 * so the pet_scop should not be freed before the ppcg_scop.
 */
static struct ppcg_scop *ppcg_scop_from_pet_scop(struct pet_scop *scop,
	struct ppcg_options *options)
{
	isl_ctx *ctx;
	struct ppcg_scop *ps;

	if (!scop)
		return NULL;

	ctx = isl_set_get_ctx(scop->context);

	ps = isl_calloc_type(ctx, struct ppcg_scop);
	if (!ps)
		return NULL;

	ps->context = isl_set_copy(scop->context);
	ps->context = set_intersect_str(ps->context, options->ctx);
	ps->domain = collect_non_kill_domains(scop);
	ps->call = collect_call_domains(scop);
	ps->reads = pet_scop_collect_reads(scop);
	ps->writes = pet_scop_collect_writes(scop);
	ps->kills = collect_kills(scop);
	ps->schedule = pet_scop_collect_schedule(scop);
	ps->n_array = scop->n_array;
	ps->arrays = scop->arrays;
	ps->n_stmt = scop->n_stmt;
	ps->stmts = scop->stmts;

	compute_dependences(ps);
	eliminate_dead_code(ps);

	return ps;
}

static void ppcg_scop_free(struct ppcg_scop *ps)
{
	if (!ps)
		return;

	isl_set_free(ps->context);
	isl_union_set_free(ps->domain);
	isl_union_set_free(ps->call);
	isl_union_map_free(ps->reads);
	isl_union_map_free(ps->live_in);
	isl_union_map_free(ps->writes);
	isl_union_map_free(ps->kills);
	isl_union_map_free(ps->dep_flow);
	isl_union_map_free(ps->dep_false);
	isl_union_map_free(ps->schedule);

	free(ps);
}

int main(int argc, char **argv)
{
	int r;
	isl_ctx *ctx;
	struct options *options;
	struct pet_scop *scop;
	struct ppcg_scop *ps;

	options = options_new_with_defaults();
	assert(options);

	ctx = isl_ctx_alloc_with_options(&options_args, options);
	isl_options_set_schedule_outer_zero_distance(ctx, 1);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	if (options->ppcg->openmp)
		assert(isl_options_get_ast_build_atomic_upper_bound(ctx)
			&& "OpenMP requires atomic bounds");

	scop = pet_scop_extract_from_C_source(ctx, options->input, NULL);
	scop = pet_scop_align_params(scop);
	ps = ppcg_scop_from_pet_scop(scop, options->ppcg);

	if (options->ppcg->target == PPCG_TARGET_CUDA)
		r = generate_cuda(ctx, ps, options->ppcg, options->input);
	else
		r = generate_cpu(ctx, ps, options->ppcg, options->input,
				options->output);

	ppcg_scop_free(ps);
	pet_scop_free(scop);

	isl_ctx_free(ctx);

	return r;
}
