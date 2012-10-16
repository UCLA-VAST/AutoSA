#ifndef PRINT_H
#define PRINT_H

#include "ppcg.h"

__isl_give isl_printer *ppcg_start_block(__isl_take isl_printer *p);
__isl_give isl_printer *ppcg_end_block(__isl_take isl_printer *p);

__isl_give isl_printer *ppcg_print_exposed_declarations(
	__isl_take isl_printer *p, struct ppcg_scop *scop);
__isl_give isl_printer *ppcg_print_hidden_declarations(
	__isl_take isl_printer *p, struct ppcg_scop *scop);

#endif
