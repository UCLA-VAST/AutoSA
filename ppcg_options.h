#ifndef PPCG_OPTIONS_H
#define PPCG_OPTIONS_H

#include <isl/arg.h>

struct ppcg_options {
	int scale_tile_loops;
	int wrap;

	char *type;
	char *ctx;

	int tile_size;
};

ISL_ARG_DECL(ppcg_options, struct ppcg_options, ppcg_options_arg)

extern struct isl_arg ppcg_options_arg[];

#endif
