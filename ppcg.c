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
#include <clan/clan.h>
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
	isl_ctx *ctx;
	struct options *options;
	clan_options_p clan_options;
	scoplib_scop_p scop;
	FILE *input;

	options = options_new_with_defaults();
	assert(options);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	ctx = isl_ctx_alloc_with_options(options_arg, options);

	clan_options = clan_options_malloc();

	input = fopen(options->input, "r");
	if (!input) {
		fprintf(stderr, "unable to open input file '%s'\n",
				options->input);
		return -1;
	}
	scop = clan_scop_extract(input, clan_options);
	clan_options_free(clan_options);

	return cuda_scop(ctx, scop, options->ppcg, options->input);
}
