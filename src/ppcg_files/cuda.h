#ifndef _CUDA_H
#define _CUDA_H

#include "ppcg_options.h"
#include "ppcg.h"

#ifdef __cplusplus
extern "C"
{
#endif

	int generate_cuda(isl_ctx *ctx, struct ppcg_options *options,
										const char *input);

#ifdef __cplusplus
}
#endif

#endif
