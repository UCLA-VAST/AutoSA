#ifndef PET_SUBSTITUTER_H
#define PET_SUBSTITUTER_H

#include <map>

#include "expr.h"
#include "tree.h"

/* Keep track of substitutions that need to be performed in "subs",
 * where the isl_id is replaced by the pet_expr, which is either
 * an access expression or the address of an access expression.
 * "substitute" performs the actual substitution.
 */
struct pet_substituter {
	std::map<isl_id *, pet_expr *> subs;

	void add_sub(__isl_take isl_id *id, __isl_take pet_expr *expr);

	__isl_give pet_expr *substitute(__isl_take pet_expr *expr);
	__isl_give pet_tree *substitute(__isl_take pet_tree *tree);

	~pet_substituter();
};

#endif
