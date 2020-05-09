/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2013-2014 Ecole Normale Superieure. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY LEIDEN UNIVERSITY ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LEIDEN UNIVERSITY OR
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
 * Leiden University.
 */ 

#include <stdlib.h>
#include <yaml.h>

#include <isl/ctx.h>
#include <isl/id.h>
#include <isl/val.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_set.h>
#include <isl/union_map.h>

#include "expr.h"
#include "loc.h"
#include "scop.h"
#include "scop_yaml.h"
#include "tree.h"

static char *extract_string(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return strdup((char *) node->data.scalar.value);
}

static int extract_int(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return -1);

	return atoi((char *) node->data.scalar.value);
}

static double extract_double(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return -1);

	return strtod((char *) node->data.scalar.value, NULL);
}

static enum pet_expr_type extract_expr_type(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return -1);

	return pet_str_type((char *) node->data.scalar.value);
}

static enum pet_op_type extract_op(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return -1);

	return pet_str_op((char *) node->data.scalar.value);
}

static __isl_give isl_set *extract_set(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_set_read_from_str(ctx, (char *) node->data.scalar.value);
}

static __isl_give isl_id *extract_id(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_id_alloc(ctx, (char *) node->data.scalar.value, NULL);
}

static __isl_give isl_map *extract_map(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_map_read_from_str(ctx, (char *) node->data.scalar.value);
}

/* Extract an isl_union_set from "node".
 */
static __isl_give isl_union_set *extract_union_set(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_union_set_read_from_str(ctx,
					    (char *) node->data.scalar.value);
}

/* Extract an isl_union_map from "node".
 */
static __isl_give isl_union_map *extract_union_map(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_union_map_read_from_str(ctx,
					    (char *) node->data.scalar.value);
}

/* Extract an isl_val from "node".
 */
static __isl_give isl_val *extract_val(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_val_read_from_str(ctx, (char *) node->data.scalar.value);
}

/* Extract an isl_multi_pw_aff from "node".
 */
static __isl_give isl_multi_pw_aff *extract_multi_pw_aff(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_multi_pw_aff_read_from_str(ctx,
					    (char *) node->data.scalar.value);
}

/* Extract an isl_schedule from "node".
 */
static __isl_give isl_schedule *extract_schedule(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return NULL);

	return isl_schedule_read_from_str(ctx,
					    (char *) node->data.scalar.value);
}

/* Extract a pet_type from "node".
 */
static struct pet_type *extract_type(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	struct pet_type *type;
	yaml_node_pair_t * pair;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	type = isl_calloc_type(ctx, struct pet_type);
	if (!type)
		return NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_type_free(type));

		if (!strcmp((char *) key->data.scalar.value, "name"))
			type->name = extract_string(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "definition"))
			type->definition = extract_string(ctx, document, value);
	}

	return type;
}

/* Extract a sequence of types from "node" and store them in scop->types.
 */
static struct pet_scop *extract_types(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, struct pet_scop *scop)
{
	int i;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return NULL);

	scop->n_type = node->data.sequence.items.top
				- node->data.sequence.items.start;
	scop->types = isl_calloc_array(ctx, struct pet_type *, scop->n_type);
	if (!scop->types)
		return pet_scop_free(scop);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;

		n = yaml_document_get_node(document, *item);
		scop->types[i] = extract_type(ctx, document, n);
		if (!scop->types[i])
			return pet_scop_free(scop);
	}

	return scop;
}

static struct pet_array *extract_array(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	struct pet_array *array;
	yaml_node_pair_t * pair;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	array = isl_calloc_type(ctx, struct pet_array);
	if (!array)
		return NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_array_free(array));

		if (!strcmp((char *) key->data.scalar.value, "context"))
			array->context = extract_set(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "extent"))
			array->extent = extract_set(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "value_bounds"))
			array->value_bounds = extract_set(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "element_type"))
			array->element_type =
					extract_string(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "element_size"))
			array->element_size = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value,
				"element_is_record"))
			array->element_is_record =
					extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "live_out"))
			array->live_out = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value,
				"uniquely_defined"))
			array->uniquely_defined =
					extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "declared"))
			array->declared = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "exposed"))
			array->exposed = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "outer"))
			array->outer = extract_int(ctx, document, value);
	}

	return array;
}

