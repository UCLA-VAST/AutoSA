/*
 * Copyright 2011      INRIA Saclay
 * Copyright 2013      Ecole Normale Superieure
 * Copyright 2015      Sven Verdoolaege
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
#include <isl/id.h>
#include <isl/val.h>
#include <isl/set.h>
#include <isl/union_set.h>
#include <isl/union_map.h>
#include <isl/space.h>
#include <isl/aff.h>
#include <isl/flow.h>
#include <isl/options.h>
#include <isl/schedule.h>
#include <isl/ast.h>
#include <isl/id_to_ast_expr.h>
#include <isl/ast_build.h>
#include <isl/schedule.h>
#include <isl/constraint.h>
#include <pet.h>
#include <math.h>
#include "ppcg.h"
#include "ppcg_options.h"
//#include "cuda.h"
//#include "opencl.h"
//#include "cpu.h"
#include "autosa_xilinx_hls_c.h"
#include "autosa_intel_opencl.h"
#include "autosa_catapult_hls_c.h"
#include "autosa_tapa_cpp.h"

//#define _DEBUG

struct options {
	struct pet_options *pet;
	struct ppcg_options *ppcg;
	char *input;
	char *output;
};

//const char *ppcg_version(void);
//static void print_version(void)
//{
//	printf("%s", ppcg_version());
//}

ISL_ARGS_START(struct options, options_args)
ISL_ARG_CHILD(struct options, pet, "pet", &pet_options_args, "pet options")
ISL_ARG_CHILD(struct options, ppcg, NULL, &ppcg_options_args, "ppcg options")
ISL_ARG_STR(struct options, output, 'o', NULL,
	"filename", NULL, "output filename (c and opencl targets)")
ISL_ARG_ARG(struct options, input, "input", NULL)
//ISL_ARG_VERSION(print_version)
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

/* Does "scop" refer to any arrays that are declared, but not
 * exposed to the code after the scop?
 */
int ppcg_scop_any_hidden_declarations(struct ppcg_scop *scop)
{
	int i;

	if (!scop)
		return 0;

	for (i = 0; i < scop->pet->n_array; ++i)
		if (scop->pet->arrays[i]->declared &&
		    !scop->pet->arrays[i]->exposed)
			return 1;

	return 0;
}

/* Collect all variable names that are in use in "scop".
 * In particular, collect all parameters in the context and
 * all the array names.
 * Store these names in an isl_id_to_ast_expr by mapping
 * them to a dummy value (0).
 */
static __isl_give isl_id_to_ast_expr *collect_names(struct pet_scop *scop)
{
	int i, n;
	isl_ctx *ctx;
	isl_ast_expr *zero;
	isl_id_to_ast_expr *names;

	ctx = isl_set_get_ctx(scop->context);

	n = isl_set_dim(scop->context, isl_dim_param);

	names = isl_id_to_ast_expr_alloc(ctx, n + scop->n_array);
	zero = isl_ast_expr_from_val(isl_val_zero(ctx));

	for (i = 0; i < n; ++i) {
		isl_id *id;

		id = isl_set_get_dim_id(scop->context, isl_dim_param, i);
		names = isl_id_to_ast_expr_set(names,
						id, isl_ast_expr_copy(zero));
	}

	for (i = 0; i < scop->n_array; ++i) {
		struct pet_array *array = scop->arrays[i];
		isl_id *id;

		id = isl_set_get_tuple_id(array->extent);
		names = isl_id_to_ast_expr_set(names,
						id, isl_ast_expr_copy(zero));
	}

	isl_ast_expr_free(zero);

	return names;
}

/* Return an isl_id called "prefix%d", with "%d" set to "i".
 * If an isl_id with such a name already appears among the variable names
 * of "scop", then adjust the name to "prefix%d_%d".
 */
static __isl_give isl_id *generate_name(struct ppcg_scop *scop,
	const char *prefix, int i)
{
	int j;
	char name[23];
	isl_ctx *ctx;
	isl_id *id;
	int has_name;

	ctx = isl_set_get_ctx(scop->context);
	snprintf(name, sizeof(name), "%s%d", prefix, i);
	id = isl_id_alloc(ctx, name, NULL);

	j = 0;
	while ((has_name = isl_id_to_ast_expr_has(scop->names, id)) == 1) {
		isl_id_free(id);
		snprintf(name, sizeof(name), "%s%d_%d", prefix, i, j++);
		id = isl_id_alloc(ctx, name, NULL);
	}

	return has_name < 0 ? isl_id_free(id) : id;
}

/* Return a list of "n" isl_ids of the form "prefix%d".
 * If an isl_id with such a name already appears among the variable names
 * of "scop", then adjust the name to "prefix%d_%d".
 */
__isl_give isl_id_list *ppcg_scop_generate_names(struct ppcg_scop *scop,
	int n, const char *prefix)
{
	int i;
	isl_ctx *ctx;
	isl_id_list *names;

	ctx = isl_set_get_ctx(scop->context);
	names = isl_id_list_alloc(ctx, n);
	for (i = 0; i < n; ++i) {
		isl_id *id;

		id = generate_name(scop, prefix, i);
		names = isl_id_list_add(names, id);
	}

	return names;
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
	return isl_union_map_domain_factor_domain(umap);
}

/* Construct a function from tagged iteration domains to the corresponding
 * untagged iteration domains with as range of the wrapped map in the domain
 * the reference tags that appear in any of the reads, writes or kills.
 * Store the result in ps->tagger.
 *
 * For example, if the statement with iteration space S[i,j]
 * contains two array references R_1[] and R_2[], then ps->tagger will contain
 *
 *	{ [S[i,j] -> R_1[]] -> S[i,j]; [S[i,j] -> R_2[]] -> S[i,j] }
 */
static void compute_tagger(struct ppcg_scop *ps)
{
	isl_union_map *tagged;
	isl_union_pw_multi_aff *tagger;

	tagged = isl_union_map_copy(ps->tagged_reads);
	tagged = isl_union_map_union(tagged,
				isl_union_map_copy(ps->tagged_may_writes));
	tagged = isl_union_map_union(tagged,
				isl_union_map_copy(ps->tagged_must_kills));
	tagged = isl_union_map_universe(tagged);
	tagged = isl_union_set_unwrap(isl_union_map_domain(tagged));

	tagger = isl_union_map_domain_map_union_pw_multi_aff(tagged);

	ps->tagger = tagger;
}

