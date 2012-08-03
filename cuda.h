#ifndef _CUDA_H
#define _CUDA_H

#include <pet.h>
#include "ppcg_options.h"

int generate_cuda(isl_ctx *ctx, struct pet_scop *scop,
	struct ppcg_options *options, const char *input);

#endif
