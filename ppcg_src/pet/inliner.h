#ifndef PET_INLINER_H
#define PET_INLINER_H

#include <string>
#include <utility>
#include <vector>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>

#include <isl/id.h>

#include "expr.h"
#include "substituter.h"

/* A helper class for inlining a pet_tree of the body of a called function.
 * It keeps track of the substitutions that need to be performed on
 * the inlined tree, along with the assignments that need to performed
 * before the inlined tree.
 *
 * In particular, for a scalar argument declared "n" in the callee
 * and passed the expression "e" by the caller, an assignment
 *
 *	n' = e;
 *
 * is added before the inlined tree and n is replaced by n' in the inlined
 * tree.  n' may have the same name as n, but it may also be different if
 * n also appears in the caller.
 * For an array argument declared "a" in the callee and passed
 * the expression "X[i1,...,in]" by the caller, assignments
 *
 *	__pet_arg_1 = i1;
 *	...
 *	__pet_arg_n = in;
 *
 * are added before the inlined tree and "a" is replaced by
 * X[__pet_arg_1,...,__pet_arg_n] in the inlined tree.
 *
 * The assignments are stored in "assignments".
 * The substitutions are stored in the superclass.
 */
struct pet_inliner : pet_substituter {
	isl_ctx *ctx;
	std::vector<std::pair<pet_expr *, pet_expr *> > assignments;
	int &n_arg;
	clang::ASTContext &ast_context;

	pet_inliner(isl_ctx *ctx, int &n_arg, clang::ASTContext &ast_context) :
		ctx(ctx), n_arg(n_arg), ast_context(ast_context) {}

	__isl_give pet_expr *assign( __isl_take isl_id *id,
		__isl_take pet_expr *expr);

	void add_scalar_arg(clang::ValueDecl *decl, const std::string &name,
		__isl_take pet_expr *expr);
	void add_array_arg(clang::ValueDecl *decl, __isl_take pet_expr *expr,
		int is_add);

	__isl_give pet_tree *inline_tree(__isl_take pet_tree *tree,
		__isl_keep isl_id *return_id);

	~pet_inliner();
};

#endif