/* Compute the live out accesses, i.e., the writes that are
 * potentially not killed by any kills or any other writes, and
 * store them in ps->live_out.
 *
 * We compute the "dependence" of any "kill" (an explicit kill
 * or a must write) on any may write.
 * The elements accessed by the may writes with a "depending" kill
 * also accessing the element are definitely killed.
 * The remaining may writes can potentially be live out.
 *
 * The result of the dependence analysis is
 *
 *	{ IW -> [IK -> A] }
 *
 * with IW the instance of the write statement, IK the instance of kill
 * statement and A the element that was killed.
 * The range factor range is
 *
 *	{ IW -> A }
 *
 * containing all such pairs for which there is a kill statement instance,
 * i.e., all pairs that have been killed.
 */
static void compute_live_out(struct ppcg_scop *ps)
{
	isl_schedule *schedule;
	isl_union_map *kills;
	isl_union_map *exposed;
	isl_union_map *covering;
	isl_union_access_info *access;
	isl_union_flow *flow;

	schedule = isl_schedule_copy(ps->schedule);
	kills = isl_union_map_union(isl_union_map_copy(ps->must_writes),
				    isl_union_map_copy(ps->must_kills));
	access = isl_union_access_info_from_sink(kills);
	access = isl_union_access_info_set_may_source(access,
				    isl_union_map_copy(ps->may_writes));
	access = isl_union_access_info_set_schedule(access, schedule);
	flow = isl_union_access_info_compute_flow(access);
	covering = isl_union_flow_get_full_may_dependence(flow);
	isl_union_flow_free(flow);

	covering = isl_union_map_range_factor_range(covering);
	exposed = isl_union_map_copy(ps->may_writes);
	exposed = isl_union_map_subtract(exposed, covering);
	ps->live_out = exposed;
}

/* Compute the tagged flow dependences and the live_in accesses and store
 * the results in ps->tagged_dep_flow and ps->live_in.
 *
 * Both must-writes and must-kills are allowed to kill dependences
 * from earlier writes to subsequent reads.
 * The must-kills are not included in the potential sources, though.
 * The flow dependences with a must-kill as source would
 * reflect possibly uninitialized reads.
 * No dependences need to be introduced to protect such reads
 * (other than those imposed by potential flows from may writes
 * that follow the kill).  Those flow dependences are therefore not needed.
 * The dead code elimination also assumes
 * the flow sources are non-kill instances.
 */
static void compute_tagged_flow_dep_only(struct ppcg_scop *ps)
{
	isl_union_pw_multi_aff *tagger;
	isl_schedule *schedule;
	isl_union_map *live_in;
	isl_union_access_info *access;
	isl_union_flow *flow;
	isl_union_map *must_source;
	isl_union_map *kills;
	isl_union_map *tagged_flow;

	tagger = isl_union_pw_multi_aff_copy(ps->tagger);
	schedule = isl_schedule_copy(ps->schedule);
	schedule = isl_schedule_pullback_union_pw_multi_aff(schedule, tagger);
	kills = isl_union_map_copy(ps->tagged_must_kills);
	must_source = isl_union_map_copy(ps->tagged_must_writes);
	kills = isl_union_map_union(kills, must_source);
	access = isl_union_access_info_from_sink(
				isl_union_map_copy(ps->tagged_reads));
	access = isl_union_access_info_set_kill(access, kills);
	access = isl_union_access_info_set_may_source(access,
				isl_union_map_copy(ps->tagged_may_writes));
	access = isl_union_access_info_set_schedule(access, schedule);
	flow = isl_union_access_info_compute_flow(access);
	tagged_flow = isl_union_flow_get_may_dependence(flow);
	ps->tagged_dep_flow = tagged_flow;
	live_in = isl_union_flow_get_may_no_source(flow);
	ps->live_in = project_out_tags(live_in);
	isl_union_flow_free(flow);
}

/* Compute ps->dep_flow from ps->tagged_dep_flow
 * by projecting out the reference tags.
 */
static void derive_flow_dep_from_tagged_flow_dep(struct ppcg_scop *ps)
{
	ps->dep_flow = isl_union_map_copy(ps->tagged_dep_flow);
	ps->dep_flow = isl_union_map_factor_domain(ps->dep_flow);
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
	compute_tagged_flow_dep_only(ps);
	derive_flow_dep_from_tagged_flow_dep(ps);
}

/* Compute the order dependences that prevent the potential live ranges
 * from overlapping.
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
 *
 * The order dependences are computed by computing the "dataflow"
 * from the above unmatched writes and the reads to the may writes.
 * The unmatched writes and the reads are treated as may sources
 * such that they would not kill order dependences from earlier
 * such writes and reads.
 */
static void compute_order_dependences(struct ppcg_scop *ps)
{
	isl_union_map *reads;
	isl_union_map *shared_access;
	isl_union_set *matched;
	isl_union_map *unmatched;
	isl_union_pw_multi_aff *tagger;
	isl_schedule *schedule;
	isl_union_access_info *access;
	isl_union_flow *flow;

	tagger = isl_union_pw_multi_aff_copy(ps->tagger);
	schedule = isl_schedule_copy(ps->schedule);
	schedule = isl_schedule_pullback_union_pw_multi_aff(schedule, tagger);
	reads = isl_union_map_copy(ps->tagged_reads);
	matched = isl_union_map_domain(isl_union_map_copy(ps->tagged_dep_flow));
	unmatched = isl_union_map_copy(ps->tagged_may_writes);
	unmatched = isl_union_map_subtract_domain(unmatched, matched);
	reads = isl_union_map_union(reads, unmatched);
	access = isl_union_access_info_from_sink(
				isl_union_map_copy(ps->tagged_may_writes));
	access = isl_union_access_info_set_may_source(access, reads);
	access = isl_union_access_info_set_schedule(access, schedule);
	flow = isl_union_access_info_compute_flow(access);
	shared_access = isl_union_flow_get_may_dependence(flow);
	isl_union_flow_free(flow);

	ps->tagged_dep_order = isl_union_map_copy(shared_access);
	ps->dep_order = isl_union_map_factor_domain(shared_access);
}

