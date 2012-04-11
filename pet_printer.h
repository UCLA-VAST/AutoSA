#ifndef _PET_PRINTER_H
#define _PET_PRINTER_H

#include <isl/printer.h>

struct pet_expr;

__isl_give isl_printer *print_pet_expr(__isl_take isl_printer *p,
	struct pet_expr *expr,
	__isl_give isl_printer *(*print_access)(__isl_take isl_printer *p,
		struct pet_expr *expr, void *usr), void *usr);

#endif
