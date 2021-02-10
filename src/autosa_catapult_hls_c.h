#ifndef _AUTOSA_CATAPULT_HLS_C_H
#define _AUTOSA_CATAPULT_HLS_C_H

#include <pet.h>
#include "ppcg_options.h"
#include "ppcg.h"

#ifdef __cplusplus
extern "C"
{
#endif

int generate_autosa_catapult_hls_c(isl_ctx *ctx, struct ppcg_options *options,
																	 const char *input);

#ifdef __cplusplus
}
#endif

#endif