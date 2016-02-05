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

/* Given a multi affine expression "mpa" without domain, modify it to have
 * the schedule space of "build" as domain.
 *
 * If the schedule space of "build" is a parameter space, then nothing
 * needs to be done.
 * Otherwise, "mpa" is first given a 0D domain and then it is combined
 * with a mapping from the schedule space of "build" to the same 0D domain.
 */
__isl_give isl_multi_pw_aff *ppcg_attach_multi_pw_aff(
	__isl_take isl_multi_pw_aff *mpa, __isl_keep isl_ast_build *build)
{
	isl_bool params;
	isl_space *space;
	isl_multi_aff *ma;

	space = isl_ast_build_get_schedule_space(build);
	params = isl_space_is_params(space);
	if (params < 0 || params) {
		isl_space_free(space);
		if (params < 0)
			return isl_multi_pw_aff_free(mpa);
		return mpa;
	}
	space = isl_space_from_domain(space);
	ma = isl_multi_aff_zero(space);
	mpa = isl_multi_pw_aff_from_range(mpa);
	mpa = isl_multi_pw_aff_pullback_multi_aff(mpa, ma);

	return mpa;
}

/* Build an access AST expression from "size" using "build".
 * "size" does not have a domain, but "build" may have a proper schedule space.
 * First modify "size" to have that schedule space as domain.
 */
__isl_give isl_ast_expr *ppcg_build_size_expr(__isl_take isl_multi_pw_aff *size,
	__isl_keep isl_ast_build *build)
{
	size = ppcg_attach_multi_pw_aff(size, build);
	return isl_ast_build_access_from_multi_pw_aff(build, size);
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
