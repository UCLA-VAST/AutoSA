/*
 * Copyright 2012-2013 Ecole Normale Superieure. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ECOLE NORMALE SUPERIEURE ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL ECOLE NORMALE SUPERIEURE OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as
 * representing official policies, either expressed or implied, of
 * Ecole Normale Superieure.
 */

#include <assert.h>
#include <isl/space.h>
#include <isl/aff.h>
#include <isl/ast_build.h>
#include <isl/options.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_set.h>
#include <isl/schedule_node.h>

struct options {
	struct isl_options	*isl;
	unsigned		 tree;
	unsigned		 atomic;
	unsigned		 separate;
	unsigned		 read_options;
};

ISL_ARGS_START(struct options, options_args)
ISL_ARG_CHILD(struct options, isl, "isl", &isl_options_args, "isl options")
ISL_ARG_BOOL(struct options, tree, 0, "tree", 0,
	"input schedule is specified as schedule tree")
ISL_ARG_BOOL(struct options, atomic, 0, "atomic", 0,
	"globally set the atomic option")
ISL_ARG_BOOL(struct options, separate, 0, "separate", 0,
	"globally set the separate option")
ISL_ARG_BOOL(struct options, read_options, 0, "read-options", 0,
	"read options from standard input")
ISL_ARGS_END

ISL_ARG_DEF(options, struct options, options_args)

/* Return a universal, 1-dimensional set with the given name.
 */
static __isl_give isl_union_set *universe(isl_ctx *ctx, const char *name)
{
	isl_space *space;

	space = isl_space_set_alloc(ctx, 0, 1);
	space = isl_space_set_tuple_name(space, isl_dim_set, name);
	return isl_union_set_from_set(isl_set_universe(space));
}

/* Set the "name" option for the entire schedule domain.
 */
static __isl_give isl_union_map *set_universe(__isl_take isl_union_map *opt,
	__isl_keep isl_union_map *schedule, const char *name)
{
	isl_ctx *ctx;
	isl_union_set *domain, *target;
	isl_union_map *option;

	ctx = isl_union_map_get_ctx(opt);

	domain = isl_union_map_range(isl_union_map_copy(schedule));
	domain = isl_union_set_universe(domain);
	target = universe(ctx, name);
	option = isl_union_map_from_domain_and_range(domain, target);
	opt = isl_union_map_union(opt, option);

	return opt;
}

/* Set the build options based on the command line options.
 *
 * If no command line options are specified, we use default build options.
 * If --read-options is specified, we read the build options from standard
 * input.
 * If --separate or --atomic is specified, we turn on the corresponding
 * build option over the entire schedule domain.
 */
static __isl_give isl_ast_build *set_options(__isl_take isl_ast_build *build,
	struct options *options, __isl_keep isl_union_map *schedule)
{
	isl_ctx *ctx;
	isl_union_map *opt;

	if (!options->separate && !options->atomic && !options->read_options)
		return build;

	ctx = isl_union_map_get_ctx(schedule);

	if (options->read_options)
		opt = isl_union_map_read_from_file(ctx, stdin);
	else
		opt = isl_union_map_empty(isl_union_map_get_space(schedule));

	if (options->separate)
		opt = set_universe(opt, schedule, "separate");
	if (options->atomic)
		opt = set_universe(opt, schedule, "atomic");

	build = isl_ast_build_set_options(build, opt);

	return build;
}

/* Print a function declaration for the domain "set".
 *
 * In particular, print a declaration of the form
 *
 *	void S(int, ..., int);
 *
 * where S is the name of the domain and the number of arguments
 * is equal to the dimension of "set".
 */
static isl_stat print_declaration(__isl_take isl_set *set, void *user)
{
	isl_printer **p = user;
	int i, n;

	n = isl_set_dim(set, isl_dim_set);

	*p = isl_printer_start_line(*p);
	*p = isl_printer_print_str(*p, "void ");
	*p = isl_printer_print_str(*p, isl_set_get_tuple_name(set));
	*p = isl_printer_print_str(*p, "(");
	for (i = 0; i < n; ++i) {
		if (i)
			*p = isl_printer_print_str(*p, ", ");
		*p = isl_printer_print_str(*p, "int");
	}
	*p = isl_printer_print_str(*p, ");");
	*p = isl_printer_end_line(*p);

	isl_set_free(set);

	return isl_stat_ok;
}

/* Print a function declaration for each domain in "uset".
 */
static __isl_give isl_printer *print_declarations(__isl_take isl_printer *p,
	__isl_keep isl_union_set *uset)
{
	if (isl_union_set_foreach_set(uset, &print_declaration, &p) >= 0)
		return p;
	isl_printer_free(p);
	return NULL;
}

/* Check that the domain of "map" is named.
 */
static isl_stat check_name(__isl_take isl_map *map, void *user)
{
	int named;
	isl_ctx *ctx;

	ctx = isl_map_get_ctx(map);
	named = isl_map_has_tuple_name(map, isl_dim_in);
	isl_map_free(map);

	if (named < 0)
		return isl_stat_error;
	if (!named)
		isl_die(ctx, isl_error_invalid,
			"all domains should be named", return isl_stat_error);
	return isl_stat_ok;
}

