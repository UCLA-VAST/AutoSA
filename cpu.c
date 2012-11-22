/*
 * Copyright 2012 INRIA Paris-Rocquencourt
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Tobias Grosser, INRIA Paris-Rocquencourt,
 * Domaine de Voluceau, Rocquenqourt, B.P. 105,
 * 78153 Le Chesnay Cedex France
 */

#include <limits.h>
#include <stdio.h>

#include <isl/aff.h>
#include <isl/ctx.h>
#include <isl/map.h>
#include <isl/ast_build.h>
#include <pet.h>

#include "ppcg.h"
#include "cpu.h"
#include "pet_printer.h"
#include "print.h"
#include "rewrite.h"

/* Representation of a statement inside a generated AST.
 *
 * "stmt" refers to the original statement.
 * "n_access" is the number of accesses in the statement.
 * "access" is the list of accesses transformed to refer to the iterators
 * in the generated AST.
 */
struct ppcg_stmt {
	struct pet_stmt *stmt;

	int n_access;
	isl_ast_expr_list **access;
};

static void ppcg_stmt_free(void *user)
{
	struct ppcg_stmt *stmt = user;
	int i;

	if (!stmt)
		return;

	for (i = 0; i < stmt->n_access; ++i)
		isl_ast_expr_list_free(stmt->access[i]);

	free(stmt->access);
	free(stmt);
}

/* Derive the output file name from the input file name.
 * 'input' is the entire path of the input file. The output
 * is the file name plus the additional extension.
 *
 * We will basically replace everything after the last point
 * with '.ppcg.c'. This means file.c becomes file.ppcg.c
 */
static FILE *get_output_file(const char *input, const char *output)
{
	char name[PATH_MAX];
	const char *base;
	const char *ext;
	const char ppcg_marker[] = ".ppcg";
	int len;

	base = strrchr(input, '/');
	if (base)
		base++;
	else
		base = input;
	ext = strrchr(base, '.');
	len = ext ? ext - base : strlen(base);

	memcpy(name, base, len);
	strcpy(name + len, ppcg_marker);
	strcpy(name + len + sizeof(ppcg_marker) - 1, ext);

	if (!output)
		output = name;

	return fopen(output, "w");
}

/* Print a memory access 'access' to the printer 'p'.
 *
 * "expr" refers to the original access.
 * "access" is the list of index expressions transformed to refer
 * to the iterators of the generated AST.
 *
 * In case the original access is unnamed (and presumably single-dimensional),
 * we assume this is not a memory access, but just an expression.
 */
static __isl_give isl_printer *print_access(__isl_take isl_printer *p,
	struct pet_expr *expr, __isl_keep isl_ast_expr_list *access)
{
	int i;
	const char *name;
	unsigned n_index;

	n_index = isl_ast_expr_list_n_ast_expr(access);
	name = isl_map_get_tuple_name(expr->acc.access, isl_dim_out);

	if (name == NULL) {
		isl_ast_expr *index;
		index = isl_ast_expr_list_get_ast_expr(access, 0);
		p = isl_printer_print_str(p, "(");
		p = isl_printer_print_ast_expr(p, index);
		p = isl_printer_print_str(p, ")");
		isl_ast_expr_free(index);
		return p;
	}

	p = isl_printer_print_str(p, name);

	for (i = 0; i < n_index; ++i) {
		isl_ast_expr *index;

		index = isl_ast_expr_list_get_ast_expr(access, i);

		p = isl_printer_print_str(p, "[");
		p = isl_printer_print_ast_expr(p, index);
		p = isl_printer_print_str(p, "]");
		isl_ast_expr_free(index);
	}

	return p;
}

/* Find the element in scop->stmts that has the given "id".
 */
static struct pet_stmt *find_stmt(struct ppcg_scop *scop, __isl_keep isl_id *id)
{
	int i;

	for (i = 0; i < scop->n_stmt; ++i) {
		struct pet_stmt *stmt = scop->stmts[i];
		isl_id *id_i;

		id_i = isl_set_get_tuple_id(stmt->domain);
		isl_id_free(id_i);

		if (id_i == id)
			return stmt;
	}

