#ifndef PET_EXPR_H
#define PET_EXPR_H

#include <pet.h>

#include "context.h"
#include "expr_access_type.h"
#include "summary.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Representation of access expression.
 *
 * For each access expression inside the body of a statement, "ref_id"
 * is a unique reference identifier.
 * "index" represents the index expression, while "access"
 * represents the corresponding access relations.
 * The output dimension of the index expression may be smaller
 * than the number of dimensions of the accessed array (recorded
 * in "depth").
 * The target space of the access relation, on the other hand,
 * is equal to the array space.
 * The entries in "access" may be NULL if they can be derived directly from
 * "index" and "depth" in construct_access_relation or if they are
 * irrelevant for the given type of access.
 * In particular, the entries of "access" may be NULL if there are
 * no additional constraints on the access relations.
 * Both "index" and the "access" entries usually map an iteration space
 * to a (partial) data space.
 * If the access has arguments, however, then the domain of the
 * mapping is a wrapped mapping from the iteration space
 * to a space of dimensionality equal to the number of arguments.
 * Each dimension in this space corresponds to the value of the
 * corresponding argument.
 *
 * The ranges of the index expressions and access relations may
 * also be wrapped relations, in which case the expression represents
 * a member access, with the structure represented by the domain
 * of this wrapped relation and the member represented by the range.
 * In case of nested member accesses, the domain is itself a wrapped
 * relation.
 *
 * If the data space is unnamed (and 1D), then it represents
 * the set of integers.  That is, the access represents a value that
 * is equal to the index.
 *
 * An access expresssion is marked "read" if it represents a read and
 * marked "write" if it represents a write.  A single access expression
 * may be marked both read and write.
 * Alternatively, the expression may be marked "kill", in which case it
 * is the argument of a kill operation and represents the set of
 * killed array elements.  Such accesses are marked neither read nor write.
 * Since a kill can never be a read (or a write), the killed access
 * relation is stored in the same location as the may read access relation.
 */
struct pet_expr_access {
	isl_id *ref_id;
	isl_multi_pw_aff *index;
	int depth;
	unsigned read : 1;
	unsigned write : 1;
	unsigned kill : 1;
	isl_union_map *access[pet_expr_access_end];
};
/* Representation of call expression.
 *
 * A function call is represented by the name of the called function and
 * an optional function summary (the value NULL indicating that there is
 * no function summary).
 */
struct pet_expr_call {
	char *name;
	pet_function_summary *summary;
};
/* Representation of double expression.
 *
 * A double is represented as both an (approximate) value "val" and
 * a string representation "s".
 */
struct pet_expr_double {
	double val;
	char *s;
};
/* d is valid when type == pet_expr_double
 * i isl valid when type == pet_expr_int
 * acc is valid when type == pet_expr_access
 * c is valid when type == pet_expr_call
 * type is valid when type == pet_expr_cast
 * op is valid otherwise
 *
 * "hash" is a copy of the hash value computed by pet_expr_get_hash.
 * It is zero when it has not been computed yet.  The value is reset
 * whenever the pet_expr is modified (in pet_expr_cow and
 * introduce_access_relations).
 *
 * If type_size is not zero, then the expression is of an integer type
 * and type_size represents the size of the type in bits.
 * If type_size is greater than zero, then the type is unsigned
 * and the number of bits is equal to type_size.
 * If type_size is less than zero, then the type is signed
 * and the number of bits is equal to -type_size.
 * type_size may also be zero if the size is (still) unknown.
 */
struct pet_expr {
	int ref;
	isl_ctx *ctx;

	uint32_t hash;

	enum pet_expr_type type;

	int type_size;

	unsigned n_arg;
	pet_expr **args;

	union {
		struct pet_expr_access acc;
		enum pet_op_type op;
		struct pet_expr_call c;
		char *type_name;
		struct pet_expr_double d;
		isl_val *i;
	};
};

const char *pet_type_str(enum pet_expr_type type);
enum pet_expr_type pet_str_type(const char *str);

enum pet_op_type pet_str_op(const char *str);

