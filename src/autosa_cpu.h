#ifndef _AUTOSA_CPU_H
#define _AUTOSA_CPU_H

#include <isl/ctx.h>

#include "ppcg.h"

struct ppcg_options;

int generate_autosa_cpu(isl_ctx *ctx, struct ppcg_options *options,
												const char *input);

#endif