	isl_die(isl_id_get_ctx(id), isl_error_internal,
		"statement not found", return NULL);
}

/* To print the transformed accesses we walk the list of transformed accesses
 * simultaneously with the pet printer. This means that whenever
 * the pet printer prints a pet access expression we have
 * the corresponding transformed access available for printing.
 */
static __isl_give isl_printer *print_access_expr(__isl_take isl_printer *p,
	struct pet_expr *expr, void *user)
{
	isl_ast_expr_list ***access = user;

	p = print_access(p, expr, **access);
	(*access)++;

	return p;
}

/* Print a user statement in the generated AST.
 * The ppcg_stmt has been attached to the node in at_each_domain.
 */
static __isl_give isl_printer *print_user(__isl_take isl_printer *p,
	__isl_take isl_ast_print_options *print_options,
	__isl_keep isl_ast_node *node, void *user)
{
	struct ppcg_stmt *stmt;
	isl_ast_expr_list **access;
	isl_id *id;

	id = isl_ast_node_get_annotation(node);
	stmt = isl_id_get_user(id);
	isl_id_free(id);

	access = stmt->access;

	p = isl_printer_start_line(p);
	p = print_pet_expr(p, stmt->stmt->body, &print_access_expr, &access);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	isl_ast_print_options_free(print_options);

	return p;
}

/* Call "fn" on each access expression in "expr".
 */
static int foreach_access_expr(struct pet_expr *expr,
	int (*fn)(struct pet_expr *expr, void *user), void *user)
{
	int i;

	if (!expr)
		return -1;

	if (expr->type == pet_expr_access)
		return fn(expr, user);

	for (i = 0; i < expr->n_arg; ++i)
		if (foreach_access_expr(expr->args[i], fn, user) < 0)
			return -1;

	return 0;
}

static int inc_n_access(struct pet_expr *expr, void *user)
{
	struct ppcg_stmt *stmt = user;
	stmt->n_access++;
	return 0;
}

/* Internal data for add_access.
 *
 * "stmt" is the statement to which an access needs to be added.
 * "build" is the current AST build.
 * "map" maps the AST loop iterators to the iteration domain of the statement.
 */
struct ppcg_add_access_data {
	struct ppcg_stmt *stmt;
	isl_ast_build *build;
	isl_map *map;
};

/* Given an access expression, add it to data->stmt after
 * transforming it to refer to the AST loop iterators.
 */
static int add_access(struct pet_expr *expr, void *user)
{
	int i, n;
	isl_ctx *ctx;
	isl_map *access;
	isl_pw_multi_aff *pma;
	struct ppcg_add_access_data *data = user;
	isl_ast_expr_list *index;

	ctx = isl_map_get_ctx(expr->acc.access);
	n = isl_map_dim(expr->acc.access, isl_dim_out);
	access = isl_map_copy(expr->acc.access);
	access = isl_map_apply_range(isl_map_copy(data->map), access);
	pma = isl_pw_multi_aff_from_map(access);
	pma = isl_pw_multi_aff_coalesce(pma);

	index = isl_ast_expr_list_alloc(ctx, n);
	for (i = 0; i < n; ++i) {
		isl_pw_aff *pa;
		isl_ast_expr *expr;

		pa = isl_pw_multi_aff_get_pw_aff(pma, i);
		expr = isl_ast_build_expr_from_pw_aff(data->build, pa);
		index = isl_ast_expr_list_add(index, expr);
	}
	isl_pw_multi_aff_free(pma);

	data->stmt->access[data->stmt->n_access] = index;
	data->stmt->n_access++;
	return 0;
}

/* Transform the accesses in the statement associated to the domain
 * called by "node" to refer to the AST loop iterators,
 * collect them in a ppcg_stmt and annotate the node with the ppcg_stmt.
 */
