#ifndef PET_SCOP_YAML_H
#define PET_SCOP_YAML_H

#include <stdio.h>
#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

int pet_scop_emit(FILE *out, struct pet_scop *scop);
struct pet_scop *pet_scop_parse(isl_ctx *ctx, FILE *in);

#if defined(__cplusplus)
}
#endif

#endif
