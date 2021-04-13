#ifndef _OPENCL_H
#define _OPENCL_H

#include <pet.h>
#include "ppcg_options.h"
#include "ppcg.h"

#ifdef __cplusplus
extern "C"
{
#endif

	int generate_opencl(isl_ctx *ctx, struct ppcg_options *options,
											const char *input, const char *output);

#ifdef __cplusplus
}
#endif

#endif
