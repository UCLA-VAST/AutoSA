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

#include <cloog/cloog.h>
#include <cloog/isl/cloog.h>
#include <isl/aff.h>
#include <isl/ctx.h>
#include <isl/map.h>
#include <pet.h>

#include "clast_printer.h"
#include "cpu.h"
#include "pet_printer.h"
#include "rewrite.h"

/* Derive the output file name from the input file name.
 * 'input' is the entire path of the input file. The output
 * is the file name plus the additional extension.
 *
 * We will basically replace everything after the last point
 * with '.ppcg.c'. This means file.c becomes file.ppcg.c
 */
FILE *get_output_file(const char *input)
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

	return fopen(name, "w");
}

/* Print a memory access 'access' to the printer 'p'.
 *
 * Given a map [a,b,c] -> {S[i,j] -> A[i,j+a]} we will print: A[i][j+a].
 *
 * In case the output dimensions is one dimensional and unnamed we assume this
 * is not a memory access, but just an expression.  This means the example
 * [a,b,c] -> {S[i,j] -> [j+a]} will be printed as: j+a
 *
 * The code that is printed is C code and the variable parameter and input
 * dimension names are derived from the isl_dim names.
 */
static __isl_give isl_printer *print_access(__isl_take isl_printer *p,
	__isl_take isl_map *access)
{
	int i;
	const char *name;
	unsigned n_index;
	isl_pw_multi_aff *pma;

	n_index = isl_map_dim(access, isl_dim_out);
	name = isl_map_get_tuple_name(access, isl_dim_out);
	pma = isl_pw_multi_aff_from_map(access);
	pma = isl_pw_multi_aff_coalesce(pma);

	if (name == NULL) {
		isl_pw_aff *index;
		index = isl_pw_multi_aff_get_pw_aff(pma, 0);
		p = isl_printer_print_str(p, "(");
		p = isl_printer_print_pw_aff(p, index);
		p = isl_printer_print_str(p, ")");
		isl_pw_aff_free(index);
		isl_pw_multi_aff_free(pma);
		return p;
	}

	p = isl_printer_print_str(p, name);

	for (i = 0; i < n_index; ++i) {
		isl_pw_aff *index;

		index = isl_pw_multi_aff_get_pw_aff(pma, i);

		p = isl_printer_print_str(p, "[");
		p = isl_printer_print_pw_aff(p, index);
		p = isl_printer_print_str(p, "]");
		isl_pw_aff_free(index);
	}

	isl_pw_multi_aff_free(pma);

	return p;
}

static __isl_give isl_printer *print_cpu_access(__isl_take isl_printer *p,
	struct pet_expr *expr, void *usr)
{
	isl_map *access = isl_map_copy(expr->acc.access);

	return print_access(p, access);
}

static void print_stmt_body(FILE *out, struct pet_stmt *stmt)
{
	isl_ctx *ctx = isl_set_get_ctx(stmt->domain);
	isl_printer *p;

	p = isl_printer_to_file(ctx, out);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = print_pet_expr(p, stmt->body, &print_cpu_access, out);
	isl_printer_free(p);
}

/* Create a CloogInput data structure that describes the 'scop'.
 */
static CloogInput *cloog_input_from_scop(CloogState *state,
	struct pet_scop *scop)
{
	CloogDomain *cloog_context;
	CloogUnionDomain *ud;
	CloogInput *input;
	isl_set *context;
	isl_union_set *domain_set;
	isl_union_map *schedule_map;

	scop = pet_scop_align_params(scop);

	context = isl_set_copy(scop->context);
	domain_set = pet_scop_collect_domains(scop);
	schedule_map = pet_scop_collect_schedule(scop);
	schedule_map = isl_union_map_intersect_domain(schedule_map, domain_set);

	ud = cloog_union_domain_from_isl_union_map(schedule_map);

	cloog_context = cloog_domain_from_isl_set(context);

	input = cloog_input_alloc(cloog_context, ud);

	return input;
}

/* Print a #define macro for every statement in the 'scop'.
 */
static void print_stmt_definitions(struct pet_scop *scop, FILE *output)
{
	int i, j;

	for (i = 0; i < scop->n_stmt; ++i) {
		struct pet_stmt *stmt = scop->stmts[i];
		const char *name = isl_set_get_tuple_name(stmt->domain);

		fprintf(output, "#define %s(", name);

		for (j = 0; j < isl_set_dim(stmt->domain, isl_dim_set); ++j) {
			const char *name;

			if (j)
				fprintf(output, ", ");

			name = isl_set_get_dim_name(stmt->domain, isl_dim_set, j);
			fprintf(output, "%s", name);
		}

		fprintf(output, ") ");

		print_stmt_body(output, stmt);

		fprintf(output, "\n");
	}
}

/* Code generate the scop 'scop' and print the corresponding C code to
 * 'output'.
 */
static void print_scop(isl_ctx *ctx, struct pet_scop *scop, FILE *output)
{
	CloogState *state;
	CloogOptions *options;
	CloogInput *input;
	struct clast_stmt *stmt;
	struct clast_printer_info code;

	state = cloog_isl_state_malloc(ctx);

	options = cloog_options_malloc(state);
	options->language = CLOOG_LANGUAGE_C;
	options->otl = 1;
	options->strides = 1;

	input = cloog_input_from_scop(state, scop);
	stmt = cloog_clast_create_from_input(input, options);

	code.indent = 0;
	code.dst = output;
	code.print_user_stmt = NULL;
	code.print_user_stmt_list = NULL;
	code.print_for_head = NULL;
	code.print_for_foot = NULL;

	print_cloog_macros(output);
	fprintf(output, "\n");
	print_stmt_definitions(scop, output);
	fprintf(output, "\n");
	print_clast(&code, stmt);

	cloog_clast_free(stmt);
	cloog_options_free(options);
	cloog_state_free(state);

	fprintf(output, "\n");
}

int generate_cpu(isl_ctx *ctx, struct pet_scop *scop,
	struct ppcg_options *options, const char *input)
{
	FILE *input_file;
	FILE *output_file;

	if (!scop)
		return -1;

	input_file = fopen(input, "r");
	output_file = get_output_file(input);

	copy_before_scop(input_file, output_file);
	fprintf(output_file, "/* ppcg generated CPU code */\n\n");
	print_scop(ctx, scop, output_file);
	copy_after_scop(input_file, output_file);

	fclose(output_file);
	fclose(input_file);

	return 0;
}
