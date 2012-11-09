#ifndef PRINT_H
#define PRINT_H

#include "ppcg.h"

__isl_give isl_printer *ppcg_start_block(__isl_take isl_printer *p);
__isl_give isl_printer *ppcg_end_block(__isl_take isl_printer *p);

#endif