__isl_give pet_expr *pet_expr_alloc(isl_ctx *ctx, enum pet_expr_type type);
__isl_give pet_expr *pet_expr_kill_from_access_and_index(
	__isl_take isl_map *access, __isl_take isl_multi_pw_aff *index);
__isl_give pet_expr *pet_expr_new_unary(int type_size, enum pet_op_type op,
	__isl_take pet_expr *arg);
__isl_give pet_expr *pet_expr_new_binary(int type_size, enum pet_op_type op,
	__isl_take pet_expr *lhs, __isl_take pet_expr *rhs);
__isl_give pet_expr *pet_expr_new_ternary(__isl_take pet_expr *cond,
	__isl_take pet_expr *lhs, __isl_take pet_expr *rhs);
__isl_give pet_expr *pet_expr_new_call(isl_ctx *ctx, const char *name,
	unsigned n_arg);
__isl_give pet_expr *pet_expr_new_double(isl_ctx *ctx, double d, const char *s);
__isl_give pet_expr *pet_expr_new_int(__isl_take isl_val *v);

__isl_give pet_expr *pet_expr_arg(__isl_take pet_expr *expr, int pos);

__isl_give pet_expr *pet_expr_cow(__isl_take pet_expr *expr);

__isl_give isl_pw_aff *pet_expr_extract_affine_condition(
	__isl_keep pet_expr *expr, __isl_keep pet_context *pc);
__isl_give isl_pw_aff *pet_expr_extract_comparison(enum pet_op_type op,
	__isl_keep pet_expr *lhs, __isl_keep pet_expr *rhs,
	__isl_keep pet_context *pc);
__isl_give pet_expr *pet_expr_resolve_assume(__isl_take pet_expr *expr,
	__isl_keep pet_context *pc);

uint32_t pet_expr_get_hash(__isl_keep pet_expr *expr);

int pet_expr_is_address_of(__isl_keep pet_expr *expr);
int pet_expr_is_assume(__isl_keep pet_expr *expr);
int pet_expr_is_boolean(__isl_keep pet_expr *expr);
int pet_expr_is_comparison(__isl_keep pet_expr *expr);
int pet_expr_is_min(__isl_keep pet_expr *expr);
int pet_expr_is_max(__isl_keep pet_expr *expr);
int pet_expr_is_scalar_access(__isl_keep pet_expr *expr);
int pet_expr_is_equal(__isl_keep pet_expr *expr1, __isl_keep pet_expr *expr2);
isl_bool pet_expr_is_same_access(__isl_keep pet_expr *expr1,
	__isl_keep pet_expr *expr2);

__isl_give isl_pw_aff *pet_expr_get_affine(__isl_keep pet_expr *expr);
__isl_give isl_space *pet_expr_access_get_parameter_space(
	__isl_take pet_expr *expr);
__isl_give isl_space *pet_expr_access_get_augmented_domain_space(
	__isl_keep pet_expr *expr);
__isl_give isl_space *pet_expr_access_get_domain_space(
	__isl_keep pet_expr *expr);
isl_stat pet_expr_access_foreach_data_space(__isl_keep pet_expr *expr,
	isl_stat (*fn)(__isl_take isl_space *space, void *user), void *user);

isl_bool pet_expr_access_has_any_access_relation(__isl_keep pet_expr *expr);
__isl_give isl_union_map *pet_expr_access_get_dependent_access(
	__isl_keep pet_expr *expr, enum pet_expr_access_type type);
__isl_give isl_map *pet_expr_access_get_may_access(__isl_keep pet_expr *expr);

__isl_give pet_expr *pet_expr_map_top_down(__isl_take pet_expr *expr,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user);
__isl_give pet_expr *pet_expr_map_access(__isl_take pet_expr *expr,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user);
__isl_give pet_expr *pet_expr_map_call(__isl_take pet_expr *expr,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user);
__isl_give pet_expr *pet_expr_map_op(__isl_take pet_expr *expr,
	__isl_give pet_expr *(*fn)(__isl_take pet_expr *expr, void *user),
	void *user);

