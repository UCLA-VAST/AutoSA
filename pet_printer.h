#ifndef _PET_PRINTER_H
#define _PET_PRINTER_H

#include <stdio.h>

struct pet_expr;

void print_pet_expr(FILE *out, struct pet_expr *expr,
	void (*print_access_fn)(struct pet_expr *expr, void *usr), void *usr);

#endif