static struct pet_scop *extract_arrays(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node, struct pet_scop *scop)
{
	int i;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return NULL);

	scop->n_array = node->data.sequence.items.top
				- node->data.sequence.items.start;
	scop->arrays = isl_calloc_array(ctx, struct pet_array *, scop->n_array);
	if (!scop->arrays)
		return pet_scop_free(scop);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;

		n = yaml_document_get_node(document, *item);
		scop->arrays[i] = extract_array(ctx, document, n);
		if (!scop->arrays[i])
			return pet_scop_free(scop);
	}

	return scop;
}

static __isl_give pet_expr *extract_expr(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node);

static __isl_give pet_expr *extract_arguments(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	int i, n;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return pet_expr_free(expr));

	n = node->data.sequence.items.top - node->data.sequence.items.start;
	expr = pet_expr_set_n_arg(expr, n);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;
		pet_expr *arg;

		n = yaml_document_get_node(document, *item);
		arg = extract_expr(ctx, document, n);
		expr = pet_expr_set_arg(expr, i, arg);
	}

	return expr;
}

/* Extract pet_expr_double specific fields from "node" and
 * update "expr" accordingly.
 */
static __isl_give pet_expr *extract_expr_double(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	yaml_node_pair_t *pair;
	double d = 0;
	char *s = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "value"))
			d = extract_double(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "string"))
			s = extract_string(ctx, document, value);
	}

	expr = pet_expr_double_set(expr, d, s);
	free(s);

	return expr;
}

/* Extract pet_expr_access specific fields from "node" and
 * update "expr" accordingly.
 *
 * The depth of the access is initialized by pet_expr_access_set_index.
 * Any explicitly specified depth therefore needs to be set after
 * setting the index expression.  Similiarly, the access relations (if any)
 * need to be set after setting the depth.
 */
static __isl_give pet_expr *extract_expr_access(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	yaml_node_pair_t *pair;
	int depth = -1;
	isl_multi_pw_aff *index = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "index"))
			index = extract_multi_pw_aff(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "depth"))
			depth = extract_int(ctx, document, value);
	}

	expr = pet_expr_access_set_index(expr, index);
	if (depth >= 0)
		expr = pet_expr_access_set_depth(expr, depth);

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "may_read"))
			expr = pet_expr_access_set_access(expr,
				    pet_expr_access_may_read,
				    extract_union_map(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "may_write"))
			expr = pet_expr_access_set_access(expr,
				    pet_expr_access_may_write,
				    extract_union_map(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "must_write"))
			expr = pet_expr_access_set_access(expr,
				    pet_expr_access_must_write,
				    extract_union_map(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "killed"))
			expr = pet_expr_access_set_access(expr,
				    pet_expr_access_killed,
				    extract_union_map(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "reference"))
			expr = pet_expr_access_set_ref_id(expr,
				    extract_id(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "read"))
			expr = pet_expr_access_set_read(expr,
				    extract_int(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "write"))
			expr = pet_expr_access_set_write(expr,
				    extract_int(ctx, document, value));
		if (!strcmp((char *) key->data.scalar.value, "kill"))
			expr = pet_expr_access_set_kill(expr,
				    extract_int(ctx, document, value));
	}

	return expr;
}

/* Extract operation expression specific fields from "node" and
 * update "expr" accordingly.
 */
static __isl_give pet_expr *extract_expr_op(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	yaml_node_pair_t *pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "operation"))
			expr = pet_expr_op_set_type(expr,
					    extract_op(ctx, document, value));
	}

	return expr;
}

/* Extract pet_expr_call specific fields from "node" and
 * update "expr" accordingly.
 */