__isl_give isl_union_map *pet_expr_access_get_access(__isl_keep pet_expr *expr,
	enum pet_expr_access_type type);
__isl_give pet_expr *pet_expr_access_set_access(__isl_take pet_expr *expr,
	enum pet_expr_access_type type, __isl_take isl_union_map *access);
__isl_give pet_expr *pet_expr_access_set_index(__isl_take pet_expr *expr,
	__isl_take isl_multi_pw_aff *index);

int pet_expr_is_sub_access(__isl_keep pet_expr *expr1,
	__isl_keep pet_expr *expr2, int n_arg);

int pet_expr_writes(__isl_keep pet_expr *expr, __isl_keep isl_id *id);

__isl_give pet_expr *pet_expr_access_move_dims(__isl_take pet_expr *expr,
	enum isl_dim_type dst_type, unsigned dst_pos,
	enum isl_dim_type src_type, unsigned src_pos, unsigned n);
__isl_give pet_expr *pet_expr_access_pullback_multi_aff(
	__isl_take pet_expr *expr, __isl_take isl_multi_aff *ma);
__isl_give pet_expr *pet_expr_access_pullback_multi_pw_aff(
	__isl_take pet_expr *expr, __isl_take isl_multi_pw_aff *mpa);
__isl_give pet_expr *pet_expr_access_align_params(__isl_take pet_expr *expr);
__isl_give pet_expr *pet_expr_restrict(__isl_take pet_expr *expr,
	__isl_take isl_set *cond);
__isl_give pet_expr *pet_expr_access_update_domain(__isl_take pet_expr *expr,
	__isl_keep isl_multi_pw_aff *update);
__isl_give pet_expr *pet_expr_update_domain(__isl_take pet_expr *expr,
	__isl_take isl_multi_pw_aff *update);
__isl_give pet_expr *pet_expr_align_params(__isl_take pet_expr *expr,
	__isl_take isl_space *space);
__isl_give pet_expr *pet_expr_filter(__isl_take pet_expr *expr,
	__isl_take isl_multi_pw_aff *test, int satisfied);
__isl_give pet_expr *pet_expr_add_ref_ids(__isl_take pet_expr *expr,
	int *n_ref);
__isl_give pet_expr *pet_expr_anonymize(__isl_take pet_expr *expr);
__isl_give pet_expr *pet_expr_gist(__isl_take pet_expr *expr,
	__isl_keep isl_set *context, __isl_keep isl_union_map *value_bounds);

__isl_give isl_union_map *pet_expr_tag_access(__isl_keep pet_expr *expr,
	__isl_take isl_union_map *access);

__isl_give pet_expr *pet_expr_access_subscript(__isl_take pet_expr *base,
	__isl_take pet_expr *index);
__isl_give pet_expr *pet_expr_access_member(__isl_take pet_expr *base,
	__isl_take isl_id *member);

int pet_expr_call_has_summary(__isl_keep pet_expr *expr);
__isl_give pet_function_summary *pet_expr_call_get_summary(
	__isl_keep pet_expr *expr);
__isl_give pet_expr *pet_expr_call_set_summary(__isl_take pet_expr *expr,
	__isl_take pet_function_summary *summary);

int pet_expr_get_type_size(__isl_keep pet_expr *expr);
__isl_give pet_expr *pet_expr_set_type_size(__isl_take pet_expr *expr,
	int type_size);
__isl_give pet_expr *pet_expr_access_set_depth(__isl_take pet_expr *expr,
	int depth);

__isl_give pet_expr *pet_expr_insert_domain(__isl_take pet_expr *expr,
	__isl_take isl_space *space);

__isl_give pet_expr *pet_expr_access_patch(__isl_take pet_expr *expr,
	__isl_take isl_multi_pw_aff *prefix, int add);

__isl_give isl_printer *pet_expr_print(__isl_keep pet_expr *expr,
	__isl_take isl_printer *p);
void pet_expr_dump_with_indent(__isl_keep pet_expr *expr, int indent);

#if defined(__cplusplus)
}
#endif

#endif