/* Compute those validity dependences of the program represented by "scop"
 * that should be unconditionally enforced even when live-range reordering
 * is used.
 *
 * In particular, compute the external false dependences
 * as well as order dependences between sources with the same sink.
 * The anti-dependences are already taken care of by the order dependences.
 * The external false dependences are only used to ensure that live-in and
 * live-out data is not overwritten by any writes inside the scop.
 * The independences are removed from the external false dependences,
 * but not from the order dependences between sources with the same sink.
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
 *
 * In the presence of may-writes, there may be multiple live-ranges
 * with the same sink, accessing the same memory element.
 * The sources of these live-ranges need to be executed
 * in the same relative order as in the original program
 * since we do not know which of the may-writes will actually
 * perform a write.  Consider all sources that share a sink and
 * that may write to the same memory element and compute
 * the order dependences among them.
 */
static void compute_forced_dependences(struct ppcg_scop *ps)
{
	isl_union_map *shared_access;
	isl_union_map *exposed;
	isl_union_map *live_in;
	isl_union_map *sink_access;
	isl_union_map *shared_sink;
	isl_union_access_info *access;
	isl_union_flow *flow;
	isl_schedule *schedule;

	exposed = isl_union_map_copy(ps->live_out);
	schedule = isl_schedule_copy(ps->schedule);
	access = isl_union_access_info_from_sink(exposed);
	access = isl_union_access_info_set_may_source(access,
				isl_union_map_copy(ps->may_writes));
	access = isl_union_access_info_set_schedule(access, schedule);
	flow = isl_union_access_info_compute_flow(access);
	shared_access = isl_union_flow_get_may_dependence(flow);
	isl_union_flow_free(flow);
	ps->dep_forced = shared_access;

	schedule = isl_schedule_copy(ps->schedule);
	access = isl_union_access_info_from_sink(
				isl_union_map_copy(ps->may_writes));
	access = isl_union_access_info_set_may_source(access,
				isl_union_map_copy(ps->live_in));
	access = isl_union_access_info_set_schedule(access, schedule);
	flow = isl_union_access_info_compute_flow(access);
	live_in = isl_union_flow_get_may_dependence(flow);
	isl_union_flow_free(flow);

	ps->dep_forced = isl_union_map_union(ps->dep_forced, live_in);
	ps->dep_forced = isl_union_map_subtract(ps->dep_forced,
				isl_union_map_copy(ps->independence));

	schedule = isl_schedule_copy(ps->schedule);
	sink_access = isl_union_map_copy(ps->tagged_dep_flow);
	sink_access = isl_union_map_range_product(sink_access,
				isl_union_map_copy(ps->tagged_may_writes));
	sink_access = isl_union_map_domain_factor_domain(sink_access);
	access = isl_union_access_info_from_sink(
				isl_union_map_copy(sink_access));
	access = isl_union_access_info_set_may_source(access, sink_access);
	access = isl_union_access_info_set_schedule(access, schedule);
	flow = isl_union_access_info_compute_flow(access);
	shared_sink = isl_union_flow_get_may_dependence(flow);
	isl_union_flow_free(flow);
	ps->dep_forced = isl_union_map_union(ps->dep_forced, shared_sink);
}

/* Remove independence from the tagged flow dependences.
 * Since the user has guaranteed that source and sink of an independence
 * can be executed in any order, there cannot be a flow dependence
 * between them, so they can be removed from the set of flow dependences.
 * However, if the source of such a flow dependence is a must write,
 * then it may have killed other potential sources, which would have
 * to be recovered if we were to remove those flow dependences.
 * We therefore keep the flow dependences that originate in a must write,
 * even if it corresponds to a known independence.
 */
static void remove_independences_from_tagged_flow(struct ppcg_scop *ps)
{
	isl_union_map *tf;
	isl_union_set *indep;
	isl_union_set *mw;

	tf = isl_union_map_copy(ps->tagged_dep_flow);
	tf = isl_union_map_zip(tf);
	indep = isl_union_map_wrap(isl_union_map_copy(ps->independence));
	tf = isl_union_map_intersect_domain(tf, indep);
	tf = isl_union_map_zip(tf);
	mw = isl_union_map_domain(isl_union_map_copy(ps->tagged_must_writes));
	tf = isl_union_map_subtract_domain(tf, mw);
	ps->tagged_dep_flow = isl_union_map_subtract(ps->tagged_dep_flow, tf);
}

/* Compute the dependences of the program represented by "scop"
 * in case live range reordering is allowed.
 *
 * We compute the actual live ranges and the corresponding order
 * false dependences.
 *
 * The independences are removed from the flow dependences
 * (provided the source is not a must-write) as well as
 * from the external false dependences (by compute_forced_dependences).
 */
static void compute_live_range_reordering_dependences(struct ppcg_scop *ps)
{
	compute_tagged_flow_dep_only(ps);
	remove_independences_from_tagged_flow(ps);
	derive_flow_dep_from_tagged_flow_dep(ps);
	compute_order_dependences(ps);
	compute_forced_dependences(ps);
}

/* Compute the potential flow dependences and the potential live in
 * accesses.
 *
 * Both must-writes and must-kills are allowed to kill dependences
 * from earlier writes to subsequent reads, as in compute_tagged_flow_dep_only.
 */
static void compute_flow_dep(struct ppcg_scop *ps)
{
	isl_union_access_info *access;
	isl_union_flow *flow;
	isl_union_map *kills, *must_writes;

	access = isl_union_access_info_from_sink(isl_union_map_copy(ps->reads));
	kills = isl_union_map_copy(ps->must_kills);
	must_writes = isl_union_map_copy(ps->must_writes);
	kills = isl_union_map_union(kills, must_writes);
	access = isl_union_access_info_set_kill(access, kills);
	access = isl_union_access_info_set_may_source(access,
				isl_union_map_copy(ps->may_writes));
	access = isl_union_access_info_set_schedule(access,
				isl_schedule_copy(ps->schedule));
	flow = isl_union_access_info_compute_flow(access);

	ps->dep_flow = isl_union_flow_get_may_dependence(flow);
	ps->live_in = isl_union_flow_get_may_no_source(flow);
	isl_union_flow_free(flow);
}