static __isl_give pet_expr *extract_expr_call(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	yaml_node_pair_t *pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "name"))
			expr = pet_expr_call_set_name(expr,
					extract_string(ctx, document, value));
	}

	return expr;
}

/* Extract pet_expr_cast specific fields from "node" and
 * update "expr" accordingly.
 */
static __isl_give pet_expr *extract_expr_cast(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	yaml_node_pair_t *pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "type_name"))
			expr = pet_expr_cast_set_type_name(expr,
					extract_string(ctx, document, value));
	}

	return expr;
}

/* Extract pet_expr_int specific fields from "node" and
 * update "expr" accordingly.
 */
static __isl_give pet_expr *extract_expr_int(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, __isl_take pet_expr *expr)
{
	yaml_node_pair_t * pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_expr_free(expr));

		if (!strcmp((char *) key->data.scalar.value, "value"))
			expr = pet_expr_int_set_val(expr,
					    extract_val(ctx, document, value));
	}

	return expr;
}

/* Extract a pet_expr from "node".
 *
 * We first extract the type and arguments of the expression and
 * then extract additional fields depending on the type.
 */
static __isl_give pet_expr *extract_expr(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	enum pet_expr_type type = pet_expr_error;
	pet_expr *expr;
	yaml_node_pair_t *pair;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "type"))
			type = extract_expr_type(ctx, document, value);
	}

	if (type == pet_expr_error)
		isl_die(ctx, isl_error_invalid, "cannot determine type",
			return NULL);

	expr = pet_expr_alloc(ctx, type);
	if (!expr)
		return NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (!strcmp((char *) key->data.scalar.value, "arguments"))
			expr = extract_arguments(ctx, document, value, expr);
		if (!expr)
			return NULL;
	}

	switch (type) {
	case pet_expr_error:
		isl_die(ctx, isl_error_internal, "unreachable code",
			return NULL);
	case pet_expr_access:
		expr = extract_expr_access(ctx, document, node, expr);
		break;
	case pet_expr_double:
		expr = extract_expr_double(ctx, document, node, expr);
		break;
	case pet_expr_call:
		expr = extract_expr_call(ctx, document, node, expr);
		break;
	case pet_expr_cast:
		expr = extract_expr_cast(ctx, document, node, expr);
		break;
	case pet_expr_int:
		expr = extract_expr_int(ctx, document, node, expr);
		break;
	case pet_expr_op:
		expr = extract_expr_op(ctx, document, node, expr);
		break;
	}

	return expr;
}

/* Extract a pet_tree_type from "node".
 */
static enum pet_tree_type extract_tree_type(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE)
		isl_die(ctx, isl_error_invalid, "expecting scalar node",
			return -1);

	return pet_tree_str_type((char *) node->data.scalar.value);
}

static __isl_give pet_tree *extract_tree(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node);

/* Extract a pet_tree of type pet_tree_block from "node".
 */
static __isl_give pet_tree *extract_tree_block(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	int block = 0;
	int i, n;
	yaml_node_pair_t *pair;
	yaml_node_item_t *item;
	yaml_node_t *children = NULL;
	pet_tree *tree;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "block"))
			block = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "children"))
			children = value;
	}

	if (!children)
		n = 0;
	else
		n = children->data.sequence.items.top -
					    children->data.sequence.items.start;

	tree = pet_tree_new_block(ctx, block, n);
	if (!children)
		return tree;

	for (item = children->data.sequence.items.start, i = 0;
	     item < children->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;
		pet_tree *child;

		n = yaml_document_get_node(document, *item);
		child = extract_tree(ctx, document, n);
		tree = pet_tree_block_add_child(tree, child);
	}

	return tree;
}

/* Extract a pet_tree of type pet_tree_decl from "node".
 */
static __isl_give pet_tree *extract_tree_decl(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_expr *var = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "variable")) {
			var = extract_expr(ctx, document, value);
			if (!var)
				return NULL;
		}
	}

	if (!var)
		isl_die(ctx, isl_error_invalid,
			"no variable field", return NULL);

	return pet_tree_new_decl(var);
}

