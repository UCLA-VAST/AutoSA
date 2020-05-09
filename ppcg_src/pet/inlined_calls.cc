/*
 * Copyright 2017      Sven Verdoolaege. All rights reserved.
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

#include "clang.h"
#include "expr_plus.h"
#include "id.h"
#include "inlined_calls.h"
#include "scan.h"
#include "tree.h"

#include "config.h"

using namespace std;
using namespace clang;

pet_inlined_calls::~pet_inlined_calls()
{
	vector<pet_tree *>::iterator it;
	map<Stmt *, isl_id *>::iterator it_id;

	for (it = inlined.begin(); it != inlined.end(); ++it)
		pet_tree_free(*it);
	for (it_id = call2id.begin(); it_id != call2id.end(); ++it_id)
		isl_id_free(it_id->second);
}

/* This method is called for each call expression "call"
 * in an expression statement.
 *
 * If the corresponding function body is marked "inline", then add
 * it to this->calls.
 *
 * Return true to continue the traversal.
 */
bool pet_inlined_calls::VisitCallExpr(clang::CallExpr *call)
{
	FunctionDecl *fd = call->getDirectCallee();

	fd = pet_clang_find_function_decl_with_body(fd);

	if (fd && fd->isInlineSpecified())
		calls.push_back(call);

	return true;
}

/* Extract a pet_tree corresponding to the inlined function that
 * is called by "call" and store it in this->inlined.
 * If, moreover, the inlined function has a return value,
 * then create a corresponding variable and store it in this->call2id.
 *
 * While extracting the inlined function, take into account
 * the mapping from call expressions to return variables for
 * previously extracted inlined functions in order to handle
 * nested calls.
 */
void pet_inlined_calls::add(CallExpr *call)
{
	FunctionDecl *fd = call->getDirectCallee();
	QualType qt;
	isl_id *id = NULL;

	qt = fd->getReturnType();
	if (!qt->isVoidType()) {
		id = pet_id_ret_from_type(scan->ctx, scan->n_ret++, qt);
		call2id[call] = isl_id_copy(id);
	}
	scan->call2id = &this->call2id;
	inlined.push_back(scan->extract_inlined_call(call, fd, id));
	scan->call2id = NULL;
	isl_id_free(id);
}

/* Collect all the call expressions in "stmt" that need to be inlined,
 * the corresponding pet_tree objects and the variables that store
 * the return values.
 *
 * The call expressions are first collected outermost to innermost.
 * Then the corresponding inlined functions are extracted in reverse order
 * to ensure that a nested call is performed before an outer call.
 */
void pet_inlined_calls::collect(Stmt *stmt)
{
	int n;

	TraverseStmt(stmt);

	n = calls.size();
	for (int i = n - 1; i >= 0; --i)
		add(cast<CallExpr>(calls[i]));
}

/* Add the inlined call expressions to "tree", where "tree" corresponds
 * to the original expression statement containing the calls, but with
 * the calls replaced by accesses to the return variables in this->call2id.
 * In particular, construct a new block containing declarations
 * for the return variables in this->call2id, the inlined functions
 * from innermost to outermost and finally "tree" itself.
 */
__isl_give pet_tree *pet_inlined_calls::add_inlined(__isl_take pet_tree *tree)
{
	pet_tree *block;
	int n;
	std::vector<pet_tree *>::iterator it_in;
	std::map<Stmt *, isl_id *>::iterator it;

	if (inlined.empty())
		return tree;

	n = call2id.size() + inlined.size() + 1;

	block = pet_tree_new_block(scan->ctx, 1, n);
	for (it = call2id.begin(); it != call2id.end(); ++it) {
		pet_expr *expr;
		pet_tree *decl;

		expr = pet_expr_access_from_id(isl_id_copy(it->second),
						scan->ast_context);
		decl = pet_tree_new_decl(expr);
		block = pet_tree_block_add_child(block, decl);
	}
	for (it_in = inlined.begin(); it_in != inlined.end(); ++it_in)
		block = pet_tree_block_add_child(block, pet_tree_copy(*it_in));
	block = pet_tree_block_add_child(block, tree);

	return block;
}
