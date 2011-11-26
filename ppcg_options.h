#ifndef PPCG_OPTIONS_H
#define PPCG_OPTIONS_H

#include <isl/arg.h>

struct ppcg_options {
	int scale_tile_loops;
	int wrap;

	char *ctx;
	char *sizes;

	int tile_size;

	/* Take advantage of private memory. */
	int use_private_memory;

	/* Take advantage of shared memory. */
	int use_shared_memory;

	/* Maximal amount of shared memory. */
	int max_shared_memory;
};

ISL_ARG_DECL(ppcg_options, struct ppcg_options, ppcg_options_args)

#endif
