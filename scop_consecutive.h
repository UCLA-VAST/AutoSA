#ifndef _SCOP_CONSECUTIVE_H
#define _SCOP_CONSECUTIVE_H

#include "ppcg.h"
#include "consecutivity.h"

__isl_give ppcg_consecutive *ppcg_scop_extract_consecutive(
	struct ppcg_scop *scop);
__isl_give isl_schedule_constraints *ppcg_add_consecutivity_constraints(
	__isl_take isl_schedule_constraints *sc, struct ppcg_scop *scop);

#endif
