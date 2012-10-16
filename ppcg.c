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
#include <isl/options.h>
#include <isl/schedule.h>
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
ISL_ARG_ARG(struct options, input, "input", NULL)
ISL_ARG_VERSION(print_version)
ISL_ARGS_END

ISL_ARG_DEF(options, struct options, options_args)

/* Extract a ppcg_scop from a pet_scop.
 *
 * The constructed ppcg_scop refers to elements from the pet_scop
 * so the pet_scop should not be freed before the ppcg_scop.
 */
static struct ppcg_scop *ppcg_scop_from_pet_scop(struct pet_scop *scop)
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
	ps->domain = pet_scop_collect_domains(scop);
	ps->reads = pet_scop_collect_reads(scop);
	ps->writes = pet_scop_collect_writes(scop);
	ps->schedule = pet_scop_collect_schedule(scop);
	ps->n_array = scop->n_array;
	ps->arrays = scop->arrays;
	ps->n_stmt = scop->n_stmt;
	ps->stmts = scop->stmts;

	return ps;
}

static void ppcg_scop_free(struct ppcg_scop *ps)
{
	if (!ps)
		return;

	isl_set_free(ps->context);
	isl_union_set_free(ps->domain);
	isl_union_map_free(ps->reads);
	isl_union_map_free(ps->writes);
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

	scop = pet_scop_extract_from_C_source(ctx, options->input, NULL);
	scop = pet_scop_align_params(scop);
	ps = ppcg_scop_from_pet_scop(scop);

	if (options->ppcg->target == PPCG_TARGET_CUDA)
		r = generate_cuda(ctx, ps, options->ppcg, options->input);
	else
		r = generate_cpu(ctx, ps, options->ppcg, options->input);

	ppcg_scop_free(ps);
	pet_scop_free(scop);

	isl_ctx_free(ctx);

	return r;
}