/* Extract a pet_tree of type pet_tree_decl_init from "node".
 */
static __isl_give pet_tree *extract_tree_decl_init(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_expr *var = NULL;
	pet_expr *init = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "variable")) {
			var = extract_expr(ctx, document, value);
			if (!var)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value,
							"initialization")) {
			init = extract_expr(ctx, document, value);
			if (!init)
				goto error;
		}
	}

	if (!var)
		isl_die(ctx, isl_error_invalid,
			"no variable field", goto error);
	if (!init)
		isl_die(ctx, isl_error_invalid,
			"no initialization field", goto error);

	return pet_tree_new_decl_init(var, init);
error:
	pet_expr_free(var);
	pet_expr_free(init);
	return NULL;
}

/* Extract a pet_expr corresponding to a pet_tree with a single "expr" field
 * from "node".
 */
static __isl_give pet_expr *extract_expr_field(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_expr *expr = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "expr")) {
			expr = extract_expr(ctx, document, value);
			if (!expr)
				return NULL;
		}
	}

	if (!expr)
		isl_die(ctx, isl_error_invalid,
			"no expr field", return NULL);

	return expr;
}

/* Extract a pet_tree of type pet_tree_expr from "node".
 */
static __isl_give pet_tree *extract_tree_expr(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	return pet_tree_new_expr(extract_expr_field(ctx, document, node));
}

/* Extract a pet_tree of type pet_tree_return from "node".
 */
static __isl_give pet_tree *extract_tree_return(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	return pet_tree_new_return(extract_expr_field(ctx, document, node));
}

/* Extract a pet_tree of type pet_tree_while from "node".
 */
static __isl_give pet_tree *extract_tree_while(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_expr *cond = NULL;
	pet_tree *body = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "condition")) {
			cond = extract_expr(ctx, document, value);
			if (!cond)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "body")) {
			body = extract_tree(ctx, document, value);
			if (!body)
				goto error;
		}
	}

	if (!cond)
		isl_die(ctx, isl_error_invalid,
			"no condition field", goto error);
	if (!body)
		isl_die(ctx, isl_error_invalid,
			"no body field", goto error);

	return pet_tree_new_while(cond, body);
error:
	pet_expr_free(cond);
	pet_tree_free(body);
	return NULL;
}

/* Extract a pet_tree of type pet_tree_infinite_loop from "node".
 */
static __isl_give pet_tree *extract_tree_infinite_loop(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_tree *body;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "body")) {
			body = extract_tree(ctx, document, value);
			if (!body)
				return NULL;
		}
	}

	if (!body)
		isl_die(ctx, isl_error_invalid,
			"no body field", return NULL);

	return pet_tree_new_infinite_loop(body);
}

/* Extract a pet_tree of type pet_tree_if from "node".
 */
static __isl_give pet_tree *extract_tree_if(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_expr *cond = NULL;
	pet_tree *then_body = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "condition")) {
			cond = extract_expr(ctx, document, value);
			if (!cond)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "then")) {
			then_body = extract_tree(ctx, document, value);
			if (!then_body)
				goto error;
		}
	}

	if (!cond)
		isl_die(ctx, isl_error_invalid,
			"no condition field", goto error);
	if (!then_body)
		isl_die(ctx, isl_error_invalid,
			"no then body", goto error);

	return pet_tree_new_if(cond, then_body);
error:
	pet_expr_free(cond);
	pet_tree_free(then_body);
	return NULL;
}

/* Extract a pet_tree of type pet_tree_if_else from "node".
 */
static __isl_give pet_tree *extract_tree_if_else(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	pet_expr *cond = NULL;
	pet_tree *then_body = NULL;
	pet_tree *else_body = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "condition")) {
			cond = extract_expr(ctx, document, value);
			if (!cond)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "then")) {
			then_body = extract_tree(ctx, document, value);
			if (!then_body)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "else")) {
			else_body = extract_tree(ctx, document, value);
			if (!else_body)
				goto error;
		}
	}

	if (!cond)
		isl_die(ctx, isl_error_invalid,
			"no condition field", goto error);
	if (!then_body)
		isl_die(ctx, isl_error_invalid,
			"no then body", goto error);
	if (!else_body)
		isl_die(ctx, isl_error_invalid,
			"no else body", goto error);

	return pet_tree_new_if_else(cond, then_body, else_body);
