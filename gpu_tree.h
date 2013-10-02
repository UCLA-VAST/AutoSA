#ifndef GPU_TREE_H
#define GPU_TREE_H

#include <isl/schedule_node.h>

int gpu_tree_node_is_kernel(__isl_keep isl_schedule_node *node);
__isl_give isl_schedule_node *gpu_tree_move_up_to_thread(
	__isl_take isl_schedule_node *node);
__isl_give isl_schedule_node *gpu_tree_move_down_to_thread(
	__isl_take isl_schedule_node *node, __isl_keep isl_union_set *core);
__isl_give isl_schedule_node *gpu_tree_move_up_to_kernel(
	__isl_take isl_schedule_node *node);
__isl_give isl_schedule_node *gpu_tree_move_down_to_depth(
	__isl_take isl_schedule_node *node, int depth,
	__isl_keep isl_union_set *core);

#endif
