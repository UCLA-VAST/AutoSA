/*
 * Copyright 2013      Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d'Ulm, 75230 Paris, France
 */

#include <string.h>

#include <isl/union_set.h>

#include "gpu_tree.h"

/* The functions in this file are used to navigate part of a schedule tree
 * that is mapped to blocks.  Initially, this part consists of a linear
 * branch segment with a mark node with name "kernel" on the outer end
 * and a mark node with name "thread" on the inner end.
 * During the mapping to blocks, branching may be introduced, but only
 * one of the elements in each sequence contains the "thread" mark.
 * The filter of this element (and only this filter) contains
 * domain elements identified by the "core" argument of the functions
 * that move down this tree.
 */

/* Is "node" a mark node with an identifier called "name"?
 */
static int is_marked(__isl_keep isl_schedule_node *node, const char *name)
{
	isl_id *mark;
	int has_name;

	if (!node)
		return -1;

	if (isl_schedule_node_get_type(node) != isl_schedule_node_mark)
		return 0;

	mark = isl_schedule_node_mark_get_id(node);
	if (!mark)
		return -1;

	has_name = !strcmp(isl_id_get_name(mark), name);
	isl_id_free(mark);

	return has_name;
}

/* Is "node" a mark node with an identifier called "kernel"?
 */
int gpu_tree_node_is_kernel(__isl_keep isl_schedule_node *node)
{
	return is_marked(node, "kernel");
}

/* Is "node" a mark node with an identifier called "thread"?
 */
static int node_is_thread(__isl_keep isl_schedule_node *node)
{
	return is_marked(node, "thread");
}

/* Assuming "node" is a filter node, does it correspond to the branch
 * that contains the "thread" mark, i.e., does it contain any elements
 * in "core"?
 */
static int node_is_core(__isl_keep isl_schedule_node *node,
	__isl_keep isl_union_set *core)
{
	int disjoint;
	isl_union_set *filter;

	filter = isl_schedule_node_filter_get_filter(node);
	disjoint = isl_union_set_is_disjoint(filter, core);
	isl_union_set_free(filter);
	if (disjoint < 0)
		return -1;

	return !disjoint;
}

/* Move to the only child of "node" that has the "thread" mark as descendant,
 * where the branch containing this mark is identified by the domain elements
 * in "core".
 *
 * If "node" is not a sequence, then it only has one child and we move
 * to that single child.
 * Otherwise, we check each of the filters in the children, pick
 * the one that corresponds to "core" and return a pointer to the child
 * of the filter node.
 */
static __isl_give isl_schedule_node *core_child(
	__isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
	int i, n;

	if (isl_schedule_node_get_type(node) != isl_schedule_node_sequence)
		return isl_schedule_node_child(node, 0);

	n = isl_schedule_node_n_children(node);
	for (i = 0; i < n; ++i) {
		int is_core;

		node = isl_schedule_node_child(node, i);
		is_core = node_is_core(node, core);

		if (is_core < 0)
			return isl_schedule_node_free(node);
		if (is_core)
			return isl_schedule_node_child(node, 0);

		node = isl_schedule_node_parent(node);
	}

	isl_die(isl_schedule_node_get_ctx(node), isl_error_internal,
		"core child not found", return isl_schedule_node_free(node));
}

/* Move down the branch between "kernel" and "thread" until
 * the "thread" mark is reached, where the branch containing the "thread"
 * mark is identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *gpu_tree_move_down_to_thread(
	__isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
	int is_thread;

	while ((is_thread = node_is_thread(node)) == 0)
		node = core_child(node, core);
	if (is_thread < 0)
		node = isl_schedule_node_free(node);

	return node;
}

/* Move up the tree underneath the "thread" mark until
 * the "thread" mark is reached.
 */
__isl_give isl_schedule_node *gpu_tree_move_up_to_thread(
	__isl_take isl_schedule_node *node)
{
	int is_thread;

	while ((is_thread = node_is_thread(node)) == 0)
		node = isl_schedule_node_parent(node);
	if (is_thread < 0)
		node = isl_schedule_node_free(node);

	return node;
}

/* Move up the tree underneath the "kernel" mark until
 * the "kernel" mark is reached.
 */
__isl_give isl_schedule_node *gpu_tree_move_up_to_kernel(
	__isl_take isl_schedule_node *node)
{
	int is_kernel;

	while ((is_kernel = gpu_tree_node_is_kernel(node)) == 0)
		node = isl_schedule_node_parent(node);
	if (is_kernel < 0)
		node = isl_schedule_node_free(node);

	return node;
}

/* Move down from the "kernel" mark (or at least a node with schedule
 * depth smaller than or equal to "depth") to a band node at schedule
 * depth "depth".  The "thread" mark is assumed to have a schedule
 * depth greater than or equal to "depth".  The branch containing the
 * "thread" mark is identified by the domain elements in "core".
 *
 * If the desired schedule depth is in the middle of band node,
 * then the band node is split into two pieces, the second piece
 * at the desired schedule depth.
 */
__isl_give isl_schedule_node *gpu_tree_move_down_to_depth(
	__isl_take isl_schedule_node *node, int depth,
	__isl_keep isl_union_set *core)
{
	int is_thread;

	while (node && isl_schedule_node_get_schedule_depth(node) < depth) {
		if (isl_schedule_node_get_type(node) ==
						    isl_schedule_node_band) {
			int node_depth, node_dim;
			node_depth = isl_schedule_node_get_schedule_depth(node);
			node_dim = isl_schedule_node_band_n_member(node);
			if (node_depth + node_dim > depth)
				node = isl_schedule_node_band_split(node,
							depth - node_depth);
		}
		node = core_child(node, core);
	}
	while ((is_thread = node_is_thread(node)) == 0 &&
	    isl_schedule_node_get_type(node) != isl_schedule_node_band)
		node = core_child(node, core);
	if (is_thread < 0)
		node = isl_schedule_node_free(node);

	return node;
}
