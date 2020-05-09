#ifndef PET_INLINED_CALLS_H
#define PET_INLINED_CALLS_H

#include <vector>
#include <map>

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RecursiveASTVisitor.h>

struct PetScan;

/* Structure for keeping track of inlined calls in an expression statement.
 *
 * "calls" collects the calls that appear in the expression statement and
 * that need to be inlined from outermost to innermost.
 * "inlined" collects the inlined pet_tree objects corresponding to elements
 * in "calls" in reverse order.
 * "call2id" maps inlined call expressions that return a value to
 * the corresponding variable.
 * "scan" is used to extract the inlined calls.
 */
struct pet_inlined_calls : clang::RecursiveASTVisitor<pet_inlined_calls> {
	std::vector<clang::Stmt *> calls;
	std::vector<pet_tree *> inlined;
	std::map<clang::Stmt *, isl_id *> call2id;
	PetScan *scan;

	pet_inlined_calls(PetScan *scan) : scan(scan) {}
	~pet_inlined_calls();

	bool VisitCallExpr(clang::CallExpr *call);

	void add(clang::CallExpr *call);
	void collect(clang::Stmt *stmt);
	__isl_give pet_tree *add_inlined(__isl_take pet_tree *tree);
};

#endif
