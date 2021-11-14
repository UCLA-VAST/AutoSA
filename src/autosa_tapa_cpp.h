#ifndef _AUTOSA_TAPA_CPP_H
#define _AUTOSA_TAPA_CPP_H

#include <pet.h>
#include "ppcg_options.h"
#include "ppcg.h"

#ifdef __cplusplus
extern "C"
{
#endif

int generate_autosa_tapa_cpp(isl_ctx *ctx, struct ppcg_options *options,
        const char *input);

#ifdef __cplusplus
}
#endif

#endif
