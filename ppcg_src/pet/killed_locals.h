#ifndef KILLED_LOCALS_H
#define KILLED_LOCALS_H

#include <set>
#include <iostream>

#include <clang/Basic/SourceManager.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RecursiveASTVisitor.h>

/* Structure for keeping track of local variables that can be killed
 * after the scop.
 * In particular, variables of interest are first added to "locals"
 * Then the Stmt in which the variable declaration appears is scanned
 * for any possible leak of a pointer or any use after a specified scop.
 * In such cases, the variable is removed from "locals".
 * The scop is assumed to appear at the same level of the declaration.
 * In particular, it does not appear inside a nested control structure,
 * meaning that it is sufficient to look at uses of the variables
 * that textually appear after the specified scop.
 *
 * locals is the set of variables of interest.
 * accessed keeps track of the variables that are accessed inside the scop.
 * scop_start is the start of the scop
 * scop_end is the end of the scop
 * addr_end is the end of the latest visited address_of expression.
 * expr_end is the end of the latest handled expression.
 */
struct pet_killed_locals : clang::RecursiveASTVisitor<pet_killed_locals> {
	clang::SourceManager &SM;
	std::set<clang::ValueDecl *> locals;
	std::set<clang::ValueDecl *> accessed;
	unsigned scop_start;
	unsigned scop_end;
	unsigned addr_end;
	unsigned expr_end;

	pet_killed_locals(clang::SourceManager &SM) : SM(SM) {}

	void add_local(clang::Decl *decl);
	void add_locals(clang::DeclStmt *stmt);
	void set_addr_end(clang::UnaryOperator *expr);
	bool check_decl_in_expr(clang::Expr *expr);
	void remove_accessed_after(clang::Stmt *stmt,
				    unsigned start, unsigned end);
	bool VisitUnaryOperator(clang::UnaryOperator *expr) {
		if (expr->getOpcode() == clang::UO_AddrOf)
			set_addr_end(expr);
		return true;
	}
	bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr) {
		return check_decl_in_expr(expr);
	}
	bool VisitDeclRefExpr(clang::DeclRefExpr *expr) {
		return check_decl_in_expr(expr);
	}
	void dump() {
		std::set<clang::ValueDecl *>::iterator it;
		std::cerr << "local" << std::endl;
		for (it = locals.begin(); it != locals.end(); ++it)
			(*it)->dump();
		std::cerr << "accessed" << std::endl;
		for (it = accessed.begin(); it != accessed.end(); ++it)
			(*it)->dump();
	}
};

#endif