error:
	pet_expr_free(cond);
	pet_tree_free(then_body);
	pet_tree_free(else_body);
	return NULL;
}

/* Extract a pet_tree of type pet_tree_for from "node".
 */
static __isl_give pet_tree *extract_tree_for(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	yaml_node_pair_t *pair;
	int declared = 0;
	int independent = 0;
	pet_expr *iv = NULL;
	pet_expr *init = NULL;
	pet_expr *cond = NULL;
	pet_expr *inc = NULL;
	pet_tree *body = NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "declared"))
			declared = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "independent"))
			independent = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "variable")) {
			iv = extract_expr(ctx, document, value);
			if (!iv)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value,
							"initialization")) {
			init = extract_expr(ctx, document, value);
			if (!init)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "condition")) {
			cond = extract_expr(ctx, document, value);
			if (!cond)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "increment")) {
			inc = extract_expr(ctx, document, value);
			if (!inc)
				goto error;
		}
		if (!strcmp((char *) key->data.scalar.value, "body")) {
			body = extract_tree(ctx, document, value);
			if (!body)
				goto error;
		}
	}

	if (!iv)
		isl_die(ctx, isl_error_invalid,
			"no variable field", goto error);
	if (!init)
		isl_die(ctx, isl_error_invalid,
			"no initialization field", goto error);
	if (!cond)
		isl_die(ctx, isl_error_invalid,
			"no condition field", goto error);
	if (!inc)
		isl_die(ctx, isl_error_invalid,
			"no increment field", goto error);
	if (!body)
		isl_die(ctx, isl_error_invalid,
			"no body field", goto error);

	return pet_tree_new_for(independent, declared, iv, init, cond, inc,
				body);
error:
	pet_expr_free(iv);
	pet_expr_free(init);
	pet_expr_free(cond);
	pet_expr_free(inc);
	pet_tree_free(body);
	return NULL;
}

/* Extract a pet_tree from "node".
 *
 * We first extract the type of the pet_tree and then call
 * the appropriate function to extract and construct a pet_tree
 * of that type.
 */
static __isl_give pet_tree *extract_tree(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	enum pet_tree_type type = pet_tree_error;
	pet_tree *tree;
	yaml_node_pair_t *pair;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return NULL);

		if (!strcmp((char *) key->data.scalar.value, "type"))
			type = extract_tree_type(ctx, document, value);
	}

	if (type == pet_tree_error)
		isl_die(ctx, isl_error_invalid, "cannot determine type",
			return NULL);

	switch (type) {
	case pet_tree_error:
		return NULL;
	case pet_tree_block:
		tree = extract_tree_block(ctx, document, node);
		break;
	case pet_tree_break:
		tree = pet_tree_new_break(ctx);
		break;
	case pet_tree_continue:
		tree = pet_tree_new_continue(ctx);
		break;
	case pet_tree_decl:
		tree = extract_tree_decl(ctx, document, node);
		break;
	case pet_tree_decl_init:
		tree = extract_tree_decl_init(ctx, document, node);
		break;
	case pet_tree_expr:
		tree = extract_tree_expr(ctx, document, node);
		break;
	case pet_tree_return:
		tree = extract_tree_return(ctx, document, node);
		break;
	case pet_tree_for:
		tree = extract_tree_for(ctx, document, node);
		break;
	case pet_tree_while:
		tree = extract_tree_while(ctx, document, node);
		break;
	case pet_tree_infinite_loop:
		tree = extract_tree_infinite_loop(ctx, document, node);
		break;
	case pet_tree_if:
		tree = extract_tree_if(ctx, document, node);
		break;
	case pet_tree_if_else:
		tree = extract_tree_if_else(ctx, document, node);
		break;
	}

	return tree;
}

