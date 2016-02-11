/*
 * Copyright 2012-2013 Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <isl/aff.h>
#include <isl/ast_build.h>

#include "print.h"
#include "util.h"

__isl_give isl_printer *ppcg_start_block(__isl_take isl_printer *p)
{
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "{");
	p = isl_printer_end_line(p);
	p = isl_printer_indent(p, 2);
	return p;
}

__isl_give isl_printer *ppcg_end_block(__isl_take isl_printer *p)
{
	p = isl_printer_indent(p, -2);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "}");
	p = isl_printer_end_line(p);
	return p;
}

static int print_macro(enum isl_ast_op_type type, void *user)
{
	isl_printer **p = user;

	if (type == isl_ast_op_fdiv_q)
		return 0;

	*p = isl_ast_op_type_print_macro(type, *p);

	return 0;
}

/* Print the required macros for "node", except one for floord.
 * The caller is assumed to have printed a macro for floord already
 * as it may also appear in the declarations and the statements.
 */
__isl_give isl_printer *ppcg_print_macros(__isl_take isl_printer *p,
	__isl_keep isl_ast_node *node)
{
	if (isl_ast_node_foreach_ast_op_type(node, &print_macro, &p) < 0)
		return isl_printer_free(p);
	return p;
}

/* Names used for the macros that may appear in a printed isl AST.
 */
const char *ppcg_min = "ppcg_min";
const char *ppcg_max = "ppcg_max";
const char *ppcg_fdiv_q = "ppcg_fdiv_q";

/* Set the names of the macros that may appear in a printed isl AST.
 */
__isl_give isl_printer *ppcg_set_macro_names(__isl_take isl_printer *p)
{
	p = isl_ast_op_type_set_print_name(p, isl_ast_op_min, ppcg_min);
	p = isl_ast_op_type_set_print_name(p, isl_ast_op_max, ppcg_max);
	p = isl_ast_op_type_set_print_name(p, isl_ast_op_fdiv_q, ppcg_fdiv_q);

	return p;
}

/* Print a declaration for array "array" to "p", using "build"
 * to simplify any size expressions.
 *
 * The size is computed from the extent of the array and is
 * subsequently converted to an "access expression" by "build".
 */
__isl_give isl_printer *ppcg_print_declaration(__isl_take isl_printer *p,
	struct pet_array *array, __isl_keep isl_ast_build *build)
{
	isl_multi_pw_aff *size;
	isl_ast_expr *expr;

	if (!array)
		return isl_printer_free(p);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, array->element_type);
	p = isl_printer_print_str(p, " ");
	size = ppcg_size_from_extent(isl_set_copy(array->extent));
	expr = isl_ast_build_access_from_multi_pw_aff(build, size);
	p = isl_printer_print_ast_expr(p, expr);
	isl_ast_expr_free(expr);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

/* Print declarations for the arrays in "scop" that are declared
 * and that are exposed (if exposed == 1) or not exposed (if exposed == 0).
 */
static __isl_give isl_printer *print_declarations(__isl_take isl_printer *p,
	struct ppcg_scop *scop, int exposed)
{
	int i;
	isl_ast_build *build;

	if (!scop)
		return isl_printer_free(p);

	build = isl_ast_build_from_context(isl_set_copy(scop->context));
	for (i = 0; i < scop->pet->n_array; ++i) {
		struct pet_array *array = scop->pet->arrays[i];

		if (!array->declared)
			continue;
		if (array->exposed != exposed)
			continue;

		p = ppcg_print_declaration(p, array, build);
	}
	isl_ast_build_free(build);

	return p;
}

/* Print declarations for the arrays in "scop" that are declared
 * and exposed to the code after the scop.
 */
__isl_give isl_printer *ppcg_print_exposed_declarations(
	__isl_take isl_printer *p, struct ppcg_scop *scop)
{
	return print_declarations(p, scop, 1);
}

/* Print declarations for the arrays in "scop" that are declared,
 * but not exposed to the code after the scop.
 */
__isl_give isl_printer *ppcg_print_hidden_declarations(
	__isl_take isl_printer *p, struct ppcg_scop *scop)
{
	return print_declarations(p, scop, 0);
}