/* Given an AST "tree", print out the following code
 *
 *	void foo(<parameters>/)
 *	{
 *		void S1(int,...,int);
 *	#pragma scop
 *		AST
 *	#pragma endscop
 *	}
 *
 * where the declarations are derived from the spaces in "domain".
 */
static void print_tree(__isl_take isl_union_set *domain,
	__isl_take isl_ast_node *tree)
{
	int i, n;
	isl_ctx *ctx;
	isl_space *space;
	isl_printer *p;
	isl_ast_print_options *print_options;

	if (!domain || !tree)
		goto error;

	ctx = isl_union_set_get_ctx(domain);

	p = isl_printer_to_file(ctx, stdout);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "void foo(");

	space = isl_union_set_get_space(domain);
	n = isl_space_dim(space, isl_dim_param);
	for (i = 0; i < n; ++i) {
		const char *name;

		if (i)
			p = isl_printer_print_str(p, ", ");
		name = isl_space_get_dim_name(space, isl_dim_param, i);
		p = isl_printer_print_str(p, "int ");
		p = isl_printer_print_str(p, name);
	}
	isl_space_free(space);

	p = isl_printer_print_str(p, ")");
	p = isl_printer_end_line(p);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "{");
	p = isl_printer_end_line(p);
	p = isl_printer_start_line(p);
	p = isl_printer_indent(p, 2);
	p = print_declarations(p, domain);
	p = isl_printer_indent(p, -2);
	p = isl_printer_print_str(p, "#pragma scop");
	p = isl_printer_end_line(p);

	p = isl_printer_indent(p, 2);
	print_options = isl_ast_print_options_alloc(ctx);
	p = isl_ast_node_print(tree, p, print_options);
	p = isl_printer_indent(p, -2);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "#pragma endscop");
	p = isl_printer_end_line(p);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "}");
	p = isl_printer_end_line(p);
	isl_printer_free(p);

error:
	isl_union_set_free(domain);
	isl_ast_node_free(tree);
}

/* If "node" is a band node, then replace the AST build options
 * by "options".
 */
static __isl_give isl_schedule_node *node_set_options(
	__isl_take isl_schedule_node *node, void *user)
{
	enum isl_ast_loop_type *type = user;
	int i, n;

	if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
		return node;

	n = isl_schedule_node_band_n_member(node);
	for (i = 0; i < n; ++i)
		node = isl_schedule_node_band_member_set_ast_loop_type(node,
								i, *type);
	return node;
}

/* Replace the AST build options on all band nodes if requested
 * by the user.
 */
static __isl_give isl_schedule *schedule_set_options(
	__isl_take isl_schedule *schedule, struct options *options)
{
	enum isl_ast_loop_type type;

	if (!options->separate && !options->atomic)
		return schedule;

	type = options->separate ? isl_ast_loop_separate : isl_ast_loop_atomic;
	schedule = isl_schedule_map_schedule_node_bottom_up(schedule,
						&node_set_options, &type);

	return schedule;
}

/* Read a schedule tree, generate an AST and print the result
 * in a form that is readable by pet.
 */
static int print_schedule_tree(isl_ctx *ctx, struct options *options)
{
	isl_union_set *domain;
	isl_schedule *schedule;
	isl_ast_build *build;
	isl_ast_node *tree;

	schedule = isl_schedule_read_from_file(ctx, stdin);
	domain = isl_schedule_get_domain(schedule);

	build = isl_ast_build_alloc(ctx);
	schedule = schedule_set_options(schedule, options);
	tree = isl_ast_build_node_from_schedule(build, schedule);
	isl_ast_build_free(build);

	print_tree(domain, tree);

	return 0;
}

/* Read a schedule, a context and (optionally) build options,
 * generate an AST and print the result in a form that is readable
 * by pet.
 */
static int print_schedule_map(isl_ctx *ctx, struct options *options)
{
	isl_set *context;
	isl_union_set *domain;
	isl_union_map *schedule;
	isl_ast_build *build;
	isl_ast_node *tree;

	schedule = isl_union_map_read_from_file(ctx, stdin);
	if (isl_union_map_foreach_map(schedule, &check_name, NULL) < 0) {
		isl_union_map_free(schedule);
		return 1;
	}
	context = isl_set_read_from_file(ctx, stdin);

	domain = isl_union_map_domain(isl_union_map_copy(schedule));
	domain = isl_union_set_align_params(domain, isl_set_get_space(context));

	build = isl_ast_build_from_context(context);
	build = set_options(build, options, schedule);
	tree = isl_ast_build_node_from_schedule_map(build, schedule);
	isl_ast_build_free(build);

	print_tree(domain, tree);

	return 0;
}

/* Read either
 * - a schedule tree or
 * - a schedule, a context and (optionally) build options,
 * generate an AST and print the result in a form that is readable
 * by pet.
 */
int main(int argc, char **argv)
{
	isl_ctx *ctx;
	struct options *options;
	int r;

	options = options_new_with_defaults();
	assert(options);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);

	ctx = isl_ctx_alloc_with_options(&options_args, options);

	if (options->tree)
		r = print_schedule_tree(ctx, options);
	else
		r = print_schedule_map(ctx, options);

	isl_ctx_free(ctx);
	return r;
}
