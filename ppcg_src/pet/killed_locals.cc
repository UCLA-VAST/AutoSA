/*
 * Copyright 2015      Sven Verdoolaege. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY SVEN VERDOOLAEGE ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SVEN VERDOOLAEGE OR
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
 * Sven Verdoolaege.
 */

#include "config.h"

#include "clang.h"
#include "clang_compatibility.h"
#include "killed_locals.h"

using namespace std;
using namespace clang;

/* Return the file offset of the expansion location of "Loc".
 */
static unsigned getExpansionOffset(SourceManager &SM, SourceLocation Loc)
{
	return SM.getFileOffset(SM.getExpansionLoc(Loc));
}

/* Given a DeclRefExpr or an ArraySubscriptExpr, return a pointer
 * to the base DeclRefExpr.
 * If the expression is something other than a nested ArraySubscriptExpr
 * with a DeclRefExpr at the base, then return NULL.
 */
static DeclRefExpr *extract_array_base(Expr *expr)
{
	while (isa<ArraySubscriptExpr>(expr)) {
		expr = (cast<ArraySubscriptExpr>(expr))->getBase();
		expr = pet_clang_strip_casts(expr);
	}
	return dyn_cast<DeclRefExpr>(expr);
}

/* Add "decl" to the set of local variables, provided it is a ValueDecl.
 */
void pet_killed_locals::add_local(Decl *decl)
{
	ValueDecl *vd;

	vd = dyn_cast<ValueDecl>(decl);
	if (vd)
		locals.insert(vd);
}

/* Add all variables declared by "stmt" to the set of local variables.
 */
void pet_killed_locals::add_locals(DeclStmt *stmt)
{
	if (stmt->isSingleDecl()) {
		add_local(stmt->getSingleDecl());
	} else {
		const DeclGroup &group = stmt->getDeclGroup().getDeclGroup();
		unsigned n = group.size();
		for (unsigned i = 0; i < n; ++i)
			add_local(group[i]);
	}
}

/* Set this->addr_end to the end of the address_of expression "expr".
 */
void pet_killed_locals::set_addr_end(UnaryOperator *expr)
{
	addr_end = getExpansionOffset(SM, end_loc(expr));
}

/* Given an expression of type ArraySubscriptExpr or DeclRefExpr,
 * check two things
 * - is the variable used inside the scop?
 * - is the variable used after the scop or can a pointer be taken?
 * Return true if the traversal should continue.
 *
 * Reset the pointer to the end of the latest address-of expression
 * such that only the first array or scalar is considered to have
 * its address taken.  In particular, accesses inside the indices
 * of the array should not be considered to have their address taken.
 *
 * If the variable is not one of the local variables or
 * if the access appears inside an expression that was already handled,
 * then simply return.
 *
 * Otherwise, the expression is handled and "expr_end" is updated
 * to prevent subexpressions with the same base expression
 * from being handled as well.
 *
 * If a higher-dimensional slice of an array is accessed or
 * if the access appears inside an address-of expression,
 * then a pointer may leak, so the variable should not be killed.
 * Similarly, if the access appears after the end of the scop,
 * then the variable should not be killed.
 *
 * Otherwise, if the access appears inside the scop, then
 * keep track of the fact that the variable was accessed at least once
 * inside the scop.
 */
bool pet_killed_locals::check_decl_in_expr(Expr *expr)
{
	unsigned loc;
	int depth;
	DeclRefExpr *ref;
	ValueDecl *decl;
	unsigned old_addr_end;

	ref = extract_array_base(expr);
	if (!ref)
		return true;

	old_addr_end = addr_end;
	addr_end = 0;

	decl = ref->getDecl();
	if (locals.find(decl) == locals.end())
		return true;
	loc = getExpansionOffset(SM, begin_loc(expr));
	if (loc <= expr_end)
		return true;

	expr_end = getExpansionOffset(SM, end_loc(ref));
	depth = pet_clang_array_depth(expr->getType());
	if (loc >= scop_end || loc <= old_addr_end || depth != 0)
		locals.erase(decl);
	if (loc >= scop_start && loc <= scop_end)
		accessed.insert(decl);

	return locals.size() != 0;
}

/* Remove the local variables that may be accessed inside "stmt" after
 * the scop starting at "start" and ending at "end", or that
 * are not accessed at all inside that scop.
 *
 * If there are no local variables that could potentially be killed,
 * then simply return.
 *
 * Otherwise, scan "stmt" for any potential use of the variables
 * after the scop.  This includes a possible pointer being taken
 * to (part of) the variable.  If there is any such use, then
 * the variable is removed from the set of local variables.
 *
 * At the same time, keep track of the variables that are
 * used anywhere inside the scop.  At the end, replace the local
 * variables with the intersection with these accessed variables.
 */
void pet_killed_locals::remove_accessed_after(Stmt *stmt, unsigned start,
	unsigned end)
{
	set<ValueDecl *> accessed_local;

	if (locals.size() == 0)
		return;
	scop_start = start;
	scop_end = end;
	addr_end = 0;
	expr_end = 0;
	TraverseStmt(stmt);
	set_intersection(locals.begin(), locals.end(),
			 accessed.begin(), accessed.end(),
			 inserter(accessed_local, accessed_local.begin()));
	locals = accessed_local;
}
