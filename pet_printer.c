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

static void print_pet_expr_help(FILE *out, struct pet_expr *expr, int outer,
	void (*print_access_fn)(struct pet_expr *expr, void *usr), void *usr)
{
	int i;

	switch (expr->type) {
	case pet_expr_double:
		fprintf(out, "%g", expr->d);
		break;
	case pet_expr_access:
		print_access_fn(expr, usr);
		break;
	case pet_expr_unary:
		if (!outer)
			fprintf(out, "(");
		fprintf(out, " %s ", pet_op_str(expr->op));
		print_pet_expr_help(out, expr->args[pet_un_arg], 0,
			       print_access_fn, usr);
		if (!outer)
			fprintf(out, ")");
		break;
	case pet_expr_binary:
		if (!outer)
			fprintf(out, "(");
		print_pet_expr_help(out, expr->args[pet_bin_lhs], 0,
			       print_access_fn, usr);
		fprintf(out, " %s ", pet_op_str(expr->op));
		print_pet_expr_help(out, expr->args[pet_bin_rhs], 0,
			       print_access_fn, usr);
		if (!outer)
			fprintf(out, ")");
		break;
	case pet_expr_ternary:
		if (!outer)
			fprintf(out, "(");
		print_pet_expr_help(out, expr->args[pet_ter_cond], 0,
			       print_access_fn, usr);
		fprintf(out, " ? ");
		print_pet_expr_help(out, expr->args[pet_ter_true], 0,
			       print_access_fn, usr);
		fprintf(out, " : ");
		print_pet_expr_help(out, expr->args[pet_ter_false], 0,
			       print_access_fn, usr);
		if (!outer)
			fprintf(out, ")");
		break;
	case pet_expr_call:
		fprintf(out, "%s(", expr->name);
		for (i = 0; i < expr->n_arg; ++i) {
			if (i)
				fprintf(out, ", ");
			print_pet_expr_help(out, expr->args[i], 1,
				       print_access_fn, usr);
		}
		fprintf(out, ")");
	}
}

void print_pet_expr(FILE *out, struct pet_expr *expr,
	void(*print_access)(struct pet_expr *expr, void *usr), void *usr)
{
	print_pet_expr_help(out, expr, 1, print_access, usr);
}