/* Examine if the access "map" is an external access, i.e., it is not
 * associated with flow deps.
 */
static isl_bool is_external_access(__isl_keep isl_map *map, void *user) 
{
  isl_map *read_access = (isl_map *)(user);
  /* The read access is in the format of
   * {[S1[] -> pet_ref1] -> A[]}
   */
  isl_space *read_access_space = isl_map_get_space(read_access);
  /* Factor the read access to
   * {pet_ref[] -> A[]}
   */
  read_access_space = isl_space_domain_factor_range(read_access_space);
  const char *read_access_name = isl_space_get_tuple_name(read_access_space, isl_dim_in);

  /* The flow dpendence is in the format of
   * {[S1[] -> pet_ref1] -> [S1[] -> pet_ref2]}
   * We factor it to
   * {pet_ref1[] -> pet_ref2[]}
   */
  isl_map *dep = isl_map_factor_range(isl_map_copy(map));
  isl_space *dep_space = isl_map_get_space(dep);
  const char *dep_src_name = isl_space_get_tuple_name(dep_space, isl_dim_in);
  const char *dep_sink_name = isl_space_get_tuple_name(dep_space, isl_dim_out);
  isl_map_free(dep);

  /* Compare if the read access name equals either source or sink access name
   * in the flow dependence.
   */
  if (!strcmp(read_access_name, dep_src_name) || !strcmp(read_access_name, dep_sink_name)) {
    isl_space_free(read_access_space);
    isl_space_free(dep_space);
    return isl_bool_false;
  } else {
    isl_space_free(read_access_space);
    isl_space_free(dep_space);   
    return isl_bool_true;
  }
}

/* This function takes the tagged access relation in the format of
 * {[S1[] -> pet_ref..] -> A[i,j]}
 * and returns the access matrix.
 */
static __isl_give isl_mat *get_acc_mat_from_tagged_acc(__isl_keep isl_map *map) 
{
  isl_map *acc = isl_map_domain_factor_domain(isl_map_copy(map));
  /* The parameters and constants are truncated. */
  isl_mat *acc_mat = isl_mat_alloc(isl_map_get_ctx(acc), isl_map_dim(acc, isl_dim_out), isl_map_dim(acc, isl_dim_in));
  /* Fill in the matrix. */
  assert(isl_map_n_basic_map(acc) == 1);
  isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(acc);
  isl_basic_map *bmap = isl_basic_map_list_get_basic_map(bmap_list, 0);

  isl_mat *eq_mat = isl_basic_map_equalities_matrix(bmap, isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);
  isl_mat *ieq_mat = isl_basic_map_inequalities_matrix(bmap, isl_dim_out, isl_dim_in, isl_dim_div, isl_dim_param, isl_dim_cst);

  for (int row = 0; row < isl_mat_rows(eq_mat); row++) {
    isl_val *sum = isl_val_zero(isl_basic_map_get_ctx(bmap));
    int index;
    for (int col = 0; col < isl_basic_map_dim(bmap, isl_dim_out); col++) {
      sum = isl_val_add(sum, isl_val_abs(isl_mat_get_element_val(eq_mat, row, col)));
      isl_val *mat_val = isl_mat_get_element_val(eq_mat, row, col);
      if (isl_val_is_one(mat_val)) {
        index = col;
      }
      isl_val_free(mat_val);
    }
    if (!isl_val_is_one(sum)) {
      isl_val_free(sum);
      continue;
    }
    for (int col = 0; col < isl_basic_map_dim(bmap, isl_dim_in); col++) {
      isl_mat_set_element_val(acc_mat, index, col, isl_val_neg(isl_mat_get_element_val(eq_mat, row, col + isl_basic_map_dim(bmap, isl_dim_out))));
    }
    isl_val_free(sum);
  }

  isl_mat_free(eq_mat);
  isl_mat_free(ieq_mat);
  isl_map_free(acc);

  isl_basic_map_list_free(bmap_list);
  isl_basic_map_free(bmap);

  return acc_mat;
}

/* There could be mulitple solutions (basis) in the null space. 
 * This function finds one solution based on the heuristics below:
 * Dependence distance with the simpler pattern is preferred.
 *  
 * We first count the non-zero components in the dependence vector, 
 * and select those with the least non-zero components. 
 * Then, among those with the same number of non-zero components, 
 * we select ones with the least absolute value of the score computed by:
 * sum(abs(ele_of_dep) * 2^(loop_depth)).
 * We favor non-zero components at the upper level, since they are more likely
 * to be carried by the space loops.
 *
 * For T2S only:
 * At the second phase of tiled T2S code generation,
 * the coefficients  at space loop dimensions should be no less than zero.
 * For now, we will set any dependence vector with negative coefficient with a negative
 * score -1.
 * 
 * Temporary: We only allow one non-zero component in the reuse vector to simplify
 * the generation of hardware. We may relax it in the future.
 */
