#ifndef _AUTOSA_SCHEDULE_TREE_H
#define _AUTOSA_SCHEDULE_TREE_H

#include <isl/schedule_node.h>

int autosa_tree_node_is_kernel(__isl_keep isl_schedule_node *node);
int autosa_tree_node_is_mark(__isl_keep isl_schedule_node *node, const char *mark);

__isl_give isl_schedule_node *autosa_tree_move_down_to_depth(
    __isl_take isl_schedule_node *node, int depth,
    __isl_keep isl_union_set *core);
__isl_give isl_schedule_node *autosa_tree_move_down_to_array(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core);
__isl_give isl_schedule_node *autosa_tree_move_up_to_array(
    __isl_take isl_schedule_node *node);
__isl_give isl_schedule_node *autosa_tree_move_down_to_local(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core);
__isl_give isl_schedule_node *autosa_tree_move_down_to_kernel(
    __isl_take isl_schedule_node *node);
__isl_give isl_schedule_node *autosa_tree_move_up_to_kernel(
    __isl_take isl_schedule_node *node);
__isl_give isl_schedule_node *autosa_tree_move_down_to_pe(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core);
__isl_give isl_schedule_node *autosa_tree_move_up_to_pe(
    __isl_take isl_schedule_node *node);
__isl_give isl_schedule_node *autosa_tree_move_down_to_mark(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core, const char *mark);
__isl_give isl_schedule_node *autosa_tree_move_up_to_mark(
    __isl_take isl_schedule_node *node, const char *mark);
__isl_give isl_schedule_node *autosa_tree_move_down_to_first_io_mark(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core);
__isl_give isl_schedule_node *autosa_tree_move_down_to_io_mark(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core, int io_level);
__isl_give isl_schedule_node *autosa_tree_move_up_to_anchor(
    __isl_take isl_schedule_node *node);

__isl_give isl_schedule_node *autosa_tree_insert_local_before_array(
    __isl_take isl_schedule_node *node);

#endif
