/*
 * Copyright 2014      Ecole Normale Superieure. All rights reserved.
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

#include <isl/ctx.h>
#include <isl/printer.h>
#include <isl/id.h>
#include <isl/space.h>
#include <isl/union_set.h>
#include <isl/union_map.h>

#include "aff.h"
#include "summary.h"

/* A pet_function_summary objects represents an argument of a function.
 *
 * If "type" is pet_arg_int, then the argument has an integer type and
 * can be used to describe the accesses performed by the pet_arg_array
 * arguments.  In this case, "id" refers to the formal argument.
 *
 * If "type" is pet_arg_array, then we keep track of the accesses
 * through this argument in the access relations in "access".
 * The domains of these access relations refer to the integer arguments
 * of the function.  That is, the input dimensions correspond
 * to the arguments of type pet_arg_int.
 *
 * If "type" is pet_arg_other, then we do not keep track of any
 * further information.
 */
struct pet_function_summary_arg {
	enum pet_arg_type type;

	union {
		isl_id *id;
		isl_union_map *access[pet_expr_access_end];
	};
};

/* A pet_function_summary object keeps track of the accesses performed
 * by a function in terms of the function arguments.
 *
 * "n" is the number of arguments.
 * "arg" contains a description of the "n" arguments.
 */
struct pet_function_summary {
	int ref;
	isl_ctx *ctx;

	unsigned n;

	struct pet_function_summary_arg arg[];
};

/* Construct and return a new pet_function_summary object with
 * "n_arg" arguments, initialized to pet_arg_other.
 */
__isl_give pet_function_summary *pet_function_summary_alloc(isl_ctx *ctx,
	unsigned n_arg)
{
	pet_function_summary *summary;
	int i;

	summary = isl_calloc(ctx, struct pet_function_summary,
			    sizeof(struct pet_function_summary) +
			    n_arg * sizeof(struct pet_function_summary_arg));
	if (!summary)
		return summary;

	summary->ctx = ctx;
	isl_ctx_ref(ctx);
	summary->ref = 1;
	summary->n = n_arg;
	for (i = 0; i < n_arg; ++i)
		summary->arg[i].type = pet_arg_other;

	return summary;
}

/* Return an extra reference to "summary".
 */
__isl_give pet_function_summary *pet_function_summary_copy(
	__isl_keep pet_function_summary *summary)
{
	if (!summary)
		return NULL;

	summary->ref++;
	return summary;
}

/* Return the isl_ctx in which "summary" was created.
 */
isl_ctx *pet_function_summary_get_ctx(__isl_keep pet_function_summary *summary)
{
	return summary ? summary->ctx : NULL;
}

/* Free the data stored in "arg".
 */
static void free_arg(struct pet_function_summary_arg *arg)
{
	enum pet_expr_access_type type;

	if (arg->type == pet_arg_int)
		isl_id_free(arg->id);
	if (arg->type != pet_arg_array)
		return;
	for (type = pet_expr_access_begin; type < pet_expr_access_end; ++type)
		arg->access[type] = isl_union_map_free(arg->access[type]);
}

/* Free a reference to "summary".
 */
__isl_null pet_function_summary *pet_function_summary_free(
	__isl_take pet_function_summary *summary)
{
	int i;

	if (!summary)
		return NULL;
	if (--summary->ref > 0)
		return NULL;

	for (i = 0; i < summary->n; ++i)
		free_arg(&summary->arg[i]);

	isl_ctx_deref(summary->ctx);
	free(summary);
	return NULL;
}

/* Return the number of arguments of the function summarized by "summary".
 */
int pet_function_summary_get_n_arg(__isl_keep pet_function_summary *summary)
{
	if (!summary)
		return -1;

	return summary->n;
}

/* Mark the argument at position "pos" of "summary" as an integer argument
 * with the given identifier.
 */
__isl_give pet_function_summary *pet_function_summary_set_int(
	__isl_take pet_function_summary *summary, int pos,
	__isl_take isl_id *id)
{
	if (!summary || !id)
		goto error;

	if (pos < 0 || pos >= summary->n)
		isl_die(summary->ctx, isl_error_invalid,
			"position out of bounds", goto error);

	free_arg(&summary->arg[pos]);

	summary->arg[pos].type = pet_arg_int;
	summary->arg[pos].id = id;

	return summary;
error:
	isl_id_free(id);
	return pet_function_summary_free(summary);
}

/* Mark the argument at position "pos" of "summary" as an array argument
 * with the given sets of accessed elements.
 * The integer arguments of "summary" may appear as parameters
 * in these sets of accessed elements.
 * These parameters are turned into input dimensions of
 * the corresponding access relations, which are then associated
 * to the array argument.
 * The order of the input dimensions is the same as the order
 * in which the integer arguments appear in the sequence of arguments.
 */