static int rar_sol_smart_pick(
  __isl_keep isl_mat *mat, struct ppcg_scop *ps, int *n_candidates, int *n_default, int user_choice)
{
  int score[isl_mat_cols(mat)];
  int depth = isl_mat_rows(mat);
  int pick_idx = -1;
  int min_score = 0;  
  int min_non_zero_cnt = -1;
  int non_zero_cnts[isl_mat_cols(mat)];

  for (int c = 0; c < isl_mat_cols(mat); c++) {
    int non_zero_cnt = 0;
    for (int r = 0; r < isl_mat_rows(mat); r++) {
      isl_val *val = isl_mat_get_element_val(mat, r, c);
      long val_int = isl_val_get_num_si(val);
      isl_val_free(val);
      if (val_int != 0)
        non_zero_cnt++;
    }
    non_zero_cnts[c] = non_zero_cnt;
    if (min_non_zero_cnt == -1) {
      min_non_zero_cnt = non_zero_cnt;    
    } else {
      if (non_zero_cnt < min_non_zero_cnt)
        min_non_zero_cnt = non_zero_cnt;
    }
  }

  /* Temporary: We only allow one non-zero component in the reuse vector to simplify
   * the generation of hardware. We may relax it in the future.
   */
  if (min_non_zero_cnt > 1) {
	return pick_idx;
  }
  
  for (int c = 0; c < isl_mat_cols(mat); c++) {
    score[c] = 0; 
    for (int r = 0; r < isl_mat_rows(mat); r++) {
      isl_val *val = isl_mat_get_element_val(mat, r, c);
      long val_int = isl_val_get_num_si(val);
      score[c] += abs(val_int) * pow(2, r);    
      isl_val_free(val);
      if (ps->options->autosa->t2s_tile && 
						ps->options->autosa->t2s_tile_phase == 1) {
        if (val_int < 0) {
          score[c] = -1;
          break;
        }
      }
    }
    if (score[c] >= 0 && non_zero_cnts[c] == min_non_zero_cnt) {
	  if (user_choice == -1) {
	    printf("[AutoSA] Candidate %d: ", *n_candidates);
	    isl_printer *p_tmp = isl_printer_to_file(isl_mat_get_ctx(mat), stdout);
	    isl_vec *sol_tmp = isl_vec_alloc(isl_mat_get_ctx(mat), isl_mat_rows(mat));
	    for (int r = 0; r < isl_mat_rows(mat); r++) {
	  	  sol_tmp = isl_vec_set_element_val(sol_tmp, r, isl_mat_get_element_val(mat, r, c));
	    }
	    p_tmp = isl_printer_print_vec(p_tmp, sol_tmp);	  
	    isl_printer_free(p_tmp);
	    isl_vec_free(sol_tmp);
	    printf("\n");
		if (pick_idx == -1) {
          pick_idx = c;
          min_score = score[c];
		  *n_default = *n_candidates;
        } else {
          if (min_score > score[c]) {
            pick_idx = c;
            min_score = score[c];
		    *n_default = *n_candidates;
          }
        }
	  }	else {
	    if (user_choice == *n_candidates) {
		  pick_idx = c;
	      break;
		}
	  }
	  (*n_candidates)++;
    }
  }

  return pick_idx;
}

/* Construct a pseudo RAR dependence that is an identity map of the read access. */
static __isl_give isl_map *construct_pseudo_dep_rar(__isl_keep isl_map *map)
{
	isl_set *set;

//#ifdef _DEBUG
//	DBGMAP(stdout, map, isl_map_get_ctx(map));
//#endif
	set = isl_map_domain(isl_map_copy(map));
	isl_map *dep_map;
	dep_map = isl_set_identity(set);
//#ifdef _DEBUG
//	DBGMAP(stdout, dep_map, isl_map_get_ctx(dep_map));
//#endif

	return dep_map;
}

/* Construct the RAR dependence based on the dependence vector in "sol" and the 
 * access relation "map".
 */
static __isl_give isl_map *construct_dep_rar(__isl_keep isl_vec *sol, 
	__isl_keep isl_map *map) 
{
  /* Build the space. */
  isl_space *space = isl_map_get_space(map);
  space = isl_space_domain(space);
  isl_space *space_d = isl_space_factor_domain(isl_space_copy(space));
  isl_space *space_r = isl_space_factor_range(isl_space_copy(space));

  isl_space *space_d_d = isl_space_map_from_domain_and_range(space_d, isl_space_copy(space_d));
  isl_space *space_r_r = isl_space_map_from_domain_and_range(space_r, isl_space_copy(space_r));

  isl_space_free(space);
  space = isl_space_product(space_d_d, space_r_r);
  isl_map *dep_map = isl_map_universe(isl_space_copy(space));

  /* Add the dep vector constraint. */
  isl_local_space *ls = isl_local_space_from_space(space);
  for (int i = 0; i < isl_vec_size(sol); i++) {
    isl_constraint *cst = isl_constraint_alloc_equality(isl_local_space_copy(ls));
    isl_constraint_set_coefficient_si(cst, isl_dim_in, i, 1);
    isl_constraint_set_coefficient_si(cst, isl_dim_out, i, -1);
    isl_constraint_set_constant_val(cst, isl_vec_get_element_val(sol, i));
    dep_map = isl_map_add_constraint(dep_map, cst);
  }

  /* Add the iteration domain constraints. */  
  isl_set *domain = isl_map_domain(isl_map_copy(map));
  isl_map *new_map = isl_map_from_domain_and_range(domain, isl_set_copy(domain));
  dep_map = isl_map_intersect(dep_map, new_map);

  isl_local_space_free(ls);

  return dep_map;
}

struct autosa_extract_size_data
{
  const char *type;
  isl_set *res;
};

/* This function is called for each set in a union_set.
 * If the name of the set matches data->type, we store the
 * set in data->res.
 */
static isl_stat extract_size_of_type(__isl_take isl_set *size, void *user)
{
  struct autosa_extract_size_data *data = (struct autosa_extract_size_data *)user;
  const char *name;

  name = isl_set_get_tuple_name(size);
  if (name && !strcmp(name, data->type))
  {
    data->res = size;
    return isl_stat_error;
  }

  isl_set_free(size);
  return isl_stat_ok;
}

static __isl_give isl_set *extract_sa_sizes(__isl_keep isl_union_map *sizes,
                                     const char *type)
{
  isl_space *space;
  isl_set *dom;
  isl_union_set *local_sizes;
  struct autosa_extract_size_data data = {type, NULL};

  if (!sizes)
    return NULL;

  space = isl_union_map_get_space(sizes);
  space = isl_space_set_from_params(space);  
  space = isl_space_set_tuple_name(space, isl_dim_set, "kernel");
  dom = isl_set_universe(space);  

  local_sizes = isl_union_set_apply(isl_union_set_from_set(dom),
                                    isl_union_map_copy(sizes));
  isl_union_set_foreach_set(local_sizes, &extract_size_of_type, &data);
  isl_union_set_free(local_sizes);
  return data.res;
}

static __isl_give isl_union_map *extract_sizes_from_str(isl_ctx *ctx, const char *str)
{
  if (!str)
    return NULL;
  return isl_union_map_read_from_str(ctx, str);
}

