/*
 * Copyright 2011      INRIA Saclay
 * Copyright 2013      Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 * and Ecole Normale Superieure, 45 rue d'Ulm, 75230 Paris, France
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <isl/ctx.h>
#include <isl/flow.h>
#include <isl/options.h>
#include <isl/schedule.h>
#include <isl/ast_build.h>
#include <isl/schedule.h>
#include <pet.h>
#include "ppcg.h"
#include "ppcg_options.h"
#include "cuda.h"
#include "opencl.h"
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
	"filename", NULL, "output filename (c and opencl targets)")
ISL_ARG_ARG(struct options, input, "input", NULL)
ISL_ARG_VERSION(print_version)
ISL_ARGS_END

ISL_ARG_DEF(options, struct options, options_args)

/* Return a pointer to the final path component of "filename" or
 * to "filename" itself if it does not contain any components.
 */
const char *ppcg_base_name(const char *filename)
{
	const char *base;

	base = strrchr(filename, '/');
	if (base)
		return ++base;
	else
		return filename;
}

/* Copy the base name of "input" to "name" and return its length.
 * "name" is not NULL terminated.
 *
 * In particular, remove all leading directory components and
 * the final extension, if any.
 */
int ppcg_extract_base_name(char *name, const char *input)
{
	const char *base;
	const char *ext;
	int len;

	base = ppcg_base_name(input);
	ext = strrchr(base, '.');
	len = ext ? ext - base : strlen(base);

	memcpy(name, base, len);

	return len;
}

/* Is "stmt" not a kill statement?
 */
