#ifndef _AUTOSA_INTEL_OPENCL_H
#define _AUTOSA_INTEL_OPENCL_H

#include <pet.h>
#include "ppcg_options.h"
#include "ppcg.h"

#ifdef __cplusplus
extern "C" {
#endif

int generate_autosa_intel_opencl(isl_ctx *ctx, struct ppcg_options *options,
	const char *input);

#ifdef __cplusplus
}
#endif

#endif