static int read_select_rar_dep_choices(struct ppcg_scop *ps, __isl_keep isl_map *map)
{
  /* Extract the reference name */
  isl_set *domain = isl_map_domain(isl_map_copy(map));
  isl_map *domain_map = isl_set_unwrap(domain);
  isl_space *space = isl_map_get_space(domain_map);
  isl_map_free(domain_map);  
  const char *ref_name = isl_space_get_tuple_name(space, isl_dim_out);
  isl_space_free(space);  
  isl_union_map *sizes = extract_sizes_from_str(isl_map_get_ctx(map), ps->options->autosa->select_rar_dep);
  isl_set *size = extract_sa_sizes(sizes, ref_name);
  isl_union_map_free(sizes);
  int ret = -1;
  if (size) {
    isl_val *v = isl_set_plain_get_val_if_fixed(size, isl_dim_set, 0);
    ret = isl_val_get_num_si(v);
	isl_val_free(v);	
  }
  isl_set_free(size);

  return ret;	
}

/* Builds the RAR dependence for the given access "map".
 * First we examine the access is an external access (not assoiciated with
 * any flow dependence). Next, we compute the null space of the access matrix.
 * At present, we will take one of the solutions in the null space as the 
 * RAR dependence for the given array access. 
 */
static isl_stat build_rar_dep(__isl_take isl_map *map, void *user) {
  struct ppcg_scop *ps = (struct ppcg_scop *)(user);
  isl_map *tagged_dep_rar;
  /* Examine if the read access is an external access. */
  isl_union_map *tagged_dep_flow = ps->tagged_dep_flow;
  isl_bool is_external = isl_union_map_every_map(tagged_dep_flow, &is_external_access, map);
  if (!is_external) {
    isl_map_free(map);
    return isl_stat_ok;
  }

  /* Take the access function and compute the null space */
  isl_mat *acc_mat = get_acc_mat_from_tagged_acc(map); 
  isl_mat *acc_null_mat = isl_mat_right_kernel(acc_mat);
  int nsol = isl_mat_cols(acc_null_mat);  
  if (nsol > 0) {
  	/* Build the RAR dependence.
   	 * TODO: Temporary solution. We will construnct the RAR dep
     * using one independent solution based on hueristics.
     */
	int n_candidates = 0;
	{
	  printf("[AutoSA] Extract RAR dep for the array access: ");
	  isl_space *space = isl_map_get_space(map);
	  isl_map *map_tmp = isl_map_universe(space);
	  isl_printer *p_tmp = isl_printer_to_file(isl_map_get_ctx(map_tmp), stdout);
	  p_tmp = isl_printer_print_map(p_tmp, map_tmp);
	  isl_printer_free(p_tmp);
	  isl_map_free(map_tmp);
	  printf("\n");						
	}
	int default_candidate = -1;
    int col = rar_sol_smart_pick(acc_null_mat, ps, &n_candidates, &default_candidate, -1);
	if (col >= 0) {
	  /* Check if users have specified any choice. */
	  int user_choice = read_select_rar_dep_choices(ps, map);
      if (n_candidates > 1) {
		printf("[AutoSA] Found more than one legal RAR deps. ");
		if (user_choice == -1)
		  printf("Candidate %d is used by default.\n", default_candidate);
		else {
		  printf("Candidate %d is used.\n", user_choice);
		  n_candidates = 0;
		  col = rar_sol_smart_pick(acc_null_mat, ps, &n_candidates, &default_candidate, user_choice);
		}
	  }

      isl_vec *sol = isl_vec_alloc(isl_map_get_ctx(map), isl_mat_rows(acc_null_mat));
      for (int row = 0; row < isl_mat_rows(acc_null_mat); row++) {
        sol = isl_vec_set_element_val(sol, row, isl_mat_get_element_val(acc_null_mat, row, col));
      }
	  //DBGVEC(stdout, sol, isl_vec_get_ctx(sol));
      tagged_dep_rar = construct_dep_rar(sol, map);
//	  DBGMAP(stdout, tagged_dep_rar, isl_map_get_ctx(tagged_dep_rar));
      isl_vec_free(sol);      

	  /* Test if the dependence is empty. In such case, we will build an identity map 
	   * serving as a pseudo-dependence. 
	   */
	  if (isl_map_is_empty(tagged_dep_rar)) {
		isl_map_free(tagged_dep_rar);
		col = -1;
	  } 
	}

	if (col < 0) {
	  tagged_dep_rar = construct_pseudo_dep_rar(map);
	}

    ps->tagged_dep_rar = isl_union_map_union(ps->tagged_dep_rar, isl_union_map_from_map(tagged_dep_rar));
  } else {	
	/* Since there is no data reuse opportunity, we will build an identity map here. */
	tagged_dep_rar = construct_pseudo_dep_rar(map);
	ps->tagged_dep_rar = isl_union_map_union(ps->tagged_dep_rar, isl_union_map_from_map(tagged_dep_rar));
  }

  isl_mat_free(acc_null_mat);
  isl_map_free(map);
  return isl_stat_ok;
}

/* Compute ps->dep_rar from ps->tagged_dep_rar
 * by projecting out the reference tags.
 */
static void derive_rar_dep_from_tagged_rar_dep(struct ppcg_scop *ps)
{
  ps->dep_rar = isl_union_map_copy(ps->tagged_dep_rar);
  ps->dep_rar = isl_union_map_factor_domain(ps->dep_rar);
}

/* Computed the tagged RAR dependence and store the results in
 * ps->tagged_rar_flow.
 */
static void compute_tagged_rar_dep_only(struct ppcg_scop *ps)
{
  /* For each read access, if the read is an external read access,
   * compute the null space of the access function, and 
   * construct the RAR deps based on the independent solution in the null space.
   */
  isl_union_map *tagged_reads = ps->tagged_reads;
  isl_union_map_foreach_map(tagged_reads, &build_rar_dep, ps);
}

/* Compute the RAR dependence for each externel read access.
 * The results are stored in ps->dep_rar.
 * A copy of the RAR dependences, tagged with the reference tags 
 * is stored in ps->tagged_dep_rar.
 *
 * We first compute ps->tagged_dep_rar, i.e., the tagged RAR dependences
 * and then project out the tags.
 */