static int is_not_kill(struct pet_stmt *stmt)
{
	return !pet_stmt_is_kill(stmt);
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

		if (stmt->n_arg > 0)
			isl_die(isl_union_set_get_ctx(domain),
				isl_error_unsupported,
				"data dependent conditions not supported",
				return isl_union_set_free(domain));

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

/* This function is used as a callback to pet_expr_foreach_call_expr
 * to detect if there is any call expression in the input expression.
 * Assign the value 1 to the integer that "user" points to and
 * abort the search since we have found what we were looking for.
 */
static int set_has_call(__isl_keep pet_expr *expr, void *user)
{
	int *has_call = user;

	*has_call = 1;

	return -1;
}

/* Does "expr" contain any call expressions?
 */
static int expr_has_call(__isl_keep pet_expr *expr)
{
	int has_call = 0;

	if (pet_expr_foreach_call_expr(expr, &set_has_call, &has_call) < 0 &&
	    !has_call)
		return -1;

	return has_call;
}

/* This function is a callback for pet_tree_foreach_expr.
 * If "expr" contains any call (sub)expressions, then set *has_call
 * and abort the search.
 */
static int check_call(__isl_keep pet_expr *expr, void *user)
{
	int *has_call = user;

	if (expr_has_call(expr))
		*has_call = 1;

	return *has_call ? -1 : 0;
}

/* Does "stmt" contain any call expressions?
 */
static int has_call(struct pet_stmt *stmt)
{
	int has_call = 0;

	if (pet_tree_foreach_expr(stmt->body, &check_call, &has_call) < 0 &&
	    !has_call)
		return -1;

	return has_call;
}

/* Collect the iteration domains of the statements in "scop"
 * that contain a call expression.
 */
static __isl_give isl_union_set *collect_call_domains(struct pet_scop *scop)
{
	return collect_domains(scop, &has_call);
}

/* Given a union of "tagged" access relations of the form
 *
 *	[S_i[...] -> R_j[]] -> A_k[...]
 *
 * project out the "tags" (R_j[]).
 * That is, return a union of relations of the form
 *
 *	S_i[...] -> A_k[...]
 */
static __isl_give isl_union_map *project_out_tags(
	__isl_take isl_union_map *umap)
{
	isl_union_map *proj;

	proj = isl_union_map_universe(isl_union_map_copy(umap));
	proj = isl_union_set_unwrap(isl_union_map_domain(proj));
	proj = isl_union_map_domain_map(proj);

	umap = isl_union_map_apply_domain(umap, proj);

	return umap;
}

/* Construct a relation from the iteration domains to tagged iteration
 * domains with as range the reference tags that appear
 * in any of the reads, writes or kills.
 * Store the result in ps->tagger.
 *
 * For example, if the statement with iteration space S[i,j]
 * contains two array references R_1[] and R_2[], then ps->tagger will contain
 *
 *	{ S[i,j] -> [S[i,j] -> R_1[]]; S[i,j] -> [S[i,j] -> R_2[]] }
 */
static void compute_tagger(struct ppcg_scop *ps)
{
	isl_union_map *tagged, *tagger;

	tagged = isl_union_map_copy(ps->tagged_reads);
	tagged = isl_union_map_union(tagged,
				isl_union_map_copy(ps->tagged_may_writes));
	tagged = isl_union_map_union(tagged,
				isl_union_map_copy(ps->tagged_must_kills));

	tagger = isl_union_map_universe(tagged);
	tagger = isl_union_set_unwrap(isl_union_map_domain(tagger));
	tagger = isl_union_map_reverse(isl_union_map_domain_map(tagger));

	ps->tagger = tagger;
}

/* Compute the live out accesses, i.e., the writes that are
 * potentially not killed by any kills or any other writes, and
 * store them in ps->live_out.
 *
 * We compute the "dependence" of any "kill" (an explicit kill
 * or a must write) on any may write.
 * The may writes with a "depending" kill are definitely killed.
 * The remaining may writes can potentially be live out.
 */
static void compute_live_out(struct ppcg_scop *ps)
{
	isl_union_map *tagger;
	isl_union_map *schedule;
	isl_union_map *empty;
	isl_union_map *kills;
	isl_union_map *exposed;
	isl_union_map *covering;

	tagger = isl_union_map_copy(ps->tagger);
	schedule = isl_union_map_copy(ps->schedule);
	schedule = isl_union_map_apply_domain(schedule,
					isl_union_map_copy(tagger));
	empty = isl_union_map_empty(isl_union_set_get_space(ps->domain));
	kills = isl_union_map_union(isl_union_map_copy(ps->tagged_must_writes),
				    isl_union_map_copy(ps->tagged_must_kills));
	isl_union_map_compute_flow(kills, empty,
				isl_union_map_copy(ps->tagged_may_writes),
				schedule, NULL, &covering, NULL, NULL);
	exposed = isl_union_map_copy(ps->tagged_may_writes);
	exposed = isl_union_map_subtract_domain(exposed,
				isl_union_map_domain(covering));
	exposed = isl_union_map_apply_range(tagger, exposed);
	ps->live_out = exposed;
}

/* Compute the flow dependences and the live_in accesses and store
 * the results in ps->dep_flow and ps->live_in.
 * A copy of the flow dependences, tagged with the reference tags
 * is stored in ps->tagged_dep_flow.
 *
 * We first compute ps->tagged_dep_flow, i.e., the tagged flow dependences
 * and then project out the tags.
 */
static void compute_tagged_flow_dep(struct ppcg_scop *ps)
{
	isl_union_map *tagger;
	isl_union_map *schedule;
	isl_union_map *may_flow;
	isl_union_map *live_in, *may_live_in;

	tagger = isl_union_map_copy(ps->tagger);
	schedule = isl_union_map_copy(ps->schedule);
	schedule = isl_union_map_apply_domain(schedule, tagger);
	isl_union_map_compute_flow(isl_union_map_copy(ps->tagged_reads),
				isl_union_map_copy(ps->tagged_must_writes),
				isl_union_map_copy(ps->tagged_may_writes),
				schedule, &ps->tagged_dep_flow, &may_flow,
				&live_in, &may_live_in);
	ps->tagged_dep_flow = isl_union_map_union(ps->tagged_dep_flow,
							may_flow);
	ps->dep_flow = isl_union_map_copy(ps->tagged_dep_flow);
	ps->dep_flow = isl_union_map_zip(ps->dep_flow);
	ps->dep_flow = isl_union_set_unwrap(isl_union_map_domain(ps->dep_flow));
	live_in = isl_union_map_union(live_in, may_live_in);
	ps->live_in = project_out_tags(live_in);
}

/* Compute the order dependences that prevent the potential live ranges
 * from overlapping.
 * "before" contains all pairs of statement iterations where
 * the first is executed before the second according to the original schedule.
 *
 * In particular, construct a union of relations
 *
 *	[R[...] -> R_1[]] -> [W[...] -> R_2[]]
 *
 * where [R[...] -> R_1[]] is the range of one or more live ranges
 * (i.e., a read) and [W[...] -> R_2[]] is the domain of one or more
 * live ranges (i.e., a write).  Moreover, the read and the write
 * access the same memory element and the read occurs before the write
 * in the original schedule.
 * The scheduler allows some of these dependences to be violated, provided
 * the adjacent live ranges are all local (i.e., their domain and range
 * are mapped to the same point by the current schedule band).
 *
 * Note that if a live range is not local, then we need to make
 * sure it does not overlap with _any_ other live range, and not
 * just with the "previous" and/or the "next" live range.
 * We therefore add order dependences between reads and
 * _any_ later potential write.
 *
 * We also need to be careful about writes without a corresponding read.
 * They are already prevented from moving past non-local preceding
 * intervals, but we also need to prevent them from moving past non-local
 * following intervals.  We therefore also add order dependences from
 * potential writes that do not appear in any intervals
 * to all later potential writes.
 * Note that dead code elimination should have removed most of these
 * dead writes, but the dead code elimination may not remove all dead writes,
 * so we need to consider them to be safe.
 */
static void compute_order_dependences(struct ppcg_scop *ps,
	__isl_take isl_union_map *before)
{
	isl_union_map *reads;
	isl_union_map *shared_access;
	isl_union_set *matched;
	isl_union_map *unmatched;
	isl_union_set *domain;

	reads = isl_union_map_copy(ps->tagged_reads);
	matched = isl_union_map_domain(isl_union_map_copy(ps->tagged_dep_flow));
	unmatched = isl_union_map_copy(ps->tagged_may_writes);
	unmatched = isl_union_map_subtract_domain(unmatched, matched);
	reads = isl_union_map_union(reads, unmatched);
	shared_access = isl_union_map_copy(ps->tagged_may_writes);
	shared_access = isl_union_map_reverse(shared_access);
	shared_access = isl_union_map_apply_range(reads, shared_access);
	shared_access = isl_union_map_zip(shared_access);
	shared_access = isl_union_map_intersect_domain(shared_access,
						isl_union_map_wrap(before));
	domain = isl_union_map_domain(isl_union_map_copy(shared_access));
	shared_access = isl_union_map_zip(shared_access);
	ps->dep_order = isl_union_set_unwrap(domain);
	ps->tagged_dep_order = shared_access;
}

/* Compute the external false dependences of the program represented by "scop"
 * in case live range reordering is allowed.
 * "before" contains all pairs of statement iterations where
 * the first is executed before the second according to the original schedule.
 *
 * The anti-dependences are already taken care of by the order dependences.
 * The external false dependences are only used to ensure that live-in and
 * live-out data is not overwritten by any writes inside the scop.
 *
 * In particular, the reads from live-in data need to precede any
 * later write to the same memory element.
 * As to live-out data, the last writes need to remain the last writes.
 * That is, any earlier write in the original schedule needs to precede
 * the last write to the same memory element in the computed schedule.
 * The possible last writes have been computed by compute_live_out.
 * They may include kills, but if the last access is a kill,
 * then the corresponding dependences will effectively be ignored
 * since we do not schedule any kill statements.
 *
 * Note that the set of live-in and live-out accesses may be
 * an overapproximation.  There may therefore be potential writes
 * before a live-in access and after a live-out access.
 */
static void compute_external_false_dependences(struct ppcg_scop *ps,
	__isl_take isl_union_map *before)
{
	isl_union_map *shared_access;
	isl_union_map *exposed;
	isl_union_map *live_in;

	exposed = isl_union_map_copy(ps->live_out);

	exposed = isl_union_map_reverse(exposed);
	shared_access = isl_union_map_copy(ps->may_writes);
	shared_access = isl_union_map_apply_range(shared_access, exposed);

	ps->dep_external = shared_access;

	live_in = isl_union_map_apply_range(isl_union_map_copy(ps->live_in),
		    isl_union_map_reverse(isl_union_map_copy(ps->may_writes)));

	ps->dep_external = isl_union_map_union(ps->dep_external, live_in);
	ps->dep_external = isl_union_map_intersect(ps->dep_external, before);
}

/* Compute the dependences of the program represented by "scop"
 * in case live range reordering is allowed.
 *
 * We compute the actual live ranges and the corresponding order
 * false dependences.
 */
static void compute_live_range_reordering_dependences(struct ppcg_scop *ps)
{
	isl_union_map *before;

	before = isl_union_map_lex_lt_union_map(
			isl_union_map_copy(ps->schedule),
			isl_union_map_copy(ps->schedule));

	compute_tagged_flow_dep(ps);
	compute_order_dependences(ps, isl_union_map_copy(before));
	compute_external_false_dependences(ps, before);
}

/* Compute the potential flow dependences and the potential live in
 * accesses.
 */
static void compute_flow_dep(struct ppcg_scop *ps)
{
	isl_union_map *may_flow;
	isl_union_map *may_live_in;

	isl_union_map_compute_flow(isl_union_map_copy(ps->reads),
				isl_union_map_copy(ps->must_writes),
				isl_union_map_copy(ps->may_writes),
				isl_union_map_copy(ps->schedule),
				&ps->dep_flow, &may_flow,
				&ps->live_in, &may_live_in);

	ps->dep_flow = isl_union_map_union(ps->dep_flow, may_flow);
	ps->live_in = isl_union_map_union(ps->live_in, may_live_in);
}

/* Compute the dependences of the program represented by "scop".
 * Store the computed potential flow dependences
 * in scop->dep_flow and the reads with potentially no corresponding writes in
 * scop->live_in.
 * Store the potential live out accesses in scop->live_out.
 * Store the potential false (anti and output) dependences in scop->dep_false.
 *
 * If live range reordering is allowed, then we compute a separate
 * set of order dependences and a set of external false dependences
 * in compute_live_range_reordering_dependences.
 */
static void compute_dependences(struct ppcg_scop *scop)
{
	isl_union_map *dep1, *dep2;
	isl_union_map *may_source;

	if (!scop)
		return;

	compute_live_out(scop);

	if (scop->options->live_range_reordering)
		compute_live_range_reordering_dependences(scop);
	else if (scop->options->target != PPCG_TARGET_C)
		compute_tagged_flow_dep(scop);
	else
		compute_flow_dep(scop);

	may_source = isl_union_map_union(isl_union_map_copy(scop->may_writes),
					isl_union_map_copy(scop->reads));
	isl_union_map_compute_flow(isl_union_map_copy(scop->may_writes),
				isl_union_map_copy(scop->must_writes),
				may_source, isl_union_map_copy(scop->schedule),
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
	isl_union_set *live;
	isl_union_map *dep;

	live = isl_union_map_domain(isl_union_map_copy(ps->live_out));
	if (!isl_union_set_is_empty(ps->call)) {
		live = isl_union_set_union(live, isl_union_set_copy(ps->call));
		live = isl_union_set_coalesce(live);
	}

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

static void *ppcg_scop_free(struct ppcg_scop *ps)
{
	if (!ps)
		return NULL;

	isl_set_free(ps->context);
	isl_union_set_free(ps->domain);
	isl_union_set_free(ps->call);
	isl_union_map_free(ps->tagged_reads);
	isl_union_map_free(ps->reads);
	isl_union_map_free(ps->live_in);
	isl_union_map_free(ps->tagged_may_writes);
	isl_union_map_free(ps->tagged_must_writes);
	isl_union_map_free(ps->may_writes);
	isl_union_map_free(ps->must_writes);
	isl_union_map_free(ps->live_out);
	isl_union_map_free(ps->tagged_must_kills);
	isl_union_map_free(ps->tagged_dep_flow);
	isl_union_map_free(ps->dep_flow);
	isl_union_map_free(ps->dep_false);
	isl_union_map_free(ps->dep_external);
	isl_union_map_free(ps->tagged_dep_order);
	isl_union_map_free(ps->dep_order);
	isl_union_map_free(ps->schedule);
	isl_union_map_free(ps->tagger);
	isl_union_map_free(ps->independence);

	free(ps);

	return NULL;
}

/* Extract a ppcg_scop from a pet_scop.
 *
 * The constructed ppcg_scop refers to elements from the pet_scop
 * so the pet_scop should not be freed before the ppcg_scop.
 */
static struct ppcg_scop *ppcg_scop_from_pet_scop(struct pet_scop *scop,
	struct ppcg_options *options)
{
	int i;
	isl_ctx *ctx;
	struct ppcg_scop *ps;

	if (!scop)
		return NULL;

	ctx = isl_set_get_ctx(scop->context);

	ps = isl_calloc_type(ctx, struct ppcg_scop);
	if (!ps)
		return NULL;

	ps->options = options;
	ps->start = pet_loc_get_start(scop->loc);
	ps->end = pet_loc_get_end(scop->loc);
	ps->context = isl_set_copy(scop->context);
	ps->context = set_intersect_str(ps->context, options->ctx);
	ps->domain = collect_non_kill_domains(scop);
	ps->call = collect_call_domains(scop);
	ps->tagged_reads = pet_scop_collect_tagged_may_reads(scop);
	ps->reads = pet_scop_collect_may_reads(scop);
	ps->tagged_may_writes = pet_scop_collect_tagged_may_writes(scop);
	ps->may_writes = pet_scop_collect_may_writes(scop);
	ps->tagged_must_writes = pet_scop_collect_tagged_must_writes(scop);
	ps->must_writes = pet_scop_collect_must_writes(scop);
	ps->tagged_must_kills = pet_scop_collect_tagged_must_kills(scop);
	ps->schedule = pet_scop_collect_schedule(scop);
	ps->n_type = scop->n_type;
	ps->types = scop->types;
	ps->n_array = scop->n_array;
	ps->arrays = scop->arrays;
	ps->n_stmt = scop->n_stmt;
	ps->stmts = scop->stmts;
	ps->n_independence = scop->n_independence;
	ps->independences = scop->independences;
	ps->independence = isl_union_map_empty(isl_set_get_space(ps->context));
	for (i = 0; i < ps->n_independence; ++i)
		ps->independence = isl_union_map_union(ps->independence,
			isl_union_map_copy(ps->independences[i]->filter));

	compute_tagger(ps);
	compute_dependences(ps);
	eliminate_dead_code(ps);

	if (!ps->context || !ps->domain || !ps->call || !ps->reads ||
	    !ps->may_writes || !ps->must_writes || !ps->tagged_must_kills ||
	    !ps->schedule || !ps->independence)
		return ppcg_scop_free(ps);

	return ps;
}

/* Internal data structure for ppcg_transform.
 */
struct ppcg_transform_data {
	struct ppcg_options *options;
	__isl_give isl_printer *(*transform)(__isl_take isl_printer *p,
		struct ppcg_scop *scop, void *user);
	void *user;
};

/* Callback for pet_transform_C_source that transforms
 * the given pet_scop to a ppcg_scop before calling the
 * ppcg_transform callback.
 *
 * If "scop" contains any data dependent conditions or if we may
 * not be able to print the transformed program, then just print
 * the original code.
 */
static __isl_give isl_printer *transform(__isl_take isl_printer *p,
	struct pet_scop *scop, void *user)
{
	struct ppcg_transform_data *data = user;
	struct ppcg_scop *ps;

	if (!pet_scop_can_build_ast_exprs(scop) ||
	    pet_scop_has_data_dependent_conditions(scop)) {
		p = pet_scop_print_original(scop, p);
		pet_scop_free(scop);
		return p;
	}

	scop = pet_scop_align_params(scop);
	ps = ppcg_scop_from_pet_scop(scop, data->options);

	p = data->transform(p, ps, data->user);

	ppcg_scop_free(ps);
	pet_scop_free(scop);

	return p;
}

/* Transform the C source file "input" by rewriting each scop
 * through a call to "transform".
 * The transformed C code is written to "out".
 *
 * This is a wrapper around pet_transform_C_source that transforms
 * the pet_scop to a ppcg_scop before calling "fn".
 */
int ppcg_transform(isl_ctx *ctx, const char *input, FILE *out,
	struct ppcg_options *options,
	__isl_give isl_printer *(*fn)(__isl_take isl_printer *p,
		struct ppcg_scop *scop, void *user), void *user)
{
	struct ppcg_transform_data data = { options, fn, user };
	return pet_transform_C_source(ctx, input, out, &transform, &data);
}

/* Check consistency of options.
 *
 * Return -1 on error.
 */
static int check_options(isl_ctx *ctx)
{
	struct options *options;

	options = isl_ctx_peek_options(ctx, &options_args);
	if (!options)
		isl_die(ctx, isl_error_internal,
			"unable to find options", return -1);

	if (options->ppcg->openmp &&
	    !isl_options_get_ast_build_atomic_upper_bound(ctx))
		isl_die(ctx, isl_error_invalid,
			"OpenMP requires atomic bounds", return -1);

	return 0;
}

int main(int argc, char **argv)
{
	int r;
	isl_ctx *ctx;
	struct options *options;

	options = options_new_with_defaults();
	assert(options);

	ctx = isl_ctx_alloc_with_options(&options_args, options);
	isl_options_set_schedule_outer_coincidence(ctx, 1);
	isl_options_set_schedule_maximize_band_depth(ctx, 1);
	pet_options_set_encapsulate_dynamic_control(ctx, 1);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	if (check_options(ctx) < 0)
		r = EXIT_FAILURE;
	else if (options->ppcg->target == PPCG_TARGET_CUDA)
		r = generate_cuda(ctx, options->ppcg, options->input);
	else if (options->ppcg->target == PPCG_TARGET_OPENCL)
		r = generate_opencl(ctx, options->ppcg, options->input,
				options->output);
	else
		r = generate_cpu(ctx, options->ppcg, options->input,
				options->output);

	isl_ctx_free(ctx);

	return r;
}
