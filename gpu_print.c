/*
 * Copyright 2012      Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <isl/aff.h>

#include "gpu_print.h"
#include "schedule.h"

static int print_macro(enum isl_ast_op_type type, void *user)
{
	isl_printer **p = user;

	if (type == isl_ast_op_fdiv_q)
		return 0;

	*p = isl_ast_op_type_print_macro(type, *p);

	return 0;
}

/* Print the required macros for "node", including one for floord.
 * We always print a macro for floord as it may also appear in the statements.
 */
__isl_give isl_printer *gpu_print_macros(__isl_take isl_printer *p,
	__isl_keep isl_ast_node *node)
{
	p = isl_ast_op_type_print_macro(isl_ast_op_fdiv_q, p);
	if (isl_ast_node_foreach_ast_op_type(node, &print_macro, &p) < 0)
		return isl_printer_free(p);
	return p;
}

/* Print an expression for the size of "array" in bytes.
 */
__isl_give isl_printer *gpu_array_info_print_size(__isl_take isl_printer *prn,
	struct gpu_array_info *array)
{
	int i;

	for (i = 0; i < array->n_index; ++i) {
		prn = isl_printer_print_str(prn, "(");
		prn = isl_printer_print_pw_aff(prn, array->bound[i]);
		prn = isl_printer_print_str(prn, ") * ");
	}
	prn = isl_printer_print_str(prn, "sizeof(");
	prn = isl_printer_print_str(prn, array->type);
	prn = isl_printer_print_str(prn, ")");

	return prn;
}

/* Print an access to the element in the private/shared memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->local_index as a "call" to the array.
 */
static __isl_give isl_printer *stmt_print_local_index(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	int i;
	isl_ast_expr *expr;
	struct gpu_array_info *array = stmt->u.c.array;

	expr = isl_ast_expr_get_op_arg(stmt->u.c.local_index, 0);
	p = isl_printer_print_ast_expr(p, expr);
	isl_ast_expr_free(expr);

	for (i = 0; i < array->n_index; ++i) {
		expr = isl_ast_expr_get_op_arg(stmt->u.c.local_index, 1 + i);

		p = isl_printer_print_str(p, "[");
		p = isl_printer_print_ast_expr(p, expr);
		p = isl_printer_print_str(p, "]");

		isl_ast_expr_free(expr);
	}

	return p;
}

/* Print an access to the element in the global memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->index as a "call" to the array.
 *
 * The copy in global memory has been linearized, so we need to take
 * the array size into account.
 */
static __isl_give isl_printer *stmt_print_global_index(
	__isl_take isl_printer *p, struct ppcg_kernel_stmt *stmt)
{
	int i;
	struct gpu_array_info *array = stmt->u.c.array;
	isl_pw_aff_list *bound = stmt->u.c.local_array->bound;

	if (gpu_array_is_scalar(array)) {
		if (!array->read_only)
			p = isl_printer_print_str(p, "*");
		p = isl_printer_print_str(p, array->name);
		return p;
	}

	p = isl_printer_print_str(p, array->name);
	p = isl_printer_print_str(p, "[");
	for (i = 0; i + 1 < array->n_index; ++i)
		p = isl_printer_print_str(p, "(");
	for (i = 0; i < array->n_index; ++i) {
		isl_ast_expr *expr;
		expr = isl_ast_expr_get_op_arg(stmt->u.c.index, 1 + i);
		if (i) {
			isl_pw_aff *bound_i;
			bound_i = isl_pw_aff_list_get_pw_aff(bound, i);
			p = isl_printer_print_str(p, ") * (");
			p = isl_printer_print_pw_aff(p, bound_i);
			p = isl_printer_print_str(p, ") + (");
			isl_pw_aff_free(bound_i);
		}
		p = isl_printer_print_ast_expr(p, expr);
		if (i)
			p = isl_printer_print_str(p, ")");
		isl_ast_expr_free(expr);
	}
	p = isl_printer_print_str(p, "]");

	return p;
}

/* Print a copy statement.
 *
 * A read copy statement is printed as
 *
 *	local = global;
 *
 * while a write copy statement is printed as
 *
 *	global = local;
 */
__isl_give isl_printer *ppcg_kernel_print_copy(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	p = isl_printer_start_line(p);
	if (stmt->u.c.read) {
		p = stmt_print_local_index(p, stmt);
		p = isl_printer_print_str(p, " = ");
		p = stmt_print_global_index(p, stmt);
	} else {
		p = stmt_print_global_index(p, stmt);
		p = isl_printer_print_str(p, " = ");
		p = stmt_print_local_index(p, stmt);
	}
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

__isl_give isl_printer *ppcg_kernel_print_domain(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	return pet_stmt_print_body(stmt->u.d.stmt->stmt, p, stmt->u.d.ref2expr);
}