static void compute_tagged_rar_dep(struct ppcg_scop *ps)
{
  isl_space *space = isl_union_map_get_space(ps->tagged_dep_flow);
  ps->tagged_dep_rar = isl_union_map_empty(
			isl_space_set_alloc(isl_union_map_get_ctx(ps->tagged_dep_flow),
        isl_space_dim(space, isl_dim_param), 0));
  isl_space_free(space);
  compute_tagged_rar_dep_only(ps);
  derive_rar_dep_from_tagged_rar_dep(ps);
}

static void compute_tagged_waw_dep_only(struct ppcg_scop *ps)
{
  isl_union_pw_multi_aff *tagger;
  isl_schedule *schedule;
  isl_union_map *kills;
  isl_union_map *must_source;
  isl_union_access_info *access;
  isl_union_flow *flow;
  isl_union_map *tagged_flow;

  tagger = isl_union_pw_multi_aff_copy(ps->tagger);
  schedule = isl_schedule_copy(ps->schedule);
  schedule = isl_schedule_pullback_union_pw_multi_aff(schedule, tagger);
  kills = isl_union_map_copy(ps->tagged_must_kills);
  must_source = isl_union_map_copy(ps->tagged_must_writes);
  kills = isl_union_map_union(kills, must_source);
  access = isl_union_access_info_from_sink(
      isl_union_map_copy(ps->tagged_may_writes));
  access = isl_union_access_info_set_kill(access, kills);
  access = isl_union_access_info_set_may_source(access, 
      isl_union_map_copy(ps->tagged_may_writes));
  access = isl_union_access_info_set_schedule(access, schedule);
  flow = isl_union_access_info_compute_flow(access);
  tagged_flow = isl_union_flow_get_may_dependence(flow);
  ps->tagged_dep_waw = tagged_flow;
  isl_union_flow_free(flow);
}

static void derive_waw_dep_from_tagged_waw_dep(struct ppcg_scop *ps)
{
  ps->dep_waw = isl_union_map_copy(ps->tagged_dep_waw);
  ps->dep_waw = isl_union_map_factor_domain(ps->dep_waw);
}

/* Compute the WAW dependence for each intermediate write access.
 * The results are stored in ps->dep_waw.
 * A copy of the waw dependences, tagged with the reference tags 
 * is stored in ps->tagged_dep_waw.
 *
 * We first compute ps->tagged_dep_waw, i.e., the tagged WAW dependences
 * and then project out the tags. 
 */
