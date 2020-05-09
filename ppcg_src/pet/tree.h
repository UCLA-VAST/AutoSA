#ifndef PET_TREE_H
#define PET_TREE_H

#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* A pet_tree represents an AST.
 *
 * "loc" is the location of the code corresponding to the tree
 * in the input file.
 * "label" is the label (if any) that precedes the AST and may be NULL.
 *
 * The "b" field of the union is used for type pet_tree_block.
 * "block" is set if the block has its own scope.
 * "n" is the number of children in "child".
 * "max" is the capacity of the child array "child".
 *
 * The "d" field of the union is used for types pet_tree_decl
 * and pet_tree_decl_init.
 * "var" is the variable that is being declared.
 * "init" is the initial value (in case of pet_tree_decl_init).
 *
 * The "e" field of the union is used for type pet_tree_expr and
 * for type pet_tree_return.
 * "expr" is the expression represented or returned by the tree.
 *
 * The "l" field of the union is used for types pet_tree_for,
 * pet_tree_infinite_loop and pet_tree_while.
 * "body" represents the body of the loop.
 * "cond" is the loop condition (for pet_tree_for and pet_tree_while).
 * The remaining fields are only used for pet_tree_for.
 * "iv" is the induction variable of the for loop.
 " "declared" is set if this induction variable is declared by the loop.
 * "init" is the initial value of the induction variable.
 * "inc" is the increment to the induction variable.
 * "independent" is set if the for loop is marked independent.
 *
 * The "i" field of the union is used for types pet_tree_if
 * and pet_tree_if_else.
 * "cond" is the if condition.
 * "then_body" represents the then branch of the if statement.
 * "else_body" represents the else branch of the if statement
 * (in case of pet_tree_if_else).
 */
struct pet_tree {
	int ref;
	isl_ctx *ctx;

	pet_loc *loc;
	isl_id *label;

	enum pet_tree_type type;

	union {
		struct {
			int block;
			int n;
			int max;
			pet_tree **child;
		} b;
		struct {
			pet_expr *var;
			pet_expr *init;
		} d;
		struct {
			pet_expr *expr;
		} e;
		struct {
			int independent;
			int declared;
			pet_expr *iv;
			pet_expr *init;
			pet_expr *cond;
			pet_expr *inc;
			pet_tree *body;
		} l;
		struct {
			pet_expr *cond;
			pet_tree *then_body;
			pet_tree *else_body;
		} i;
	} u;
};

const char *pet_tree_type_str(enum pet_tree_type type);
enum pet_tree_type pet_tree_str_type(const char *str);

int pet_tree_is_equal(__isl_keep pet_tree *tree1, __isl_keep pet_tree *tree2);

int pet_tree_is_kill(__isl_keep pet_tree *tree);
int pet_tree_is_assign(__isl_keep pet_tree *tree);
int pet_tree_is_assume(__isl_keep pet_tree *tree);
isl_bool pet_tree_is_affine_assume(__isl_keep pet_tree *tree);
__isl_give isl_multi_pw_aff *pet_tree_assume_get_index(
	__isl_keep pet_tree *tree);

__isl_give pet_tree *pet_tree_new_decl(__isl_take pet_expr *var);
__isl_give pet_tree *pet_tree_new_decl_init(__isl_take pet_expr *var,
	__isl_take pet_expr *init);

__isl_give pet_tree *pet_tree_new_expr(__isl_take pet_expr *expr);
__isl_give pet_tree *pet_tree_new_return(__isl_take pet_expr *expr);

__isl_give pet_tree *pet_tree_set_label(__isl_take pet_tree *tree,
	__isl_take isl_id *label);

__isl_give pet_tree *pet_tree_new_block(isl_ctx *ctx, int block, int n);
__isl_give pet_tree *pet_tree_block_add_child(__isl_take pet_tree *block,
	__isl_take pet_tree *child);
int pet_tree_block_get_block(__isl_keep pet_tree *block);
__isl_give pet_tree *pet_tree_block_set_block(__isl_take pet_tree *block,
	int is_block);

__isl_give pet_tree *pet_tree_new_break(isl_ctx *ctx);
__isl_give pet_tree *pet_tree_new_continue(isl_ctx *ctx);

__isl_give pet_tree *pet_tree_new_infinite_loop(__isl_take pet_tree *body);
__isl_give pet_tree *pet_tree_new_while(__isl_take pet_expr *cond,
	__isl_take pet_tree *body);
__isl_give pet_tree *pet_tree_new_for(int independent, int declared,
	__isl_take pet_expr *iv, __isl_take pet_expr *init,
	__isl_take pet_expr *cond, __isl_take pet_expr *inc,
	__isl_take pet_tree *body);
__isl_give pet_tree *pet_tree_new_if(__isl_take pet_expr *cond,
	__isl_take pet_tree *then_body);
__isl_give pet_tree *pet_tree_new_if_else(__isl_take pet_expr *cond,
	__isl_take pet_tree *then_body, __isl_take pet_tree *else_body);

__isl_give pet_tree *pet_tree_set_loc(__isl_take pet_tree *tree,
	__isl_take pet_loc *loc);

int pet_tree_foreach_sub_tree(__isl_keep pet_tree *tree,
	int (*fn)(__isl_keep pet_tree *tree, void *user), void *user);

__isl_give pet_tree *pet_tree_map_top_down(__isl_take pet_tree *tree,
	__isl_give pet_tree *(*fn)(__isl_take pet_tree *tree, void *user),
	void *user);
__isl_give pet_tree *pet_tree_map_access_expr(__isl_take pet_tree *tree,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user);
__isl_give pet_tree *pet_tree_map_expr(__isl_take pet_tree *tree,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user);

int pet_tree_writes(__isl_keep pet_tree *tree, __isl_keep isl_id *id);
int pet_tree_has_continue_or_break(__isl_keep pet_tree *tree);

__isl_give pet_tree *pet_tree_align_params(__isl_take pet_tree *tree,
	__isl_take isl_space *space);
__isl_give pet_tree *pet_tree_add_ref_ids(__isl_take pet_tree *tree,
	int *n_ref);
__isl_give pet_tree *pet_tree_anonymize(__isl_take pet_tree *tree);
__isl_give pet_tree *pet_tree_gist(__isl_take pet_tree *tree,
	__isl_keep isl_set *context, __isl_keep isl_union_map *value_bounds);
__isl_give pet_tree *pet_tree_update_domain(__isl_take pet_tree *tree,
	__isl_take isl_multi_pw_aff *update);

void pet_tree_dump_with_indent(__isl_keep pet_tree *tree, int indent);

#if defined(__cplusplus)
}
#endif

#endif