static struct pet_stmt *extract_stmt_arguments(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, struct pet_stmt *stmt)
{
	int i;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return pet_stmt_free(stmt));

	stmt->n_arg = node->data.sequence.items.top
				- node->data.sequence.items.start;
	stmt->args = isl_calloc_array(ctx, pet_expr *, stmt->n_arg);
	if (!stmt->args)
		return pet_stmt_free(stmt);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;

		n = yaml_document_get_node(document, *item);
		stmt->args[i] = extract_expr(ctx, document, n);
		if (!stmt->args[i])
			return pet_stmt_free(stmt);
	}

	return stmt;
}

static struct pet_stmt *extract_stmt(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	struct pet_stmt *stmt;
	yaml_node_pair_t * pair;
	int line = -1;
	unsigned start = 0, end = 0;
	char *indent = NULL;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	stmt = isl_calloc_type(ctx, struct pet_stmt);
	if (!stmt)
		return NULL;

	stmt->loc = &pet_loc_dummy;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_stmt_free(stmt));

		if (!strcmp((char *) key->data.scalar.value, "indent"))
			indent = extract_string(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "line"))
			line = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "start"))
			start = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "end"))
			end = extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "domain"))
			stmt->domain = extract_set(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "body"))
			stmt->body = extract_tree(ctx, document, value);

		if (!strcmp((char *) key->data.scalar.value, "arguments"))
			stmt = extract_stmt_arguments(ctx, document,
							value, stmt);
		if (!stmt)
			return NULL;
	}

	if (!indent)
		indent = strdup("");
	stmt->loc = pet_loc_alloc(ctx, start, end, line, indent);
	if (!stmt->loc)
		return pet_stmt_free(stmt);

	return stmt;
}

static struct pet_scop *extract_statements(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, struct pet_scop *scop)
{
	int i;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return NULL);

	scop->n_stmt = node->data.sequence.items.top
				- node->data.sequence.items.start;
	scop->stmts = isl_calloc_array(ctx, struct pet_stmt *, scop->n_stmt);
	if (!scop->stmts)
		return pet_scop_free(scop);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;

		n = yaml_document_get_node(document, *item);
		scop->stmts[i] = extract_stmt(ctx, document, n);
		if (!scop->stmts[i])
			return pet_scop_free(scop);
	}

	return scop;
}

/* Extract a pet_implication from "node".
 */
static struct pet_implication *extract_implication(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	struct pet_implication *implication;
	yaml_node_pair_t * pair;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	implication = isl_calloc_type(ctx, struct pet_implication);
	if (!implication)
		return NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_implication_free(implication));

		if (!strcmp((char *) key->data.scalar.value, "satisfied"))
			implication->satisfied =
					    extract_int(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "extension"))
			implication->extension =
					    extract_map(ctx, document, value);
	}

	return implication;
}

/* Extract a sequence of implications from "node" and
 * store them in scop->implications.
 */
static struct pet_scop *extract_implications(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, struct pet_scop *scop)
{
	int i;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return NULL);

	scop->n_implication = node->data.sequence.items.top
				- node->data.sequence.items.start;
	scop->implications = isl_calloc_array(ctx, struct pet_implication *,
						scop->n_implication);
	if (!scop->implications)
		return pet_scop_free(scop);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;

		n = yaml_document_get_node(document, *item);
		scop->implications[i] = extract_implication(ctx, document, n);
		if (!scop->implications[i])
			return pet_scop_free(scop);
	}

	return scop;
}

/* Extract a pet_independence from "node".
 */
static struct pet_independence *extract_independence(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node)
{
	struct pet_independence *independence;
	yaml_node_pair_t * pair;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	independence = isl_calloc_type(ctx, struct pet_independence);
	if (!independence)
		return NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_independence_free(independence));

		if (!strcmp((char *) key->data.scalar.value, "filter"))
			independence->filter =
					extract_union_map(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "local"))
			independence->local =
					extract_union_set(ctx, document, value);
	}

	if (!independence->filter)
		isl_die(ctx, isl_error_invalid, "no filter field",
			return pet_independence_free(independence));
	if (!independence->local)
		isl_die(ctx, isl_error_invalid, "no local field",
			return pet_independence_free(independence));

	return independence;
}