static __isl_give isl_ast_node *at_each_domain(__isl_take isl_ast_node *node,
	__isl_keep isl_ast_build *build, void *user)
{
	struct ppcg_scop *scop = user;
	isl_ast_expr *expr, *arg;
	isl_ctx *ctx;
	isl_id *id;
	isl_map *map;
	struct ppcg_stmt *stmt;
	struct ppcg_add_access_data data;

	ctx = isl_ast_node_get_ctx(node);
	stmt = isl_calloc_type(ctx, struct ppcg_stmt);
	if (!stmt)
		goto error;

	expr = isl_ast_node_user_get_expr(node);
	arg = isl_ast_expr_get_op_arg(expr, 0);
	isl_ast_expr_free(expr);
	id = isl_ast_expr_get_id(arg);
	isl_ast_expr_free(arg);
	stmt->stmt = find_stmt(scop, id);
	isl_id_free(id);
	if (!stmt->stmt)
		goto error;

	stmt->n_access = 0;
	if (foreach_access_expr(stmt->stmt->body, &inc_n_access, stmt) < 0)
		goto error;

	stmt->access = isl_calloc_array(ctx, isl_ast_expr_list *,
					stmt->n_access);
	if (!stmt->access)
		goto error;

	map = isl_map_from_union_map(isl_ast_build_get_schedule(build));
	map = isl_map_reverse(map);

	stmt->n_access = 0;
	data.stmt = stmt;
	data.build = build;
	data.map = map;
	if (foreach_access_expr(stmt->stmt->body, &add_access, &data) < 0)
		node = isl_ast_node_free(node);

	isl_map_free(map);

	id = isl_id_alloc(isl_ast_node_get_ctx(node), NULL, stmt);
	id = isl_id_set_free_user(id, &ppcg_stmt_free);
	return isl_ast_node_set_annotation(node, id);
error:
	ppcg_stmt_free(stmt);
	return isl_ast_node_free(node);
}

/* Code generate the scop 'scop' and print the corresponding C code to 'p'.
 */
static __isl_give isl_printer *print_scop(isl_ctx *ctx, struct ppcg_scop *scop,
	__isl_take isl_printer *p)
{
	isl_set *context;
	isl_union_set *domain_set;
	isl_union_map *schedule_map;
	isl_ast_build *build;
	isl_ast_print_options *print_options;
	isl_ast_node *tree;

	context = isl_set_copy(scop->context);
	domain_set = isl_union_set_copy(scop->domain);
	schedule_map = isl_union_map_copy(scop->schedule);
	schedule_map = isl_union_map_intersect_domain(schedule_map, domain_set);

	build = isl_ast_build_from_context(context);
	build = isl_ast_build_set_at_each_domain(build, &at_each_domain, scop);
	tree = isl_ast_build_ast_from_schedule(build, schedule_map);
	isl_ast_build_free(build);

	print_options = isl_ast_print_options_alloc(ctx);
	print_options = isl_ast_print_options_set_print_user(print_options,
							&print_user, NULL);

	p = isl_ast_node_print_macros(tree, p);
	p = isl_ast_node_print(tree, p, print_options);

	isl_ast_node_free(tree);

	return p;
}

/* Does "scop" refer to any arrays that are declared, but not
 * exposed to the code after the scop?
 */
static int any_hidden_declarations(struct ppcg_scop *scop)
{
	int i;

	if (!scop)
		return 0;

	for (i = 0; i < scop->n_array; ++i)
		if (scop->arrays[i]->declared && !scop->arrays[i]->exposed)
			return 1;

	return 0;
}

int generate_cpu(isl_ctx *ctx, struct ppcg_scop *ps,
	struct ppcg_options *options, const char *input, const char *output)
{
	FILE *input_file;
	FILE *output_file;
	isl_printer *p;
	int hidden;

	if (!ps)
		return -1;

	input_file = fopen(input, "r");
	output_file = get_output_file(input, output);

	copy_before_scop(input_file, output_file);
	fprintf(output_file, "/* ppcg generated CPU code */\n\n");
	p = isl_printer_to_file(ctx, output_file);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = ppcg_print_exposed_declarations(p, ps);
	hidden = any_hidden_declarations(ps);
	if (hidden) {
		p = ppcg_start_block(p);
		p = ppcg_print_hidden_declarations(p, ps);
	}
	p = print_scop(ctx, ps, p);
	if (hidden)
		p = ppcg_end_block(p);
	isl_printer_free(p);
	copy_after_scop(input_file, output_file);

	fclose(output_file);
	fclose(input_file);

	return 0;
}