static void compute_tagged_waw_dep(struct ppcg_scop *ps)
{
  compute_tagged_waw_dep_only(ps); 
  derive_waw_dep_from_tagged_waw_dep(ps);
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
 * 
 * Extended by AutoSA: Add analysis for WAW and RAR dependences.
 */
static void compute_dependences(struct ppcg_scop *scop)
{
	isl_union_map *may_source;
	isl_union_access_info *access;
	isl_union_flow *flow;

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
	access = isl_union_access_info_from_sink(
				isl_union_map_copy(scop->may_writes));
	//access = isl_union_access_info_set_kill(access,
	//			isl_union_map_copy(scop->must_writes));
	access = isl_union_access_info_set_kill(access,
					isl_union_map_union(isl_union_map_copy(scop->must_writes), 
					                    isl_union_map_copy(scop->must_kills)));
	access = isl_union_access_info_set_may_source(access, may_source);
	access = isl_union_access_info_set_schedule(access,
				isl_schedule_copy(scop->schedule));
	flow = isl_union_access_info_compute_flow(access);

	scop->dep_false = isl_union_flow_get_may_dependence(flow);
	scop->dep_false = isl_union_map_coalesce(scop->dep_false);
	isl_union_flow_free(flow);

	/* AutoSA Extended */
	if (scop->options->autosa->autosa) {
		compute_tagged_rar_dep(scop);
		compute_tagged_waw_dep(scop);			
	}
	/* AutoSA Extended */
}

/* Eliminate dead code from ps->domain.
 *
 * In particular, intersect both ps->domain and the domain of
 * ps->schedule with the (parts of) iteration
 * domains that are needed to produce the output or for statement
 * iterations that call functions.
 * Also intersect the range of the dataflow dependences with
 * this domain such that the removed instances will no longer
 * be considered as targets of dataflow.
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
	isl_union_pw_multi_aff *tagger;

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

	ps->domain = isl_union_set_intersect(ps->domain,
						isl_union_set_copy(live));
	ps->schedule = isl_schedule_intersect_domain(ps->schedule,
						isl_union_set_copy(live));
	ps->dep_flow = isl_union_map_intersect_range(ps->dep_flow,
						isl_union_set_copy(live));
	tagger = isl_union_pw_multi_aff_copy(ps->tagger);
	live = isl_union_set_preimage_union_pw_multi_aff(live, tagger);
	ps->tagged_dep_flow = isl_union_map_intersect_range(ps->tagged_dep_flow,
						live);
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
	isl_union_map_free(ps->must_kills);
	isl_union_map_free(ps->tagged_dep_flow);
	isl_union_map_free(ps->dep_flow);
	isl_union_map_free(ps->dep_false);
	isl_union_map_free(ps->dep_forced);
	isl_union_map_free(ps->tagged_dep_order);
	isl_union_map_free(ps->dep_order);
	isl_schedule_free(ps->schedule);
	isl_union_pw_multi_aff_free(ps->tagger);
	isl_union_map_free(ps->independence);
	isl_id_to_ast_expr_free(ps->names);
	/* AutoSA Extended */
	isl_union_map_free(ps->tagged_dep_rar);
	isl_union_map_free(ps->dep_rar);
	isl_union_map_free(ps->tagged_dep_waw);
	isl_union_map_free(ps->dep_waw);
	/* AutoSA Extended */

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

	ps->names = collect_names(scop);
	ps->options = options;
	ps->start = pet_loc_get_start(scop->loc);
	ps->end = pet_loc_get_end(scop->loc);
	ps->context = isl_set_copy(scop->context);
	ps->context = set_intersect_str(ps->context, options->ctx);
	if (options->non_negative_parameters) {
		isl_space *space = isl_set_get_space(ps->context);
		isl_set *nn = isl_set_nat_universe(space);
		ps->context = isl_set_intersect(ps->context, nn);
	}
	ps->domain = collect_non_kill_domains(scop);
	ps->call = collect_call_domains(scop);
	ps->tagged_reads = pet_scop_get_tagged_may_reads(scop);
	ps->reads = pet_scop_get_may_reads(scop);
	ps->tagged_may_writes = pet_scop_get_tagged_may_writes(scop);
	ps->may_writes = pet_scop_get_may_writes(scop);
	ps->tagged_must_writes = pet_scop_get_tagged_must_writes(scop);
	ps->must_writes = pet_scop_get_must_writes(scop);
	ps->tagged_must_kills = pet_scop_get_tagged_must_kills(scop);
	ps->must_kills = pet_scop_get_must_kills(scop);
	ps->schedule = isl_schedule_copy(scop->schedule);
	ps->pet = scop;
	ps->independence = isl_union_map_empty(isl_set_get_space(ps->context));
	for (i = 0; i < scop->n_independence; ++i)
		ps->independence = isl_union_map_union(ps->independence,
			isl_union_map_copy(scop->independences[i]->filter));

	compute_tagger(ps);
	compute_dependences(ps);
	eliminate_dead_code(ps);

	if (!ps->context || !ps->domain || !ps->call || !ps->reads ||
	    !ps->may_writes || !ps->must_writes || !ps->tagged_must_kills ||
	    !ps->must_kills || !ps->schedule || !ps->independence || !ps->names)
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

/* Should we print the original code?
 * That is, does "scop" involve any data dependent conditions or
 * nested expressions that cannot be handled by pet_stmt_build_ast_exprs?
 */
static int print_original(struct pet_scop *scop, struct ppcg_options *options)
{
	if (!pet_scop_can_build_ast_exprs(scop)) {
		if (options->debug->verbose)
			fprintf(stdout, "Printing original code because "
				"some index expressions cannot currently "
				"be printed\n");
		return 1;
	}

	if (pet_scop_has_data_dependent_conditions(scop)) {
		if (options->debug->verbose)
			fprintf(stdout, "Printing original code because "
				"input involves data dependent conditions\n");
		return 1;
	}

	return 0;
}

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

	if (print_original(scop, data->options)) {
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
	struct options *options;//
	options = isl_ctx_peek_options(ctx, &options_args);
	if (!options)
		isl_die(ctx, isl_error_internal,
			"unable to find options", return -1);//
	if (options->ppcg->openmp &&
	    !isl_options_get_ast_build_atomic_upper_bound(ctx))
		isl_die(ctx, isl_error_invalid,
			"OpenMP requires atomic bounds", return -1);//
	return 0;
}

//int main(int argc, char **argv)
//{
//	int r;
//	isl_ctx *ctx;
//	struct options *options;
//
//	options = options_new_with_defaults();
//	assert(options);
//
//	ctx = isl_ctx_alloc_with_options(&options_args, options);
//	ppcg_options_set_target_defaults(options->ppcg);
//	isl_options_set_ast_build_detect_min_max(ctx, 1);
//	isl_options_set_ast_print_macro_once(ctx, 1);
//	isl_options_set_schedule_whole_component(ctx, 0);
//	isl_options_set_schedule_maximize_band_depth(ctx, 1);
//	isl_options_set_schedule_maximize_coincidence(ctx, 1);
//	pet_options_set_encapsulate_dynamic_control(ctx, 1);
//	argc = options_parse(options, argc, argv, ISL_ARG_ALL);
//
//	if (check_options(ctx) < 0)
//		r = EXIT_FAILURE;
//	else if (options->ppcg->target == PPCG_TARGET_CUDA)
//		r = generate_cuda(ctx, options->ppcg, options->input);
//	else if (options->ppcg->target == PPCG_TARGET_OPENCL)
//		r = generate_opencl(ctx, options->ppcg, options->input,
//				options->output);
//	else
//		r = generate_cpu(ctx, options->ppcg, options->input,
//				options->output);
//
//	isl_ctx_free(ctx);
//
//	return r;
//}

int autosa_main_wrap(int argc, char **argv)
{
	int r;
	isl_ctx *ctx;
	struct options *options;

	options = options_new_with_defaults();
	assert(options);

	ctx = isl_ctx_alloc_with_options(&options_args, options);
	ppcg_options_set_target_defaults(options->ppcg);
	isl_options_set_ast_build_detect_min_max(ctx, 1);
	isl_options_set_ast_print_macro_once(ctx, 1);
	isl_options_set_schedule_whole_component(ctx, 0);
	isl_options_set_schedule_maximize_band_depth(ctx, 1);
	isl_options_set_schedule_maximize_coincidence(ctx, 1);
	pet_options_set_encapsulate_dynamic_control(ctx, 1);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	if (check_options(ctx) < 0)
		r = EXIT_FAILURE;
	//else if (options->ppcg->target == PPCG_TARGET_CUDA)
	//	r = generate_cuda(ctx, options->ppcg, options->input);
	//else if (options->ppcg->target == PPCG_TARGET_OPENCL)
	//	r = generate_opencl(ctx, options->ppcg, options->input,
	//			options->output);
	//else if (options->ppcg->target == PPCG_TARGET_C)
	//	r = generate_cpu(ctx, options->ppcg, options->input,
	//			options->output);
	else if (options->ppcg->target == AUTOSA_TARGET_XILINX_HLS_C) 
	  r = generate_autosa_xilinx_hls_c(ctx, options->ppcg, options->input);
	else if (options->ppcg->target == AUTOSA_TARGET_INTEL_OPENCL)
	  r = generate_autosa_intel_opencl(ctx, options->ppcg, options->input);
	else if (options->ppcg->target == AUTOSA_TARGET_CATAPULT_HLS_C)
		r = generate_autosa_catapult_hls_c(ctx, options->ppcg, options->input);
	else if (options->ppcg->target == AUTOSA_TARGET_TAPA_CPP)
	  r = generate_autosa_tapa_cpp(ctx, options->ppcg, options->input);
//	else if (options->ppcg->target == AUTOSA_TARGET_T2S)
//	  r = generate_autosa_t2s(ctx, options->ppcg, options->input, 
//				options->output); // TODO: To fix
//	else if (options->ppcg->target == AUTOSA_TARGET_C)
//	  r = generate_autosa_cpu(ctx, options->ppcg, options->input); // TODO: to fix

	isl_ctx_free(ctx);

	return r;
}
