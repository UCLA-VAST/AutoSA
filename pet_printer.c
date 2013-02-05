/*
 * Copyright 2010-2011 INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include "pet_printer.h"
#include "pet.h"

static __isl_take isl_printer *print_pet_expr_help(__isl_take isl_printer *p,
	struct pet_expr *expr, int outer,
	__isl_take isl_printer *(*print_access_fn)(__isl_take isl_printer *p,
		struct pet_expr *expr, void *usr), void *usr)
{
	int i;

	switch (expr->type) {
	case pet_expr_double:
		p = isl_printer_print_str(p, expr->d.s);
		break;
	case pet_expr_access:
		p = print_access_fn(p, expr, usr);
		break;
	case pet_expr_unary:
		if (!outer)
			p = isl_printer_print_str(p, "(");
		p = isl_printer_print_str(p, " ");
		p = isl_printer_print_str(p, pet_op_str(expr->op));
		p = isl_printer_print_str(p, " ");
		p = print_pet_expr_help(p, expr->args[pet_un_arg], 0,
			       print_access_fn, usr);
		if (!outer)
			p = isl_printer_print_str(p, ")");
		break;
	case pet_expr_binary:
		if (!outer)
			p = isl_printer_print_str(p, "(");
		p = print_pet_expr_help(p, expr->args[pet_bin_lhs], 0,
			       print_access_fn, usr);
		p = isl_printer_print_str(p, " ");
		p = isl_printer_print_str(p, pet_op_str(expr->op));
		p = isl_printer_print_str(p, " ");
		p = print_pet_expr_help(p, expr->args[pet_bin_rhs], 0,
			       print_access_fn, usr);
		if (!outer)
			p = isl_printer_print_str(p, ")");
		break;
	case pet_expr_ternary:
		if (!outer)
			p = isl_printer_print_str(p, "(");
		p = print_pet_expr_help(p, expr->args[pet_ter_cond], 0,
			       print_access_fn, usr);
		p = isl_printer_print_str(p, " ? ");
		p = print_pet_expr_help(p, expr->args[pet_ter_true], 0,
			       print_access_fn, usr);
		p = isl_printer_print_str(p, " : ");
		p = print_pet_expr_help(p, expr->args[pet_ter_false], 0,
			       print_access_fn, usr);
		if (!outer)
			p = isl_printer_print_str(p, ")");
		break;
	case pet_expr_call:
		p = isl_printer_print_str(p, expr->name);
		p = isl_printer_print_str(p, "(");
		for (i = 0; i < expr->n_arg; ++i) {
			if (i)
				p = isl_printer_print_str(p, ", ");
			p = print_pet_expr_help(p, expr->args[i], 1,
				       print_access_fn, usr);
		}
		p = isl_printer_print_str(p, ")");
		break;
	case pet_expr_cast:
		if (!outer)
			p = isl_printer_print_str(p, "(");
		p = isl_printer_print_str(p, "(");
		p = isl_printer_print_str(p, expr->type_name);
		p = isl_printer_print_str(p, ") ");
		p = print_pet_expr_help(p, expr->args[0], 0,
			       print_access_fn, usr);
		if (!outer)
			p = isl_printer_print_str(p, ")");
		break;
	}

	return p;
}

__isl_give isl_printer *print_pet_expr(__isl_take isl_printer *p,
	struct pet_expr *expr,
	__isl_give isl_printer *(*print_access)(__isl_take isl_printer *p,
		struct pet_expr *expr, void *usr), void *usr)
{
	return print_pet_expr_help(p, expr, 1, print_access, usr);
}
