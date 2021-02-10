#ifndef PPCG_H
#define PPCG_H

#include <isl/schedule.h>
#include <isl/set.h>
#include <isl/union_set.h>
#include <isl/union_map.h>
#include <isl/id_to_ast_expr.h>
#include <pet.h>

#include "ppcg_options.h"

#define _DEBUG

#define DBGVAR(os, var)                                  \
  (os) << "DBG: " << __FILE__ << "(" << __LINE__ << ") " \
       << #var << " = [" << (var) << "]" << std::endl;

#define DBGSCHDNODE(os, node, ctx)                                    {\
  printf("%s(%d) Print schedule_node.\n", __FILE__, __LINE__);         \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_set_yaml_style(p_debug, ISL_YAML_STYLE_BLOCK); \
  p_debug = isl_printer_print_schedule_node(p_debug, node);            \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGSCHD(os, node, ctx)                                        {\
  printf("%s(%d) Print schedule.\n", __FILE__, __LINE__);              \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_set_yaml_style(p_debug, ISL_YAML_STYLE_BLOCK); \
  p_debug = isl_printer_print_schedule(p_debug, node);                 \
  p_debug = isl_printer_free(p_debug);                                 \
} 

#define DBGSET(os, set, ctx)                                          {\
  printf("%s(%d) Print set.\n", __FILE__, __LINE__);                   \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_set(p_debug, set);                       \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGSPACE(os, space, ctx)                                      {\
  printf("%s(%d) Print space.\n", __FILE__, __LINE__);                 \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_space(p_debug, space);                   \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGUSET(os, uset, ctx)                                        {\
  printf("%s(%d) Print union_set.\n", __FILE__, __LINE__);             \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_union_set(p_debug, uset);                \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGUMAP(os, umap, ctx)                                        {\
  printf("%s(%d) Print union_map.\n", __FILE__, __LINE__);             \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_union_map(p_debug, umap);                \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGMAP(os, map, ctx)                                          {\
  printf("%s(%d) Print map.\n", __FILE__, __LINE__);                   \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_map(p_debug, map);                       \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGBMAP(os, bmap, ctx)                                        {\
  printf("%s(%d) Print basic_map.\n", __FILE__, __LINE__);             \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_basic_map(p_debug, bmap);                \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGMA(os, ma, ctx)                                            {\
  printf("%s(%d) Print multi_aff.\n", __FILE__, __LINE__);             \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_multi_aff(p_debug, ma);                  \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGVEC(os, vec, ctx)                                          {\
  printf("%s(%d) Print vec.\n", __FILE__, __LINE__);                   \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_vec(p_debug, vec);                       \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGASTEXPR(os, astexpr, ctx)                                  {\
  printf("%s(%d) Print AST expr.\n", __FILE__, __LINE__);              \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_set_output_format(p_debug, ISL_FORMAT_C);      \
  p_debug = isl_printer_print_ast_expr(p_debug, astexpr);              \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGASTNODE(os, astnode, ctx)                                  {\
  printf("%s(%d) Print AST node.\n", __FILE__, __LINE__);              \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_set_output_format(p_debug, ISL_FORMAT_C);      \
  p_debug = isl_printer_print_ast_node(p_debug, astnode);              \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGMUPA(os, mupa, ctx)                                        {\
  printf("%s(%d) Print multi_union_pw_aff.\n", __FILE__, __LINE__);    \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_multi_union_pw_aff(p_debug, mupa);       \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGUPA(os, upa, ctx)                                          {\
  printf("%s(%d) Print union_pw_aff.\n", __FILE__, __LINE__);          \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_union_pw_aff(p_debug, upa);              \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGVAL(os, val, ctx)                                          {\
  printf("%s(%d) Print val.\n", __FILE__, __LINE__);                   \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_val(p_debug, val);                       \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGID(os, id, ctx)                                            {\
  printf("%s(%d) Print id.\n", __FILE__, __LINE__);                    \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_id(p_debug, id);                         \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#define DBGPWQPOLY(os, pwqpoly, ctx)                                  {\
  printf("%s(%d) Print id.\n", __FILE__, __LINE__);                    \
  isl_printer *p_debug = isl_printer_to_file(ctx, os);                 \
  p_debug = isl_printer_print_pw_qpolynomial(p_debug, pwqpoly);        \
  p_debug = isl_printer_print_str(p_debug, "\n");                      \
  p_debug = isl_printer_free(p_debug);                                 \
}

#ifdef __cplusplus
extern "C"
{
#endif

	const char *ppcg_base_name(const char *filename);
	int ppcg_extract_base_name(char *name, const char *input);

	/* Representation of the scop for use inside PPCG.
 *
 * "options" are the options specified by the user.
 * Some fields in this structure may depend on some of the options.
 *
 * "start" and "end" are file offsets of the corresponding program text.
 * "context" represents constraints on the parameters.
 * "domain" is the union of all iteration domains.
 * "call" contains the iteration domains of statements with a call expression.
 * "reads" contains all potential read accesses.
 * "tagged_reads" is the same as "reads", except that the domain is a wrapped
 *	relation mapping an iteration domain to a reference identifier
 * "live_in" contains the potential read accesses that potentially
 *	have no corresponding writes in the scop.
 * "may_writes" contains all potential write accesses.
 * "tagged_may_writes" is the same as "may_writes", except that the domain
 *	is a wrapped relation mapping an iteration domain
 *	to a reference identifier
 * "must_writes" contains all definite write accesses.
 * "tagged_must_writes" is the same as "must_writes", except that the domain
 *	is a wrapped relation mapping an iteration domain
 *	to a reference identifier
 * "live_out" contains the potential write accesses that are potentially
 *	not killed by any kills or any other writes.
 * "must_kills" contains all definite kill accesses.
 * "tagged_must_kills" is the same as "must_kills", except that the domain
 *	is a wrapped relation mapping an iteration domain
 *	to a reference identifier.
 *
 * "tagger" maps tagged iteration domains to the corresponding untagged
 *	iteration domain.
 *
 * "independence" is the union of all independence filters.
 *
 * "dep_flow" represents the potential flow dependences.
 * "tagged_dep_flow" is the same as "dep_flow", except that both domain and
 *	range are wrapped relations mapping an iteration domain to
 *	a reference identifier.  May be NULL if not computed.
 * "dep_false" represents the potential false (anti and output) dependences.
 * "dep_forced" represents the validity constraints that should be enforced
 *	even when live-range reordering is used.
 *	In particular, these constraints ensure that all live-in
 *	accesses remain live-in and that all live-out accesses remain live-out
 *	and that multiple potential sources for the same read are
 *	executed in the original order.
 * "dep_order"/"tagged_dep_order" represents the order dependences between
 *	the live range intervals in "dep_flow"/"tagged_dep_flow".
 *	It is only used if the live_range_reordering
 *	option is set.  Otherwise it is NULL.
 *	If "dep_order" is used, then "dep_false" only contains a limited
 *	set of anti and output dependences.
 * "schedule" represents the (original) schedule.
 *
 * "names" contains all variable names that are in use by the scop.
 * The names are mapped to a dummy value.
 *
 * "pet" is the original pet_scop.
 */
	struct ppcg_scop
	{
		struct ppcg_options *options;

		unsigned start;
		unsigned end;

		isl_set *context;
		isl_union_set *domain;
		isl_union_set *call;
		isl_union_map *tagged_reads;
		isl_union_map *reads;
		isl_union_map *live_in;
		isl_union_map *tagged_may_writes;
		isl_union_map *may_writes;
		isl_union_map *tagged_must_writes;
		isl_union_map *must_writes;
		isl_union_map *live_out;
		isl_union_map *tagged_must_kills;
		isl_union_map *must_kills;

		isl_union_pw_multi_aff *tagger;

		isl_union_map *independence;

		isl_union_map *dep_flow;
		isl_union_map *tagged_dep_flow;
		isl_union_map *dep_false;
		isl_union_map *dep_forced;
		isl_union_map *dep_order;
		isl_union_map *tagged_dep_order;
		isl_schedule *schedule;

		isl_id_to_ast_expr *names;

		struct pet_scop *pet;

		/* AutoSA Extended */
		isl_union_map *dep_rar;
		isl_union_map *tagged_dep_rar;
		isl_union_map *dep_waw;
		isl_union_map *tagged_dep_waw;
		/* AutoSA Extended */
	};

	int ppcg_scop_any_hidden_declarations(struct ppcg_scop *scop);
	__isl_give isl_id_list *ppcg_scop_generate_names(struct ppcg_scop *scop,
																									 int n, const char *prefix);

	int ppcg_transform(isl_ctx *ctx, const char *input, FILE *out,
										 struct ppcg_options *options,
										 __isl_give isl_printer *(*fn)(__isl_take isl_printer *p,
																									 struct ppcg_scop *scop, void *user),
										 void *user);

	int autosa_main_wrap(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif
