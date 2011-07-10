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
#include <pet.h>
#include "ppcg_options.h"
#include "cuda.h"

struct options {
	struct isl_options *isl;
	struct ppcg_options *ppcg;
	char *input;
};

struct isl_arg options_arg[] = {
ISL_ARG_CHILD(struct options, isl, "isl", isl_options_arg, "isl options")
ISL_ARG_CHILD(struct options, ppcg, NULL, ppcg_options_arg, NULL)
ISL_ARG_ARG(struct options, input, "input", NULL)
ISL_ARG_END
};

ISL_ARG_DEF(options, struct options, options_arg)

int main(int argc, char **argv)
{
	int r;
	isl_ctx *ctx;
	struct options *options;
	FILE *input;
	struct pet_scop *scop;

	options = options_new_with_defaults();
	assert(options);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	ctx = isl_ctx_alloc_with_options(options_arg, options);

	scop = pet_scop_extract_from_C_source(ctx, options->input, NULL, 0);
	r = cuda_pet(ctx, scop, options->ppcg, options->input);
	pet_scop_free(scop);

	isl_ctx_free(ctx);

	return r;
}
