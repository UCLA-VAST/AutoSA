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

/* Print "extent" as a sequence of
 *
 *	[1 + maximal_value]
 *
 * one for each dimension.
 */
static __isl_give isl_printer *print_extent(__isl_take isl_printer *p,
	__isl_keep isl_set *extent)
{
	int i, n;

	n = isl_set_dim(extent, isl_dim_set);
	if (n == 0)
		return p;

	for (i = 0; i < n; ++i) {
		isl_set *dom;
		isl_local_space *ls;
		isl_aff *one;
		isl_pw_aff *bound;

		bound = isl_set_dim_max(isl_set_copy(extent), i);
		dom = isl_pw_aff_domain(isl_pw_aff_copy(bound));
		ls = isl_local_space_from_space(isl_set_get_space(dom));
		one = isl_aff_zero_on_domain(ls);
		one = isl_aff_add_constant_si(one, 1);
		bound = isl_pw_aff_add(bound, isl_pw_aff_alloc(dom, one));

		p = isl_printer_print_str(p, "[");
		p = isl_printer_print_pw_aff(p, bound);
		p = isl_printer_print_str(p, "]");

		isl_pw_aff_free(bound);
	}

	return p;
}

/* Print declarations for the arrays in "scop" that are declared
 * and that are exposed (if exposed == 1) or not exposed (if exposed == 0).
 */
static __isl_give isl_printer *print_declarations(__isl_take isl_printer *p,
	struct ppcg_scop *scop, int exposed)
{
	int i;

	if (!scop)
		return isl_printer_free(p);

	for (i = 0; i < scop->pet->n_array; ++i) {
		struct pet_array *array = scop->pet->arrays[i];
		const char *name;

		if (!array->declared)
			continue;
		if (array->exposed != exposed)
			continue;

		name = isl_set_get_tuple_name(array->extent);

		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, array->element_type);
		p = isl_printer_print_str(p, " ");
		p = isl_printer_print_str(p, name);
		p = print_extent(p, array->extent);
		p = isl_printer_print_str(p, ";");
		p = isl_printer_end_line(p);
	}

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

/* Internal data structure for print_guarded_user.
 *
 * fn is the function that should be called to print the body.
 * user is the argument that should be passed to this function.
 */
struct ppcg_print_guarded_data {
	__isl_give isl_printer *(*fn)(__isl_take isl_printer *p, void *user);
	void *user;
};

/* Print the body of the if statement expressing the guard passed
 * to "ppcg_print_guarded" by calling data->fn.
 */
static __isl_give isl_printer *print_guarded_user(__isl_take isl_printer *p,
	__isl_take isl_ast_print_options *options,
	__isl_keep isl_ast_node *node, void *user)
{
	struct ppcg_print_guarded_data *data = user;

	p = data->fn(p, data->user);

	isl_ast_print_options_free(options);
	return p;
}

/* Print a condition for the given "guard" within the given "context"
 * on "p", calling "fn" with "user" to print the body of the if statement.
 * If the guard is implied by the context, then no if statement is printed
 * and the body is printed directly to "p".
 *
 * Both "guard" and "context" are assumed to be parameter sets.
 *
 * We slightly abuse the AST generator to print this guard.
 * In particular, we create a trivial schedule for an iteration
 * domain with a single instance, restricted by the guard.
 */
__isl_give isl_printer *ppcg_print_guarded(__isl_take isl_printer *p,
	__isl_take isl_set *guard, __isl_take isl_set *context,
	__isl_give isl_printer *(*fn)(__isl_take isl_printer *p, void *user),
	void *user)
{
	struct ppcg_print_guarded_data data = { fn, user };
	isl_ctx *ctx;
	isl_union_map *schedule;
	isl_ast_build *build;
	isl_ast_node *tree;
	isl_ast_print_options *options;

	ctx = isl_printer_get_ctx(p);
	guard = isl_set_from_params(guard);
	schedule = isl_union_map_from_map(isl_map_from_domain(guard));
	build = isl_ast_build_from_context(context);
	tree = isl_ast_build_ast_from_schedule(build, schedule);
	isl_ast_build_free(build);

	options = isl_ast_print_options_alloc(ctx);
	options = isl_ast_print_options_set_print_user(options,
						&print_guarded_user, &data);
	p = isl_ast_node_print(tree, p, options);
	isl_ast_node_free(tree);

	return p;
}
