#ifndef _CPU_H
#define _CPU_H

#include <isl/ctx.h>

struct pet_scop;
struct ppcg_options;

int generate_cpu(isl_ctx *ctx, struct pet_scop *scop,
	struct ppcg_options *options, const char *input);

#endif