/* Extract a sequence of independences from "node" and
 * store them in scop->independences.
 */
static struct pet_scop *extract_independences(isl_ctx *ctx,
	yaml_document_t *document, yaml_node_t *node, struct pet_scop *scop)
{
	int i;
	yaml_node_item_t *item;

	if (node->type != YAML_SEQUENCE_NODE)
		isl_die(ctx, isl_error_invalid, "expecting sequence",
			return NULL);

	scop->n_independence = node->data.sequence.items.top
				- node->data.sequence.items.start;
	scop->independences = isl_calloc_array(ctx, struct pet_independence *,
						scop->n_independence);
	if (!scop->independences)
		return pet_scop_free(scop);

	for (item = node->data.sequence.items.start, i = 0;
	     item < node->data.sequence.items.top; ++item, ++i) {
		yaml_node_t *n;

		n = yaml_document_get_node(document, *item);
		scop->independences[i] = extract_independence(ctx, document, n);
		if (!scop->independences[i])
			return pet_scop_free(scop);
	}

	return scop;
}

static struct pet_scop *extract_scop(isl_ctx *ctx, yaml_document_t *document,
	yaml_node_t *node)
{
	struct pet_scop *scop;
	yaml_node_pair_t * pair;

	if (!node)
		return NULL;

	if (node->type != YAML_MAPPING_NODE)
		isl_die(ctx, isl_error_invalid, "expecting mapping",
			return NULL);

	scop = pet_scop_alloc(ctx);
	if (!scop)
		return NULL;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		yaml_node_t *key, *value;

		key = yaml_document_get_node(document, pair->key);
		value = yaml_document_get_node(document, pair->value);

		if (key->type != YAML_SCALAR_NODE)
			isl_die(ctx, isl_error_invalid, "expecting scalar key",
				return pet_scop_free(scop));
		if (!strcmp((char *) key->data.scalar.value, "context"))
			scop->context = extract_set(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "context_value"))
			scop->context_value = extract_set(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "schedule"))
			scop->schedule = extract_schedule(ctx, document, value);
		if (!strcmp((char *) key->data.scalar.value, "types"))
			scop = extract_types(ctx, document, value, scop);
		if (!strcmp((char *) key->data.scalar.value, "arrays"))
			scop = extract_arrays(ctx, document, value, scop);
		if (!strcmp((char *) key->data.scalar.value, "statements"))
			scop = extract_statements(ctx, document, value, scop);
		if (!strcmp((char *) key->data.scalar.value, "implications"))
			scop = extract_implications(ctx, document, value, scop);
		if (!strcmp((char *) key->data.scalar.value, "independences"))
			scop = extract_independences(ctx,
							document, value, scop);
		if (!scop)
			return NULL;
	}

	if (!scop->context_value) {
		isl_space *space = isl_space_params_alloc(ctx, 0);
		scop->context_value = isl_set_universe(space);
		if (!scop->context_value)
			return pet_scop_free(scop);
	}

	return scop;
}

/* Extract a pet_scop from the YAML description in "in".
 */
struct pet_scop *pet_scop_parse(isl_ctx *ctx, FILE *in)
{
	struct pet_scop *scop = NULL;
	yaml_parser_t parser;
	yaml_node_t *root;
	yaml_document_t document = { 0 };

	yaml_parser_initialize(&parser);

	yaml_parser_set_input_file(&parser, in);

	if (!yaml_parser_load(&parser, &document))
		goto error;

	root = yaml_document_get_root_node(&document);

	scop = extract_scop(ctx, &document, root);

	yaml_document_delete(&document);

	yaml_parser_delete(&parser);

	return scop;
error:
	yaml_parser_delete(&parser);
	pet_scop_free(scop);
	return NULL;
}