__isl_give pet_function_summary *pet_function_summary_set_array(
	__isl_take pet_function_summary *summary, int pos,
	__isl_take isl_union_set *may_read, __isl_take isl_union_set *may_write,
	__isl_take isl_union_set *must_write)
{
	int i, n;
	isl_space *space;
	enum pet_expr_access_type type;

	if (!summary || !may_read || !may_write || !must_write)
		goto error;

	if (pos < 0 || pos >= summary->n)
		isl_die(summary->ctx, isl_error_invalid,
			"position out of bounds", goto error);

	n = 0;
	for (i = 0; i < summary->n; ++i)
		if (pet_function_summary_arg_is_int(summary, i))
			n++;

	space = isl_space_params_alloc(summary->ctx, n);

	n = 0;
	for (i = 0; i < summary->n; ++i)
		if (pet_function_summary_arg_is_int(summary, i))
			space = isl_space_set_dim_id(space, isl_dim_param, n++,
					    isl_id_copy(summary->arg[i].id));

	free_arg(&summary->arg[pos]);

	summary->arg[pos].type = pet_arg_array;
	summary->arg[pos].access[pet_expr_access_may_read] =
		isl_union_map_from_range(may_read);
	summary->arg[pos].access[pet_expr_access_may_write] =
		isl_union_map_from_range(may_write);
	summary->arg[pos].access[pet_expr_access_must_write] =
		isl_union_map_from_range(must_write);

	for (type = pet_expr_access_begin; type < pet_expr_access_end; ++type) {
		isl_union_map *umap;
		umap = summary->arg[pos].access[type];
		umap = isl_union_map_align_params(umap, isl_space_copy(space));
		umap = pet_union_map_move_dims(umap, isl_dim_in, 0,
						isl_dim_param, 0, n);
		summary->arg[pos].access[type] = umap;
		if (!umap)
			break;
	}

	isl_space_free(space);

	if (type < pet_expr_access_end)
		return pet_function_summary_free(summary);

	return summary;
error:
	isl_union_set_free(may_read);
	isl_union_set_free(may_write);
	isl_union_set_free(must_write);
	return pet_function_summary_free(summary);
}

/* Has the argument of "summary" at position "pos" been marked
 * as an integer argument?
 */
int pet_function_summary_arg_is_int(__isl_keep pet_function_summary *summary,
	int pos)
{
	if (!summary)
		return -1;

	if (pos < 0 || pos >= summary->n)
		isl_die(summary->ctx, isl_error_invalid,
			"position out of bounds", return -1);

	return summary->arg[pos].type == pet_arg_int;
}

/* Has the argument of "summary" at position "pos" been marked
 * as an array argument?
 */
int pet_function_summary_arg_is_array(__isl_keep pet_function_summary *summary,
	int pos)
{
	if (!summary)
		return -1;

	if (pos < 0 || pos >= summary->n)
		isl_die(summary->ctx, isl_error_invalid,
			"position out of bounds", return -1);

	return summary->arg[pos].type == pet_arg_array;
}

/* Return the access relation of type "type" associated to
 * the argument of "summary" at position "pos", which is assumed
 * to be an array argument.
 */
__isl_give isl_union_map *pet_function_summary_arg_get_access(
	__isl_keep pet_function_summary *summary, int pos,
	enum pet_expr_access_type type)
{
	if (!summary)
		return NULL;
	if (pos < 0 || pos >= summary->n)
		isl_die(summary->ctx, isl_error_invalid,
			"position out of bounds", return NULL);
	if (summary->arg[pos].type != pet_arg_array)
		isl_die(summary->ctx, isl_error_invalid,
			"not an array argument", return NULL);

	return isl_union_map_copy(summary->arg[pos].access[type]);
}

/* Print "summary" to "p" in YAML format.
 */
__isl_give isl_printer *pet_function_summary_print(
	__isl_keep pet_function_summary *summary, __isl_take isl_printer *p)
{
	int i;

	if (!summary || !p)
		return isl_printer_free(p);
	p = isl_printer_yaml_start_sequence(p);
	for (i = 0; i < summary->n; ++i) {
		switch (summary->arg[i].type) {
		case pet_arg_int:
			p = isl_printer_yaml_start_mapping(p);
			p = isl_printer_print_str(p, "id");
			p = isl_printer_yaml_next(p);
			p = isl_printer_print_id(p, summary->arg[i].id);
			p = isl_printer_yaml_next(p);
			p = isl_printer_yaml_end_mapping(p);
			break;
		case pet_arg_other:
			p = isl_printer_print_str(p, "other");
			break;
		case pet_arg_array:
			p = isl_printer_yaml_start_mapping(p);
			p = isl_printer_print_str(p, "may_read");
			p = isl_printer_yaml_next(p);
			p = isl_printer_print_union_map(p,
			    summary->arg[i].access[pet_expr_access_may_read]);
			p = isl_printer_yaml_next(p);
			p = isl_printer_print_str(p, "may_write");
			p = isl_printer_yaml_next(p);
			p = isl_printer_print_union_map(p,
			    summary->arg[i].access[pet_expr_access_may_write]);
			p = isl_printer_yaml_next(p);
			p = isl_printer_print_str(p, "must_write");
			p = isl_printer_yaml_next(p);
			p = isl_printer_print_union_map(p,
			    summary->arg[i].access[pet_expr_access_must_write]);
			p = isl_printer_yaml_next(p);
			p = isl_printer_yaml_end_mapping(p);
			break;
		}
	}
	p = isl_printer_yaml_end_sequence(p);

	return p;
}

/* Dump "summary" to stderr.
 */
void pet_function_summary_dump(__isl_keep pet_function_summary *summary)
{
	isl_printer *p;

	if (!summary)
		return;

	p = isl_printer_to_file(pet_function_summary_get_ctx(summary), stderr);
	p = isl_printer_set_yaml_style(p, ISL_YAML_STYLE_BLOCK);
	p = pet_function_summary_print(summary, p);

	isl_printer_free(p);
}
