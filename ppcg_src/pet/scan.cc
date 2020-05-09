/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2012-2015 Ecole Normale Superieure. All rights reserved.
 * Copyright 2015-2017 Sven Verdoolaege. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY LEIDEN UNIVERSITY ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LEIDEN UNIVERSITY OR
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
 * Leiden University.
 */ 

#include "config.h"

#include <string.h>
#include <set>
#include <map>
#include <iostream>
#include <sstream>
#include <llvm/Support/raw_ostream.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTDiagnostic.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>

#include <isl/id.h>
#include <isl/space.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/union_set.h>

#include "aff.h"
#include "array.h"
#include "clang_compatibility.h"
#include "clang.h"
#include "context.h"
#include "expr.h"
#include "expr_plus.h"
#include "id.h"
#include "inliner.h"
#include "inlined_calls.h"
#include "killed_locals.h"
#include "nest.h"
#include "options.h"
#include "scan.h"
#include "scop.h"
#include "scop_plus.h"
#include "substituter.h"
#include "tree.h"
#include "tree2scop.h"

using namespace std;
using namespace clang;

static enum pet_op_type UnaryOperatorKind2pet_op_type(UnaryOperatorKind kind)
{
	switch (kind) {
	case UO_Minus:
		return pet_op_minus;
	case UO_Not:
		return pet_op_not;
	case UO_LNot:
		return pet_op_lnot;
	case UO_PostInc:
		return pet_op_post_inc;
	case UO_PostDec:
		return pet_op_post_dec;
	case UO_PreInc:
		return pet_op_pre_inc;
	case UO_PreDec:
		return pet_op_pre_dec;
	default:
		return pet_op_last;
	}
}

static enum pet_op_type BinaryOperatorKind2pet_op_type(BinaryOperatorKind kind)
{
	switch (kind) {
	case BO_AddAssign:
		return pet_op_add_assign;
	case BO_SubAssign:
		return pet_op_sub_assign;
	case BO_MulAssign:
		return pet_op_mul_assign;
	case BO_DivAssign:
		return pet_op_div_assign;
	case BO_AndAssign:
		return pet_op_and_assign;
	case BO_XorAssign:
		return pet_op_xor_assign;
	case BO_OrAssign:
		return pet_op_or_assign;
	case BO_Assign:
		return pet_op_assign;
	case BO_Add:
		return pet_op_add;
	case BO_Sub:
		return pet_op_sub;
	case BO_Mul:
		return pet_op_mul;
	case BO_Div:
		return pet_op_div;
	case BO_Rem:
		return pet_op_mod;
	case BO_Shl:
		return pet_op_shl;
	case BO_Shr:
		return pet_op_shr;
	case BO_EQ:
		return pet_op_eq;
	case BO_NE:
		return pet_op_ne;
	case BO_LE:
		return pet_op_le;
	case BO_GE:
		return pet_op_ge;
	case BO_LT:
		return pet_op_lt;
	case BO_GT:
		return pet_op_gt;
	case BO_And:
		return pet_op_and;
	case BO_Xor:
		return pet_op_xor;
	case BO_Or:
		return pet_op_or;
	case BO_LAnd:
		return pet_op_land;
	case BO_LOr:
		return pet_op_lor;
	default:
		return pet_op_last;
	}
}

#ifdef GETTYPEINFORETURNSTYPEINFO

static int size_in_bytes(ASTContext &context, QualType type)
{
	return context.getTypeInfo(type).Width / 8;
}

#else

static int size_in_bytes(ASTContext &context, QualType type)
{
	return context.getTypeInfo(type).first / 8;
}

#endif

/* Check if the element type corresponding to the given array type
 * has a const qualifier.
 */
static bool const_base(QualType qt)
{
	const Type *type = qt.getTypePtr();

	if (type->isPointerType())
		return const_base(type->getPointeeType());
	if (type->isArrayType()) {
		const ArrayType *atype;
		type = type->getCanonicalTypeInternal().getTypePtr();
		atype = cast<ArrayType>(type);
		return const_base(atype->getElementType());
	}

	return qt.isConstQualified();
}

PetScan::~PetScan()
{
	std::map<const Type *, pet_expr *>::iterator it;
	std::map<FunctionDecl *, pet_function_summary *>::iterator it_s;

	for (it = type_size.begin(); it != type_size.end(); ++it)
		pet_expr_free(it->second);
	for (it_s = summary_cache.begin(); it_s != summary_cache.end(); ++it_s)
		pet_function_summary_free(it_s->second);

	isl_id_to_pet_expr_free(id_size);
	isl_union_map_free(value_bounds);
}

/* Report a diagnostic on the range "range", unless autodetect is set.
 */
void PetScan::report(SourceRange range, unsigned id)
{
	if (options->autodetect)
		return;

	SourceLocation loc = range.getBegin();
	DiagnosticsEngine &diag = PP.getDiagnostics();
	DiagnosticBuilder B = diag.Report(loc, id) << range;
}

/* Report a diagnostic on "stmt", unless autodetect is set.
 */
void PetScan::report(Stmt *stmt, unsigned id)
{
	report(stmt->getSourceRange(), id);
}

/* Report a diagnostic on "decl", unless autodetect is set.
 */
void PetScan::report(Decl *decl, unsigned id)
{
	report(decl->getSourceRange(), id);
}

/* Called if we found something we (currently) cannot handle.
 * We'll provide more informative warnings later.
 *
 * We only actually complain if autodetect is false.
 */
void PetScan::unsupported(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "unsupported");
	report(stmt, id);
}

/* Report an unsupported unary operator, unless autodetect is set.
 */
void PetScan::report_unsupported_unary_operator(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
			       "this type of unary operator is not supported");
	report(stmt, id);
}

/* Report an unsupported binary operator, unless autodetect is set.
 */
void PetScan::report_unsupported_binary_operator(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
			       "this type of binary operator is not supported");
	report(stmt, id);
}

/* Report an unsupported statement type, unless autodetect is set.
 */
void PetScan::report_unsupported_statement_type(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
				   "this type of statement is not supported");
	report(stmt, id);
}

/* Report a missing prototype, unless autodetect is set.
 */
void PetScan::report_prototype_required(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "prototype required");
	report(stmt, id);
}

/* Report a missing increment, unless autodetect is set.
 */
void PetScan::report_missing_increment(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "missing increment");
	report(stmt, id);
}

/* Report a missing summary function, unless autodetect is set.
 */
void PetScan::report_missing_summary_function(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "missing summary function");
	report(stmt, id);
}

/* Report a missing summary function body, unless autodetect is set.
 */
void PetScan::report_missing_summary_function_body(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "missing summary function body");
	report(stmt, id);
}

/* Report an unsupported argument in a call to an inlined function,
 * unless autodetect is set.
 */
void PetScan::report_unsupported_inline_function_argument(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
				   "unsupported inline function call argument");
	report(stmt, id);
}

/* Report an unsupported type of declaration, unless autodetect is set.
 */
void PetScan::report_unsupported_declaration(Decl *decl)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
				   "unsupported declaration");
	report(decl, id);
}

/* Report an unbalanced pair of scop/endscop pragmas, unless autodetect is set.
 */
void PetScan::report_unbalanced_pragmas(SourceLocation scop,
	SourceLocation endscop)
{
	if (options->autodetect)
		return;

	DiagnosticsEngine &diag = PP.getDiagnostics();
	{
		unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "unbalanced endscop pragma");
		DiagnosticBuilder B2 = diag.Report(endscop, id);
	}
	{
		unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Note,
					   "corresponding scop pragma");
		DiagnosticBuilder B = diag.Report(scop, id);
	}
}

/* Report a return statement in an unsupported context,
 * unless autodetect is set.
 */
void PetScan::report_unsupported_return(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
			   "return statements not supported in this context");
	report(stmt, id);
}

/* Report a return statement that does not appear at the end of a function,
 * unless autodetect is set.
 */
void PetScan::report_return_not_at_end_of_function(Stmt *stmt)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
		       "return statement must be final statement in function");
	report(stmt, id);
}

/* Extract an integer from "val", which is assumed to be non-negative.
 */
static __isl_give isl_val *extract_unsigned(isl_ctx *ctx,
	const llvm::APInt &val)
{
	unsigned n;
	const uint64_t *data;

	data = val.getRawData();
	n = val.getNumWords();
	return isl_val_int_from_chunks(ctx, n, sizeof(uint64_t), data);
}

/* Extract an integer from "val".  If "is_signed" is set, then "val"
 * is signed.  Otherwise it it unsigned.
 */
static __isl_give isl_val *extract_int(isl_ctx *ctx, bool is_signed,
	llvm::APInt val)
{
	int is_negative = is_signed && val.isNegative();
	isl_val *v;

	if (is_negative)
		val = -val;

	v = extract_unsigned(ctx, val);

	if (is_negative)
		v = isl_val_neg(v);
	return v;
}

/* Extract an integer from "expr".
 */
__isl_give isl_val *PetScan::extract_int(isl_ctx *ctx, IntegerLiteral *expr)
{
	const Type *type = expr->getType().getTypePtr();
	bool is_signed = type->hasSignedIntegerRepresentation();

	return ::extract_int(ctx, is_signed, expr->getValue());
}

/* Extract an integer from "expr".
 * Return NULL if "expr" does not (obviously) represent an integer.
 */
__isl_give isl_val *PetScan::extract_int(clang::ParenExpr *expr)
{
	return extract_int(expr->getSubExpr());
}

/* Extract an integer from "expr".
 * Return NULL if "expr" does not (obviously) represent an integer.
 */
__isl_give isl_val *PetScan::extract_int(clang::Expr *expr)
{
	if (expr->getStmtClass() == Stmt::IntegerLiteralClass)
		return extract_int(ctx, cast<IntegerLiteral>(expr));
	if (expr->getStmtClass() == Stmt::ParenExprClass)
		return extract_int(cast<ParenExpr>(expr));

	unsupported(expr);
	return NULL;
}

/* Extract a pet_expr from the APInt "val", which is assumed
 * to be non-negative.
 */
__isl_give pet_expr *PetScan::extract_expr(const llvm::APInt &val)
{
	return pet_expr_new_int(extract_unsigned(ctx, val));
}

/* Return the number of bits needed to represent the type of "decl",
 * if it is an integer type.  Otherwise return 0.
 * If qt is signed then return the opposite of the number of bits.
 */
static int get_type_size(ValueDecl *decl)
{
	return pet_clang_get_type_size(decl->getType(), decl->getASTContext());
}

/* Bound parameter "pos" of "set" to the possible values of "decl".
 */
static __isl_give isl_set *set_parameter_bounds(__isl_take isl_set *set,
	unsigned pos, ValueDecl *decl)
{
	int type_size;
	isl_ctx *ctx;
	isl_val *bound;

	ctx = isl_set_get_ctx(set);
	type_size = get_type_size(decl);
	if (type_size == 0)
		isl_die(ctx, isl_error_invalid, "not an integer type",
			return isl_set_free(set));
	if (type_size > 0) {
		set = isl_set_lower_bound_si(set, isl_dim_param, pos, 0);
		bound = isl_val_int_from_ui(ctx, type_size);
		bound = isl_val_2exp(bound);
		bound = isl_val_sub_ui(bound, 1);
		set = isl_set_upper_bound_val(set, isl_dim_param, pos, bound);
	} else {
		bound = isl_val_int_from_ui(ctx, -type_size - 1);
		bound = isl_val_2exp(bound);
		bound = isl_val_sub_ui(bound, 1);
		set = isl_set_upper_bound_val(set, isl_dim_param, pos,
						isl_val_copy(bound));
		bound = isl_val_neg(bound);
		bound = isl_val_sub_ui(bound, 1);
		set = isl_set_lower_bound_val(set, isl_dim_param, pos, bound);
	}

	return set;
}

__isl_give pet_expr *PetScan::extract_index_expr(ImplicitCastExpr *expr)
{
	return extract_index_expr(expr->getSubExpr());
}

/* Construct a pet_expr representing an index expression for an access
 * to the variable referenced by "expr".
 *
 * If "expr" references an enum constant, then return an integer expression
 * instead, representing the value of the enum constant.
 */
__isl_give pet_expr *PetScan::extract_index_expr(DeclRefExpr *expr)
{
	return extract_index_expr(expr->getDecl());
}

/* Construct a pet_expr representing an index expression for an access
 * to the variable "decl".
 *
 * If "decl" is an enum constant, then we return an integer expression
 * instead, representing the value of the enum constant.
 */
__isl_give pet_expr *PetScan::extract_index_expr(ValueDecl *decl)
{
	isl_id *id;

	if (isa<EnumConstantDecl>(decl))
		return extract_expr(cast<EnumConstantDecl>(decl));

	id = pet_id_from_decl(ctx, decl);
	return pet_id_create_index_expr(id);
}

/* Construct a pet_expr representing the index expression "expr"
 * Return NULL on error.
 *
 * If "expr" is a reference to an enum constant, then return
 * an integer expression instead, representing the value of the enum constant.
 */
__isl_give pet_expr *PetScan::extract_index_expr(Expr *expr)
{
	switch (expr->getStmtClass()) {
	case Stmt::ImplicitCastExprClass:
		return extract_index_expr(cast<ImplicitCastExpr>(expr));
	case Stmt::DeclRefExprClass:
		return extract_index_expr(cast<DeclRefExpr>(expr));
	case Stmt::ArraySubscriptExprClass:
		return extract_index_expr(cast<ArraySubscriptExpr>(expr));
	case Stmt::IntegerLiteralClass:
		return extract_expr(cast<IntegerLiteral>(expr));
	case Stmt::MemberExprClass:
		return extract_index_expr(cast<MemberExpr>(expr));
	default:
		unsupported(expr);
	}
	return NULL;
}

/* Extract an index expression from the given array subscript expression.
 *
 * We first extract an index expression from the base.
 * This will result in an index expression with a range that corresponds
 * to the earlier indices.
 * We then extract the current index and let
 * pet_expr_access_subscript combine the two.
 */
__isl_give pet_expr *PetScan::extract_index_expr(ArraySubscriptExpr *expr)
{
	Expr *base = expr->getBase();
	Expr *idx = expr->getIdx();
	pet_expr *index;
	pet_expr *base_expr;

	base_expr = extract_index_expr(base);
	index = extract_expr(idx);

	base_expr = pet_expr_access_subscript(base_expr, index);

	return base_expr;
}

/* Extract an index expression from a member expression.
 *
 * If the base access (to the structure containing the member)
 * is of the form
 *
 *	A[..]
 *
 * and the member is called "f", then the member access is of
 * the form
 *
 *	A_f[A[..] -> f[]]
 *
 * If the member access is to an anonymous struct, then simply return
 *
 *	A[..]
 *
 * If the member access in the source code is of the form
 *
 *	A->f
 *
 * then it is treated as
 *
 *	A[0].f
 */
__isl_give pet_expr *PetScan::extract_index_expr(MemberExpr *expr)
{
	Expr *base = expr->getBase();
	FieldDecl *field = cast<FieldDecl>(expr->getMemberDecl());
	pet_expr *base_index;
	isl_id *id;

	base_index = extract_index_expr(base);

	if (expr->isArrow()) {
		pet_expr *index = pet_expr_new_int(isl_val_zero(ctx));
		base_index = pet_expr_access_subscript(base_index, index);
	}

	if (field->isAnonymousStructOrUnion())
		return base_index;

	id = pet_id_from_decl(ctx, field);

	return pet_expr_access_member(base_index, id);
}

/* Mark the given access pet_expr as a write.
 */
static __isl_give pet_expr *mark_write(__isl_take pet_expr *access)
{
	access = pet_expr_access_set_write(access, 1);
	access = pet_expr_access_set_read(access, 0);

	return access;
}

/* Mark the given (read) access pet_expr as also possibly being written.
 * That is, initialize the may write access relation from the may read relation
 * and initialize the must write access relation to the empty relation.
 */
static __isl_give pet_expr *mark_may_write(__isl_take pet_expr *expr)
{
	isl_union_map *access;
	isl_union_map *empty;

	access = pet_expr_access_get_dependent_access(expr,
						pet_expr_access_may_read);
	empty = isl_union_map_empty(isl_union_map_get_space(access));
	expr = pet_expr_access_set_access(expr, pet_expr_access_may_write,
					    access);
	expr = pet_expr_access_set_access(expr, pet_expr_access_must_write,
					    empty);

	return expr;
}

/* Construct a pet_expr representing a unary operator expression.
 */
__isl_give pet_expr *PetScan::extract_expr(UnaryOperator *expr)
{
	int type_size;
	pet_expr *arg;
	enum pet_op_type op;

	op = UnaryOperatorKind2pet_op_type(expr->getOpcode());
	if (op == pet_op_last) {
		report_unsupported_unary_operator(expr);
		return NULL;
	}

	arg = extract_expr(expr->getSubExpr());

	if (expr->isIncrementDecrementOp() &&
	    pet_expr_get_type(arg) == pet_expr_access) {
		arg = mark_write(arg);
		arg = pet_expr_access_set_read(arg, 1);
	}

	type_size = pet_clang_get_type_size(expr->getType(), ast_context);
	return pet_expr_new_unary(type_size, op, arg);
}

/* Construct a pet_expr representing a binary operator expression.
 *
 * If the top level operator is an assignment and the LHS is an access,
 * then we mark that access as a write.  If the operator is a compound
 * assignment, the access is marked as both a read and a write.
 */
__isl_give pet_expr *PetScan::extract_expr(BinaryOperator *expr)
{
	int type_size;
	pet_expr *lhs, *rhs;
	enum pet_op_type op;

	op = BinaryOperatorKind2pet_op_type(expr->getOpcode());
	if (op == pet_op_last) {
		report_unsupported_binary_operator(expr);
		return NULL;
	}

	lhs = extract_expr(expr->getLHS());
	rhs = extract_expr(expr->getRHS());

	if (expr->isAssignmentOp() &&
	    pet_expr_get_type(lhs) == pet_expr_access) {
		lhs = mark_write(lhs);
		if (expr->isCompoundAssignmentOp())
			lhs = pet_expr_access_set_read(lhs, 1);
	}

	type_size = pet_clang_get_type_size(expr->getType(), ast_context);
	return pet_expr_new_binary(type_size, op, lhs, rhs);
}

/* Construct a pet_tree for a variable declaration and
 * add the declaration to the list of declarations
 * inside the current compound statement.
 */
__isl_give pet_tree *PetScan::extract(Decl *decl)
{
	VarDecl *vd;
	pet_expr *lhs, *rhs;
	pet_tree *tree;

	if (!isa<VarDecl>(decl)) {
		report_unsupported_declaration(decl);
		return NULL;
	}

	vd = cast<VarDecl>(decl);
	declarations.push_back(vd);

	lhs = extract_access_expr(vd);
	lhs = mark_write(lhs);
	if (!vd->getInit())
		tree = pet_tree_new_decl(lhs);
	else {
		rhs = extract_expr(vd->getInit());
		tree = pet_tree_new_decl_init(lhs, rhs);
	}

	return tree;
}

/* Construct a pet_tree for a variable declaration statement.
 * If the declaration statement declares multiple variables,
 * then return a group of pet_trees, one for each declared variable.
 */
__isl_give pet_tree *PetScan::extract(DeclStmt *stmt)
{
	pet_tree *tree;
	unsigned n;

	if (!stmt->isSingleDecl()) {
		const DeclGroup &group = stmt->getDeclGroup().getDeclGroup();
		n = group.size();
		tree = pet_tree_new_block(ctx, 0, n);

		for (unsigned i = 0; i < n; ++i) {
			pet_tree *tree_i;
			pet_loc *loc;

			tree_i = extract(group[i]);
			loc = construct_pet_loc(group[i]->getSourceRange(),
						false);
			tree_i = pet_tree_set_loc(tree_i, loc);
			tree = pet_tree_block_add_child(tree, tree_i);
		}

		return tree;
	}

	return extract(stmt->getSingleDecl());
}

/* Construct a pet_expr representing a conditional operation.
 */
__isl_give pet_expr *PetScan::extract_expr(ConditionalOperator *expr)
{
	pet_expr *cond, *lhs, *rhs;

	cond = extract_expr(expr->getCond());
	lhs = extract_expr(expr->getTrueExpr());
	rhs = extract_expr(expr->getFalseExpr());

	return pet_expr_new_ternary(cond, lhs, rhs);
}

__isl_give pet_expr *PetScan::extract_expr(ImplicitCastExpr *expr)
{
	return extract_expr(expr->getSubExpr());
}

/* Construct a pet_expr representing a floating point value.
 *
 * If the floating point literal does not appear in a macro,
 * then we use the original representation in the source code
 * as the string representation.  Otherwise, we use the pretty
 * printer to produce a string representation.
 */
__isl_give pet_expr *PetScan::extract_expr(FloatingLiteral *expr)
{
	double d;
	string s;
	const LangOptions &LO = PP.getLangOpts();
	SourceLocation loc = expr->getLocation();

	if (!loc.isMacroID()) {
		SourceManager &SM = PP.getSourceManager();
		unsigned len = Lexer::MeasureTokenLength(loc, SM, LO);
		s = string(SM.getCharacterData(loc), len);
	} else {
		llvm::raw_string_ostream S(s);
		expr->printPretty(S, 0, PrintingPolicy(LO));
		S.str();
	}
	d = expr->getValueAsApproximateDouble();
	return pet_expr_new_double(ctx, d, s.c_str());
}

/* Extract an index expression from "expr" and then convert it into
 * an access pet_expr.
 *
 * If "expr" is a reference to an enum constant, then return
 * an integer expression instead, representing the value of the enum constant.
 */
__isl_give pet_expr *PetScan::extract_access_expr(Expr *expr)
{
	pet_expr *index;

	index = extract_index_expr(expr);

	if (pet_expr_get_type(index) == pet_expr_int)
		return index;

	return pet_expr_access_from_index(expr->getType(), index, ast_context);
}

/* Extract an index expression from "decl" and then convert it into
 * an access pet_expr.
 */
__isl_give pet_expr *PetScan::extract_access_expr(ValueDecl *decl)
{
	return pet_expr_access_from_index(decl->getType(),
					extract_index_expr(decl), ast_context);
}

__isl_give pet_expr *PetScan::extract_expr(ParenExpr *expr)
{
	return extract_expr(expr->getSubExpr());
}

/* Extract an assume statement from the argument "expr"
 * of a __builtin_assume or __pencil_assume statement.
 */
__isl_give pet_expr *PetScan::extract_assume(Expr *expr)
{
	return pet_expr_new_unary(0, pet_op_assume, extract_expr(expr));
}

/* If "expr" is an address-of operator, then return its argument.
 * Otherwise, return NULL.
 */
static Expr *extract_addr_of_arg(Expr *expr)
{
	UnaryOperator *op;

	if (expr->getStmtClass() != Stmt::UnaryOperatorClass)
		return NULL;
	op = cast<UnaryOperator>(expr);
	if (op->getOpcode() != UO_AddrOf)
		return NULL;
	return op->getSubExpr();
}

/* Construct a pet_expr corresponding to the function call argument "expr".
 * The argument appears in position "pos" of a call to function "fd".
 *
 * If we are passing along a pointer to an array element
 * or an entire row or even higher dimensional slice of an array,
 * then the function being called may write into the array.
 *
 * We assume here that if the function is declared to take a pointer
 * to a const type, then the function may only perform a read
 * and that otherwise, it may either perform a read or a write (or both).
 * We only perform this check if "detect_writes" is set.
 */
__isl_give pet_expr *PetScan::extract_argument(FunctionDecl *fd, int pos,
	Expr *expr, bool detect_writes)
{
	Expr *arg;
	pet_expr *res;
	int is_addr = 0, is_partial = 0;

	expr = pet_clang_strip_casts(expr);
	arg = extract_addr_of_arg(expr);
	if (arg) {
		is_addr = 1;
		expr = arg;
	}
	res = extract_expr(expr);
	if (!res)
		return NULL;
	if (pet_clang_array_depth(expr->getType()) > 0)
		is_partial = 1;
	if (detect_writes && (is_addr || is_partial) &&
	    pet_expr_get_type(res) == pet_expr_access) {
		ParmVarDecl *parm;
		if (!fd->hasPrototype()) {
			report_prototype_required(expr);
			return pet_expr_free(res);
		}
		parm = fd->getParamDecl(pos);
		if (!const_base(parm->getType()))
			res = mark_may_write(res);
	}

	if (is_addr)
		res = pet_expr_new_unary(0, pet_op_address_of, res);
	return res;
}

/* Find the first FunctionDecl with the given name.
 * "call" is the corresponding call expression and is only used
 * for reporting errors.
 *
 * Return NULL on error.
 */
FunctionDecl *PetScan::find_decl_from_name(CallExpr *call, string name)
{
	TranslationUnitDecl *tu = ast_context.getTranslationUnitDecl();
	DeclContext::decl_iterator begin = tu->decls_begin();
	DeclContext::decl_iterator end = tu->decls_end();
	for (DeclContext::decl_iterator i = begin; i != end; ++i) {
		FunctionDecl *fd = dyn_cast<FunctionDecl>(*i);
		if (!fd)
			continue;
		if (fd->getName().str().compare(name) != 0)
			continue;
		if (fd->hasBody())
			return fd;
		report_missing_summary_function_body(call);
		return NULL;
	}
	report_missing_summary_function(call);
	return NULL;
}

/* Return the FunctionDecl for the summary function associated to the
 * function called by "call".
 *
 * In particular, if the pencil option is set, then
 * search for an annotate attribute formatted as
 * "pencil_access(name)", where "name" is the name of the summary function.
 *
 * If no summary function was specified, then return the FunctionDecl
 * that is actually being called.
 *
 * Return NULL on error.
 */
FunctionDecl *PetScan::get_summary_function(CallExpr *call)
{
	FunctionDecl *decl = call->getDirectCallee();
	if (!decl)
		return NULL;

	if (!options->pencil)
		return decl;

	specific_attr_iterator<AnnotateAttr> begin, end, i;
	begin = decl->specific_attr_begin<AnnotateAttr>();
	end = decl->specific_attr_end<AnnotateAttr>();
	for (i = begin; i != end; ++i) {
		string attr = (*i)->getAnnotation().str();

		const char prefix[] = "pencil_access(";
		size_t start = attr.find(prefix);
		if (start == string::npos)
			continue;
		start += strlen(prefix);
		string name = attr.substr(start, attr.find(')') - start);

		return find_decl_from_name(call, name);
	}

	return decl;
}

/* Is "name" the name of an assume statement?
 * "pencil" indicates whether pencil builtins and pragmas should be supported.
 * "__builtin_assume" is always accepted.
 * If "pencil" is set, then "__pencil_assume" is also accepted.
 */
static bool is_assume(int pencil, const string &name)
{
	if (name == "__builtin_assume")
		return true;
	return pencil && name == "__pencil_assume";
}

/* Construct a pet_expr representing a function call.
 *
 * If this->call2id is not NULL and it contains a mapping for this call,
 * then this means that the corresponding function has been inlined.
 * Return a pet_expr that reads from the variable that
 * stores the return value of the inlined call.
 *
 * In the special case of a "call" to __builtin_assume or __pencil_assume,
 * construct an assume expression instead.
 *
 * In the case of a "call" to __pencil_kill, the arguments
 * are neither read nor written (only killed), so there
 * is no need to check for writes to these arguments.
 *
 * __pencil_assume and __pencil_kill are only recognized
 * when the pencil option is set.
 */
__isl_give pet_expr *PetScan::extract_expr(CallExpr *expr)
{
	pet_expr *res = NULL;
	FunctionDecl *fd;
	string name;
	unsigned n_arg;
	bool is_kill;

	if (call2id && call2id->find(expr) != call2id->end())
		return pet_expr_access_from_id(isl_id_copy(call2id[0][expr]),
						ast_context);

	fd = expr->getDirectCallee();
	if (!fd) {
		unsupported(expr);
		return NULL;
	}

	name = fd->getDeclName().getAsString();
	n_arg = expr->getNumArgs();

	if (n_arg == 1 && is_assume(options->pencil, name))
		return extract_assume(expr->getArg(0));
	is_kill = options->pencil && name == "__pencil_kill";

	res = pet_expr_new_call(ctx, name.c_str(), n_arg);
	if (!res)
		return NULL;

	for (unsigned i = 0; i < n_arg; ++i) {
		Expr *arg = expr->getArg(i);
		res = pet_expr_set_arg(res, i,
			    PetScan::extract_argument(fd, i, arg, !is_kill));
	}

	fd = get_summary_function(expr);
	if (!fd)
		return pet_expr_free(res);

	res = set_summary(res, fd);

	return res;
}

/* Construct a pet_expr representing a (C style) cast.
 */
__isl_give pet_expr *PetScan::extract_expr(CStyleCastExpr *expr)
{
	pet_expr *arg;
	QualType type;

	arg = extract_expr(expr->getSubExpr());
	if (!arg)
		return NULL;

	type = expr->getTypeAsWritten();
	return pet_expr_new_cast(type.getAsString().c_str(), arg);
}

/* Construct a pet_expr representing an integer.
 */
__isl_give pet_expr *PetScan::extract_expr(IntegerLiteral *expr)
{
	return pet_expr_new_int(extract_int(expr));
}

/* Construct a pet_expr representing the integer enum constant "ecd".
 */
__isl_give pet_expr *PetScan::extract_expr(EnumConstantDecl *ecd)
{
	isl_val *v;
	const llvm::APSInt &init = ecd->getInitVal();
	v = ::extract_int(ctx, init.isSigned(), init);
	return pet_expr_new_int(v);
}

/* Try and construct a pet_expr representing "expr".
 */
__isl_give pet_expr *PetScan::extract_expr(Expr *expr)
{
	switch (expr->getStmtClass()) {
	case Stmt::UnaryOperatorClass:
		return extract_expr(cast<UnaryOperator>(expr));
	case Stmt::CompoundAssignOperatorClass:
	case Stmt::BinaryOperatorClass:
		return extract_expr(cast<BinaryOperator>(expr));
	case Stmt::ImplicitCastExprClass:
		return extract_expr(cast<ImplicitCastExpr>(expr));
	case Stmt::ArraySubscriptExprClass:
	case Stmt::DeclRefExprClass:
	case Stmt::MemberExprClass:
		return extract_access_expr(expr);
	case Stmt::IntegerLiteralClass:
		return extract_expr(cast<IntegerLiteral>(expr));
	case Stmt::FloatingLiteralClass:
		return extract_expr(cast<FloatingLiteral>(expr));
	case Stmt::ParenExprClass:
		return extract_expr(cast<ParenExpr>(expr));
	case Stmt::ConditionalOperatorClass:
		return extract_expr(cast<ConditionalOperator>(expr));
	case Stmt::CallExprClass:
		return extract_expr(cast<CallExpr>(expr));
	case Stmt::CStyleCastExprClass:
		return extract_expr(cast<CStyleCastExpr>(expr));
	default:
		unsupported(expr);
	}
	return NULL;
}

/* Check if the given initialization statement is an assignment.
 * If so, return that assignment.  Otherwise return NULL.
 */
BinaryOperator *PetScan::initialization_assignment(Stmt *init)
{
	BinaryOperator *ass;

	if (init->getStmtClass() != Stmt::BinaryOperatorClass)
		return NULL;

	ass = cast<BinaryOperator>(init);
	if (ass->getOpcode() != BO_Assign)
		return NULL;

	return ass;
}

/* Check if the given initialization statement is a declaration
 * of a single variable.
 * If so, return that declaration.  Otherwise return NULL.
 */
Decl *PetScan::initialization_declaration(Stmt *init)
{
	DeclStmt *decl;

	if (init->getStmtClass() != Stmt::DeclStmtClass)
		return NULL;

	decl = cast<DeclStmt>(init);

	if (!decl->isSingleDecl())
		return NULL;

	return decl->getSingleDecl();
}

/* Given the assignment operator in the initialization of a for loop,
 * extract the induction variable, i.e., the (integer)variable being
 * assigned.
 */
ValueDecl *PetScan::extract_induction_variable(BinaryOperator *init)
{
	Expr *lhs;
	DeclRefExpr *ref;
	ValueDecl *decl;
	const Type *type;

	lhs = init->getLHS();
	if (lhs->getStmtClass() != Stmt::DeclRefExprClass) {
		unsupported(init);
		return NULL;
	}

	ref = cast<DeclRefExpr>(lhs);
	decl = ref->getDecl();
	type = decl->getType().getTypePtr();

	if (!type->isIntegerType()) {
		unsupported(lhs);
		return NULL;
	}

	return decl;
}

/* Given the initialization statement of a for loop and the single
 * declaration in this initialization statement,
 * extract the induction variable, i.e., the (integer) variable being
 * declared.
 */
VarDecl *PetScan::extract_induction_variable(Stmt *init, Decl *decl)
{
	VarDecl *vd;

	vd = cast<VarDecl>(decl);

	const QualType type = vd->getType();
	if (!type->isIntegerType()) {
		unsupported(init);
		return NULL;
	}

	if (!vd->getInit()) {
		unsupported(init);
		return NULL;
	}

	return vd;
}

/* Check that op is of the form iv++ or iv--.
 * Return a pet_expr representing "1" or "-1" accordingly.
 */
__isl_give pet_expr *PetScan::extract_unary_increment(
	clang::UnaryOperator *op, clang::ValueDecl *iv)
{
	Expr *sub;
	DeclRefExpr *ref;
	isl_val *v;

	if (!op->isIncrementDecrementOp()) {
		unsupported(op);
		return NULL;
	}

	sub = op->getSubExpr();
	if (sub->getStmtClass() != Stmt::DeclRefExprClass) {
		unsupported(op);
		return NULL;
	}

	ref = cast<DeclRefExpr>(sub);
	if (ref->getDecl() != iv) {
		unsupported(op);
		return NULL;
	}

	if (op->isIncrementOp())
		v = isl_val_one(ctx);
	else
		v = isl_val_negone(ctx);

	return pet_expr_new_int(v);
}

/* Check if op is of the form
 *
 *	iv = expr
 *
 * and return the increment "expr - iv" as a pet_expr.
 */
__isl_give pet_expr *PetScan::extract_binary_increment(BinaryOperator *op,
	clang::ValueDecl *iv)
{
	int type_size;
	Expr *lhs;
	DeclRefExpr *ref;
	pet_expr *expr, *expr_iv;

	if (op->getOpcode() != BO_Assign) {
		unsupported(op);
		return NULL;
	}

	lhs = op->getLHS();
	if (lhs->getStmtClass() != Stmt::DeclRefExprClass) {
		unsupported(op);
		return NULL;
	}

	ref = cast<DeclRefExpr>(lhs);
	if (ref->getDecl() != iv) {
		unsupported(op);
		return NULL;
	}

	expr = extract_expr(op->getRHS());
	expr_iv = extract_expr(lhs);

	type_size = pet_clang_get_type_size(iv->getType(), ast_context);
	return pet_expr_new_binary(type_size, pet_op_sub, expr, expr_iv);
}

/* Check that op is of the form iv += cst or iv -= cst
 * and return a pet_expr corresponding to cst or -cst accordingly.
 */
__isl_give pet_expr *PetScan::extract_compound_increment(
	CompoundAssignOperator *op, clang::ValueDecl *iv)
{
	Expr *lhs;
	DeclRefExpr *ref;
	bool neg = false;
	pet_expr *expr;
	BinaryOperatorKind opcode;

	opcode = op->getOpcode();
	if (opcode != BO_AddAssign && opcode != BO_SubAssign) {
		unsupported(op);
		return NULL;
	}
	if (opcode == BO_SubAssign)
		neg = true;

	lhs = op->getLHS();
	if (lhs->getStmtClass() != Stmt::DeclRefExprClass) {
		unsupported(op);
		return NULL;
	}

	ref = cast<DeclRefExpr>(lhs);
	if (ref->getDecl() != iv) {
		unsupported(op);
		return NULL;
	}

	expr = extract_expr(op->getRHS());
	if (neg) {
		int type_size;
		type_size = pet_clang_get_type_size(op->getType(), ast_context);
		expr = pet_expr_new_unary(type_size, pet_op_minus, expr);
	}

	return expr;
}

/* Check that the increment of the given for loop increments
 * (or decrements) the induction variable "iv" and return
 * the increment as a pet_expr if successful.
 */
__isl_give pet_expr *PetScan::extract_increment(clang::ForStmt *stmt,
	ValueDecl *iv)
{
	Stmt *inc = stmt->getInc();

	if (!inc) {
		report_missing_increment(stmt);
		return NULL;
	}

	if (inc->getStmtClass() == Stmt::UnaryOperatorClass)
		return extract_unary_increment(cast<UnaryOperator>(inc), iv);
	if (inc->getStmtClass() == Stmt::CompoundAssignOperatorClass)
		return extract_compound_increment(
				cast<CompoundAssignOperator>(inc), iv);
	if (inc->getStmtClass() == Stmt::BinaryOperatorClass)
		return extract_binary_increment(cast<BinaryOperator>(inc), iv);

	unsupported(inc);
	return NULL;
}

/* Construct a pet_tree for a while loop.
 *
 * If we were only able to extract part of the body, then simply
 * return that part.
 */
__isl_give pet_tree *PetScan::extract(WhileStmt *stmt)
{
	pet_expr *pe_cond;
	pet_tree *tree;

	tree = extract(stmt->getBody());
	if (partial)
		return tree;
	pe_cond = extract_expr(stmt->getCond());
	tree = pet_tree_new_while(pe_cond, tree);

	return tree;
}

/* Construct a pet_tree for a for statement.
 * The for loop is required to be of one of the following forms
 *
 *	for (i = init; condition; ++i)
 *	for (i = init; condition; --i)
 *	for (i = init; condition; i += constant)
 *	for (i = init; condition; i -= constant)
 *
 * We extract a pet_tree for the body and then include it in a pet_tree
 * of type pet_tree_for.
 *
 * As a special case, we also allow a for loop of the form
 *
 *	for (;;)
 *
 * in which case we return a pet_tree of type pet_tree_infinite_loop.
 *
 * If we were only able to extract part of the body, then simply
 * return that part.
 */
__isl_give pet_tree *PetScan::extract_for(ForStmt *stmt)
{
	BinaryOperator *ass;
	Decl *decl;
	Stmt *init;
	Expr *rhs;
	ValueDecl *iv;
	pet_tree *tree;
	int independent;
	int declared;
	pet_expr *pe_init, *pe_inc, *pe_iv, *pe_cond;

	independent = is_current_stmt_marked_independent();

	if (!stmt->getInit() && !stmt->getCond() && !stmt->getInc()) {
		tree = extract(stmt->getBody());
		if (partial)
			return tree;
		tree = pet_tree_new_infinite_loop(tree);
		return tree;
	}

	init = stmt->getInit();
	if (!init) {
		unsupported(stmt);
		return NULL;
	}
	if ((ass = initialization_assignment(init)) != NULL) {
		iv = extract_induction_variable(ass);
		if (!iv)
			return NULL;
		rhs = ass->getRHS();
	} else if ((decl = initialization_declaration(init)) != NULL) {
		VarDecl *var = extract_induction_variable(init, decl);
		if (!var)
			return NULL;
		iv = var;
		rhs = var->getInit();
	} else {
		unsupported(stmt->getInit());
		return NULL;
	}

	declared = !initialization_assignment(stmt->getInit());
	tree = extract(stmt->getBody());
	if (partial)
		return tree;
	pe_iv = extract_access_expr(iv);
	pe_iv = mark_write(pe_iv);
	pe_init = extract_expr(rhs);
	if (!stmt->getCond())
		pe_cond = pet_expr_new_int(isl_val_one(ctx));
	else
		pe_cond = extract_expr(stmt->getCond());
	pe_inc = extract_increment(stmt, iv);
	tree = pet_tree_new_for(independent, declared, pe_iv, pe_init, pe_cond,
				pe_inc, tree);
	return tree;
}

/* Store the names of the variables declared in decl_context
 * in the set declared_names.  Make sure to only do this once by
 * setting declared_names_collected.
 */
void PetScan::collect_declared_names()
{
	DeclContext *DC = decl_context;
	DeclContext::decl_iterator it;

	if (declared_names_collected)
		return;

	for (it = DC->decls_begin(); it != DC->decls_end(); ++it) {
		Decl *D = *it;
		NamedDecl *named;

		if (!isa<NamedDecl>(D))
			continue;
		named = cast<NamedDecl>(D);
		declared_names.insert(named->getName().str());
	}

	declared_names_collected = true;
}

/* Add the names in "names" that are not also in this->declared_names
 * to this->used_names.
 * It is up to the caller to make sure that declared_names has been
 * populated, if needed.
 */
void PetScan::add_new_used_names(const std::set<std::string> &names)
{
	std::set<std::string>::const_iterator it;

	for (it = names.begin(); it != names.end(); ++it) {
		if (declared_names.find(*it) != declared_names.end())
			continue;
		used_names.insert(*it);
	}
}

/* Is the name "name" used in any declaration other than "decl"?
 *
 * If the name was found to be in use before, the consider it to be in use.
 * Otherwise, check the DeclContext of the function containing the scop
 * as well as all ancestors of this DeclContext for declarations
 * other than "decl" that declare something called "name".
 */
bool PetScan::name_in_use(const string &name, Decl *decl)
{
	DeclContext *DC;
	DeclContext::decl_iterator it;

	if (used_names.find(name) != used_names.end())
		return true;

	for (DC = decl_context; DC; DC = DC->getParent()) {
		for (it = DC->decls_begin(); it != DC->decls_end(); ++it) {
			Decl *D = *it;
			NamedDecl *named;

			if (D == decl)
				continue;
			if (!isa<NamedDecl>(D))
				continue;
			named = cast<NamedDecl>(D);
			if (named->getName().str() == name)
				return true;
		}
	}

	return false;
}

/* Generate a new name based on "name" that is not in use.
 * Do so by adding a suffix _i, with i an integer.
 */
string PetScan::generate_new_name(const string &name)
{
	string new_name;

	do {
		std::ostringstream oss;
		oss << name << "_" << n_rename++;
		new_name = oss.str();
	} while (name_in_use(new_name, NULL));

	return new_name;
}

/* Try and construct a pet_tree corresponding to a compound statement.
 *
 * "skip_declarations" is set if we should skip initial declarations
 * in the children of the compound statements.
 *
 * Collect a new set of declarations for the current compound statement.
 * If any of the names in these declarations is also used by another
 * declaration reachable from the current function, then rename it
 * to a name that is not already in use.
 * In particular, keep track of the old and new names in a pet_substituter
 * and apply the substitutions to the pet_tree corresponding to the
 * compound statement.
 */
__isl_give pet_tree *PetScan::extract(CompoundStmt *stmt,
	bool skip_declarations)
{
	pet_tree *tree;
	std::vector<VarDecl *> saved_declarations;
	std::vector<VarDecl *>::iterator it;
	pet_substituter substituter;

	saved_declarations = declarations;
	declarations.clear();
	tree = extract(stmt->children(), true, skip_declarations, stmt);
	for (it = declarations.begin(); it != declarations.end(); ++it) {
		isl_id *id;
		pet_expr *expr;
		VarDecl *decl = *it;
		string name = decl->getName().str();
		bool in_use = name_in_use(name, decl);

		used_names.insert(name);
		if (!in_use)
			continue;

		name = generate_new_name(name);
		id = pet_id_from_name_and_decl(ctx, name.c_str(), decl);
		expr = pet_expr_access_from_id(id, ast_context);
		id = pet_id_from_decl(ctx, decl);
		substituter.add_sub(id, expr);
		used_names.insert(name);
	}
	tree = substituter.substitute(tree);
	declarations = saved_declarations;

	return tree;
}

/* Return the file offset of the expansion location of "Loc".
 */
static unsigned getExpansionOffset(SourceManager &SM, SourceLocation Loc)
{
	return SM.getFileOffset(SM.getExpansionLoc(Loc));
}

#ifdef HAVE_FINDLOCATIONAFTERTOKEN

/* Return a SourceLocation for the location after the first semicolon
 * after "loc".  If Lexer::findLocationAfterToken is available, we simply
 * call it and also skip trailing spaces and newline.
 */
static SourceLocation location_after_semi(SourceLocation loc, SourceManager &SM,
	const LangOptions &LO)
{
	return Lexer::findLocationAfterToken(loc, tok::semi, SM, LO, true);
}

#else

/* Return a SourceLocation for the location after the first semicolon
 * after "loc".  If Lexer::findLocationAfterToken is not available,
 * we look in the underlying character data for the first semicolon.
 */
static SourceLocation location_after_semi(SourceLocation loc, SourceManager &SM,
	const LangOptions &LO)
{
	const char *semi;
	const char *s = SM.getCharacterData(loc);

	semi = strchr(s, ';');
	if (!semi)
		return SourceLocation();
	return loc.getFileLocWithOffset(semi + 1 - s);
}

#endif

/* If the token at "loc" is the first token on the line, then return
 * a location referring to the start of the line and set *indent
 * to the indentation of "loc"
 * Otherwise, return "loc" and set *indent to "".
 *
 * This function is used to extend a scop to the start of the line
 * if the first token of the scop is also the first token on the line.
 *
 * We look for the first token on the line.  If its location is equal to "loc",
 * then the latter is the location of the first token on the line.
 */
static SourceLocation move_to_start_of_line_if_first_token(SourceLocation loc,
	SourceManager &SM, const LangOptions &LO, char **indent)
{
	std::pair<FileID, unsigned> file_offset_pair;
	llvm::StringRef file;
	const char *pos;
	Token tok;
	SourceLocation token_loc, line_loc;
	int col;
	const char *s;

	loc = SM.getExpansionLoc(loc);
	col = SM.getExpansionColumnNumber(loc);
	line_loc = loc.getLocWithOffset(1 - col);
	file_offset_pair = SM.getDecomposedLoc(line_loc);
	file = SM.getBufferData(file_offset_pair.first, NULL);
	pos = file.data() + file_offset_pair.second;

	Lexer lexer(SM.getLocForStartOfFile(file_offset_pair.first), LO,
					file.begin(), pos, file.end());
	lexer.LexFromRawLexer(tok);
	token_loc = tok.getLocation();

	s = SM.getCharacterData(line_loc);
	*indent = strndup(s, token_loc == loc ? col - 1 : 0);

	if (token_loc == loc)
		return line_loc;
	else
		return loc;
}

/* Construct a pet_loc corresponding to the region covered by "range".
 * If "skip_semi" is set, then we assume "range" is followed by
 * a semicolon and also include this semicolon.
 */
__isl_give pet_loc *PetScan::construct_pet_loc(SourceRange range,
	bool skip_semi)
{
	SourceLocation loc = range.getBegin();
	SourceManager &SM = PP.getSourceManager();
	const LangOptions &LO = PP.getLangOpts();
	int line = PP.getSourceManager().getExpansionLineNumber(loc);
	unsigned start, end;
	char *indent;

	loc = move_to_start_of_line_if_first_token(loc, SM, LO, &indent);
	start = getExpansionOffset(SM, loc);
	loc = range.getEnd();
	if (skip_semi)
		loc = location_after_semi(loc, SM, LO);
	else
		loc = PP.getLocForEndOfToken(loc);
	end = getExpansionOffset(SM, loc);

	return pet_loc_alloc(ctx, start, end, line, indent);
}

/* Convert a top-level pet_expr to an expression pet_tree.
 */
__isl_give pet_tree *PetScan::extract(__isl_take pet_expr *expr,
	SourceRange range, bool skip_semi)
{
	pet_loc *loc;
	pet_tree *tree;

	tree = pet_tree_new_expr(expr);
	loc = construct_pet_loc(range, skip_semi);
	tree = pet_tree_set_loc(tree, loc);

	return tree;
}

/* Construct a pet_tree for an if statement.
 */
__isl_give pet_tree *PetScan::extract(IfStmt *stmt)
{
	pet_expr *pe_cond;
	pet_tree *tree, *tree_else;

	pe_cond = extract_expr(stmt->getCond());
	tree = extract(stmt->getThen());
	if (stmt->getElse()) {
		tree_else = extract(stmt->getElse());
		if (options->autodetect) {
			if (tree && !tree_else) {
				partial = true;
				pet_expr_free(pe_cond);
				return tree;
			}
			if (!tree && tree_else) {
				partial = true;
				pet_expr_free(pe_cond);
				return tree_else;
			}
		}
		tree = pet_tree_new_if_else(pe_cond, tree, tree_else);
	} else
		tree = pet_tree_new_if(pe_cond, tree);
	return tree;
}

/* Is "parent" a compound statement that has "stmt" as its final child?
 */
static bool final_in_compound(ReturnStmt *stmt, Stmt *parent)
{
	CompoundStmt *c;

	c = dyn_cast<CompoundStmt>(parent);
	if (c) {
		StmtIterator i;
		Stmt *last;
		StmtRange range = c->children();

		for (i = range.first; i != range.second; ++i)
			last = *i;
		return last == stmt;
	}
	return false;
}

/* Try and construct a pet_tree for a return statement "stmt".
 *
 * Return statements are only allowed in a context where
 * this->return_root has been set.
 * Furthermore, "stmt" should appear as the last child
 * in the compound statement this->return_root.
 */
__isl_give pet_tree *PetScan::extract(ReturnStmt *stmt)
{
	pet_expr *val;

	if (!return_root) {
		report_unsupported_return(stmt);
		return NULL;
	}
	if (!final_in_compound(stmt, return_root)) {
		report_return_not_at_end_of_function(stmt);
		return NULL;
	}

	val = extract_expr(stmt->getRetValue());
	return pet_tree_new_return(val);
}

/* Try and construct a pet_tree for a label statement.
 */
__isl_give pet_tree *PetScan::extract(LabelStmt *stmt)
{
	isl_id *label;
	pet_tree *tree;

	label = isl_id_alloc(ctx, stmt->getName(), NULL);

	tree = extract(stmt->getSubStmt());
	tree = pet_tree_set_label(tree, label);
	return tree;
}

/* Update the location of "tree" to include the source range of "stmt".
 *
 * Actually, we create a new location based on the source range of "stmt" and
 * then extend this new location to include the region of the original location.
 * This ensures that the line number of the final location refers to "stmt".
 */
__isl_give pet_tree *PetScan::update_loc(__isl_take pet_tree *tree, Stmt *stmt)
{
	pet_loc *loc, *tree_loc;

	tree_loc = pet_tree_get_loc(tree);
	loc = construct_pet_loc(stmt->getSourceRange(), false);
	loc = pet_loc_update_start_end_from_loc(loc, tree_loc);
	pet_loc_free(tree_loc);

	tree = pet_tree_set_loc(tree, loc);
	return tree;
}

/* Is "expr" of a type that can be converted to an access expression?
 */
static bool is_access_expr_type(Expr *expr)
{
	switch (expr->getStmtClass()) {
	case Stmt::ArraySubscriptExprClass:
	case Stmt::DeclRefExprClass:
	case Stmt::MemberExprClass:
		return true;
	default:
		return false;
	}
}

/* Tell the pet_inliner "inliner" about the formal arguments
 * in "fd" and the corresponding actual arguments in "call".
 * Return 0 if this was successful and -1 otherwise.
 *
 * Any pointer argument is treated as an array.
 * The other arguments are treated as scalars.
 *
 * In case of scalars, there is no restriction on the actual argument.
 * This actual argument is assigned to a variable with a name
 * that is derived from the name of the corresponding formal argument,
 * but made not to conflict with any variable names that are
 * already in use.
 *
 * In case of arrays, the actual argument needs to be an expression
 * of a type that can be converted to an access expression or the address
 * of such an expression, ignoring implicit and redundant casts.
 */
int PetScan::set_inliner_arguments(pet_inliner &inliner, CallExpr *call,
	FunctionDecl *fd)
{
	unsigned n;

	n = fd->getNumParams();
	for (unsigned i = 0; i < n; ++i) {
		ParmVarDecl *parm = fd->getParamDecl(i);
		QualType type = parm->getType();
		Expr *arg, *sub;
		pet_expr *expr;
		int is_addr = 0;

		arg = call->getArg(i);
		if (pet_clang_array_depth(type) == 0) {
			string name = parm->getName().str();
			if (name_in_use(name, NULL))
				name = generate_new_name(name);
			used_names.insert(name);
			inliner.add_scalar_arg(parm, name, extract_expr(arg));
			continue;
		}
		arg = pet_clang_strip_casts(arg);
		sub = extract_addr_of_arg(arg);
		if (sub) {
			is_addr = 1;
			arg = pet_clang_strip_casts(sub);
		}
		if (!is_access_expr_type(arg)) {
			report_unsupported_inline_function_argument(arg);
			return -1;
		}
		expr = extract_access_expr(arg);
		if (!expr)
			return -1;
		inliner.add_array_arg(parm, expr, is_addr);
	}

	return 0;
}

/* Internal data structure for PetScan::substitute_array_sizes.
 * ps is the PetScan on which the method was called.
 * substituter is the substituter that is used to substitute variables
 * in the size expressions.
 */
struct pet_substitute_array_sizes_data {
	PetScan *ps;
	pet_substituter *substituter;
};

extern "C" {
	static int substitute_array_size(__isl_keep pet_tree *tree, void *user);
}

/* If "tree" is a declaration, then perform the substitutions
 * in data->substituter on its size expression and store the result
 * in the size expression cache of data->ps such that the modified expression
 * will be used in subsequent calls to get_array_size.
 */
static int substitute_array_size(__isl_keep pet_tree *tree, void *user)
{
	struct pet_substitute_array_sizes_data *data;
	isl_id *id;
	pet_expr *var, *size;

	if (!pet_tree_is_decl(tree))
		return 0;

	data = (struct pet_substitute_array_sizes_data *) user;
	var = pet_tree_decl_get_var(tree);
	id = pet_expr_access_get_id(var);
	pet_expr_free(var);

	size = data->ps->get_array_size(id);
	size = data->substituter->substitute(size);
	data->ps->set_array_size(id, size);

	return 0;
}

/* Perform the substitutions in "substituter" on all the arrays declared
 * inside "tree" and store the results in the size expression cache
 * such that the modified expressions will be used in subsequent calls
 * to get_array_size.
 */
int PetScan::substitute_array_sizes(__isl_keep pet_tree *tree,
	pet_substituter *substituter)
{
	struct pet_substitute_array_sizes_data data = { this, substituter };

	return pet_tree_foreach_sub_tree(tree, &substitute_array_size, &data);
}

/* Try and construct a pet_tree from the body of "fd" using the actual
 * arguments in "call" in place of the formal arguments.
 * "fd" is assumed to point to the declaration with a function body.
 * In particular, construct a block that consists of assignments
 * of (parts of) the actual arguments to temporary variables
 * followed by the inlined function body with the formal arguments
 * replaced by (expressions containing) these temporary variables.
 * If "return_id" is set, then it is used to store the return value
 * of the inlined function.
 *
 * The actual inlining is taken care of by the pet_inliner object.
 * This function merely calls set_inliner_arguments to tell
 * the pet_inliner about the actual arguments, extracts a pet_tree
 * from the body of the called function and then passes this pet_tree
 * to the pet_inliner.
 * The body of the called function is allowed to have a return statement
 * at the end.
 * The substitutions performed by the inliner are also applied
 * to the size expressions of the arrays declared in the inlined
 * function.  These size expressions are not stored in the tree
 * itself, but rather in the size expression cache.
 *
 * During the extraction of the function body, all variables names
 * that are declared in the calling function as well all variable
 * names that are known to be in use are considered to be in use
 * in the called function to ensure that there is no naming conflict.
 * Similarly, the additional names that are in use in the called function
 * are considered to be in use in the calling function as well.
 *
 * The location of the pet_tree is reset to the call site to ensure
 * that the extent of the scop does not include the body of the called
 * function.
 */
__isl_give pet_tree *PetScan::extract_inlined_call(CallExpr *call,
	FunctionDecl *fd, __isl_keep isl_id *return_id)
{
	int save_autodetect;
	pet_tree *tree;
	pet_loc *tree_loc;
	pet_inliner inliner(ctx, n_arg, ast_context);

	if (set_inliner_arguments(inliner, call, fd) < 0)
		return NULL;

	save_autodetect = options->autodetect;
	options->autodetect = 0;
	PetScan body_scan(PP, ast_context, fd, loc, options,
				isl_union_map_copy(value_bounds), independent);
	collect_declared_names();
	body_scan.add_new_used_names(declared_names);
	body_scan.add_new_used_names(used_names);
	body_scan.return_root = fd->getBody();
	tree = body_scan.extract(fd->getBody(), false);
	add_new_used_names(body_scan.used_names);
	options->autodetect = save_autodetect;

	tree_loc = construct_pet_loc(call->getSourceRange(), true);
	tree = pet_tree_set_loc(tree, tree_loc);

	substitute_array_sizes(tree, &inliner);

	return inliner.inline_tree(tree, return_id);
}

/* Try and construct a pet_tree corresponding
 * to the expression statement "stmt".
 *
 * First look for function calls that have corresponding bodies
 * marked "inline".  Extract the inlined functions in a pet_inlined_calls
 * object.  Then extract the statement itself, replacing calls
 * to inlined function by accesses to the corresponding return variables, and
 * return the combined result.
 * If the outer expression is itself a call to an inlined function,
 * then it already appears as one of the inlined functions and
 * no separate pet_tree needs to be extracted for "stmt" itself.
 */
__isl_give pet_tree *PetScan::extract_expr_stmt(Stmt *stmt)
{
	pet_expr *expr;
	pet_tree *tree;
	pet_inlined_calls ic(this);

	ic.collect(stmt);
	if (ic.calls.size() >= 1 && ic.calls[0] == stmt) {
		tree = pet_tree_new_block(ctx, 0, 0);
	} else {
		call2id = &ic.call2id;
		expr = extract_expr(cast<Expr>(stmt));
		tree = extract(expr, stmt->getSourceRange(), true);
		call2id = NULL;
	}
	tree = ic.add_inlined(tree);
	return tree;
}

/* Try and construct a pet_tree corresponding to "stmt".
 *
 * If "stmt" is a compound statement, then "skip_declarations"
 * indicates whether we should skip initial declarations in the
 * compound statement.
 *
 * If the constructed pet_tree is not a (possibly) partial representation
 * of "stmt", we update start and end of the pet_scop to those of "stmt".
 * In particular, if skip_declarations is set, then we may have skipped
 * declarations inside "stmt" and so the pet_scop may not represent
 * the entire "stmt".
 * Note that this function may be called with "stmt" referring to the entire
 * body of the function, including the outer braces.  In such cases,
 * skip_declarations will be set and the braces will not be taken into
 * account in tree->loc.
 */
__isl_give pet_tree *PetScan::extract(Stmt *stmt, bool skip_declarations)
{
	pet_tree *tree;

	set_current_stmt(stmt);

	if (isa<Expr>(stmt))
		return extract_expr_stmt(cast<Expr>(stmt));

	switch (stmt->getStmtClass()) {
	case Stmt::WhileStmtClass:
		tree = extract(cast<WhileStmt>(stmt));
		break;
	case Stmt::ForStmtClass:
		tree = extract_for(cast<ForStmt>(stmt));
		break;
	case Stmt::IfStmtClass:
		tree = extract(cast<IfStmt>(stmt));
		break;
	case Stmt::CompoundStmtClass:
		tree = extract(cast<CompoundStmt>(stmt), skip_declarations);
		break;
	case Stmt::LabelStmtClass:
		tree = extract(cast<LabelStmt>(stmt));
		break;
	case Stmt::ContinueStmtClass:
		tree = pet_tree_new_continue(ctx);
		break;
	case Stmt::BreakStmtClass:
		tree = pet_tree_new_break(ctx);
		break;
	case Stmt::DeclStmtClass:
		tree = extract(cast<DeclStmt>(stmt));
		break;
	case Stmt::NullStmtClass:
		tree = pet_tree_new_block(ctx, 0, 0);
		break;
	case Stmt::ReturnStmtClass:
		tree = extract(cast<ReturnStmt>(stmt));
		break;
	default:
		report_unsupported_statement_type(stmt);
		return NULL;
	}

	if (partial || skip_declarations)
		return tree;

	return update_loc(tree, stmt);
}

/* Given a sequence of statements "stmt_range" of which the first "n_decl"
 * are declarations and of which the remaining statements are represented
 * by "tree", try and extend "tree" to include the last sequence of
 * the initial declarations that can be completely extracted.
 *
 * We start collecting the initial declarations and start over
 * whenever we come across a declaration that we cannot extract.
 * If we have been able to extract any declarations, then we
 * copy over the contents of "tree" at the end of the declarations.
 * Otherwise, we simply return the original "tree".
 */
__isl_give pet_tree *PetScan::insert_initial_declarations(
	__isl_take pet_tree *tree, int n_decl, StmtRange stmt_range)
{
	StmtIterator i;
	pet_tree *res;
	int n_stmt;
	int is_block;
	int j;

	n_stmt = pet_tree_block_n_child(tree);
	is_block = pet_tree_block_get_block(tree);
	res = pet_tree_new_block(ctx, is_block, n_decl + n_stmt);

	for (i = stmt_range.first; n_decl; ++i, --n_decl) {
		Stmt *child = *i;
		pet_tree *tree_i;

		tree_i = extract(child);
		if (tree_i && !partial) {
			res = pet_tree_block_add_child(res, tree_i);
			continue;
		}
		pet_tree_free(tree_i);
		partial = false;
		if (pet_tree_block_n_child(res) == 0)
			continue;
		pet_tree_free(res);
		res = pet_tree_new_block(ctx, is_block, n_decl + n_stmt);
	}

	if (pet_tree_block_n_child(res) == 0) {
		pet_tree_free(res);
		return tree;
	}

	for (j = 0; j < n_stmt; ++j) {
		pet_tree *tree_i;

		tree_i = pet_tree_block_get_child(tree, j);
		res = pet_tree_block_add_child(res, tree_i);
	}
	pet_tree_free(tree);

	return res;
}

/* Try and construct a pet_tree corresponding to (part of)
 * a sequence of statements.
 *
 * "block" is set if the sequence represents the children of
 * a compound statement.
 * "skip_declarations" is set if we should skip initial declarations
 * in the sequence of statements.
 * "parent" is the statement that has stmt_range as (some of) its children.
 *
 * If autodetect is set, then we allow the extraction of only a subrange
 * of the sequence of statements.  However, if there is at least one
 * kill and there is some subsequent statement for which we could not
 * construct a tree, then turn off the "block" property of the tree
 * such that no extra kill will be introduced at the end of the (partial)
 * block.  If, on the other hand, the final range contains
 * no statements, then we discard the entire range.
 * If only a subrange of the sequence was extracted, but each statement
 * in the sequence was extracted completely, and if there are some
 * variable declarations in the sequence before or inside
 * the extracted subrange, then check if any of these variables are
 * not used after the extracted subrange.  If so, add kills to these
 * variables.
 *
 * If the entire range was extracted, apart from some initial declarations,
 * then we try and extend the range with the latest of those initial
 * declarations.
 */
__isl_give pet_tree *PetScan::extract(StmtRange stmt_range, bool block,
	bool skip_declarations, Stmt *parent)
{
	StmtIterator i;
	int j, skip;
	bool has_kills = false;
	bool partial_range = false;
	bool outer_partial = false;
	pet_tree *tree;
	SourceManager &SM = PP.getSourceManager();
	pet_killed_locals kl(SM);
	unsigned range_start, range_end;

	for (i = stmt_range.first, j = 0; i != stmt_range.second; ++i, ++j)
		;

	tree = pet_tree_new_block(ctx, block, j);

	skip = 0;
	i = stmt_range.first;
	if (skip_declarations)
		for (; i != stmt_range.second; ++i) {
			if ((*i)->getStmtClass() != Stmt::DeclStmtClass)
				break;
			if (options->autodetect)
				kl.add_locals(cast<DeclStmt>(*i));
			++skip;
		}

	for (; i != stmt_range.second; ++i) {
		Stmt *child = *i;
		pet_tree *tree_i;

		tree_i = extract(child);
		if (pet_tree_block_n_child(tree) != 0 && partial) {
			pet_tree_free(tree_i);
			break;
		}
		if (child->getStmtClass() == Stmt::DeclStmtClass) {
			if (options->autodetect)
				kl.add_locals(cast<DeclStmt>(child));
			if (tree_i && block)
				has_kills = true;
		}
		if (options->autodetect) {
			if (tree_i) {
				range_end = getExpansionOffset(SM,
							end_loc(child));
				if (pet_tree_block_n_child(tree) == 0)
					range_start = getExpansionOffset(SM,
							begin_loc(child));
				tree = pet_tree_block_add_child(tree, tree_i);
			} else {
				partial_range = true;
			}
			if (pet_tree_block_n_child(tree) != 0 && !tree_i)
				outer_partial = partial = true;
		} else {
			tree = pet_tree_block_add_child(tree, tree_i);
		}

		if (partial || !tree)
			break;
	}

	if (!tree)
		return NULL;

	if (partial) {
		if (has_kills)
			tree = pet_tree_block_set_block(tree, 0);
		if (outer_partial) {
			kl.remove_accessed_after(parent,
						 range_start, range_end);
			tree = add_kills(tree, kl.locals);
		}
	} else if (partial_range) {
		if (pet_tree_block_n_child(tree) == 0) {
			pet_tree_free(tree);
			return NULL;
		}
		partial = true;
	} else if (skip > 0)
		tree = insert_initial_declarations(tree, skip, stmt_range);

	return tree;
}

extern "C" {
	static __isl_give pet_expr *get_array_size(__isl_keep pet_expr *access,
		void *user);
	static struct pet_array *extract_array(__isl_keep pet_expr *access,
		__isl_keep pet_context *pc, void *user);
}

/* Construct a pet_expr that holds the sizes of the array accessed
 * by "access".
 * This function is used as a callback to pet_context_add_parameters,
 * which is also passed a pointer to the PetScan object.
 */
static __isl_give pet_expr *get_array_size(__isl_keep pet_expr *access,
	void *user)
{
	PetScan *ps = (PetScan *) user;
	isl_id *id;
	pet_expr *size;

	id = pet_expr_access_get_id(access);
	size = ps->get_array_size(id);
	isl_id_free(id);

	return size;
}

/* Construct and return a pet_array corresponding to the variable
 * accessed by "access".
 * This function is used as a callback to pet_scop_from_pet_tree,
 * which is also passed a pointer to the PetScan object.
 */
static struct pet_array *extract_array(__isl_keep pet_expr *access,
	__isl_keep pet_context *pc, void *user)
{
	PetScan *ps = (PetScan *) user;
	isl_id *id;
	pet_array *array;

	id = pet_expr_access_get_id(access);
	array = ps->extract_array(id, NULL, pc);
	isl_id_free(id);

	return array;
}

/* Extract a function summary from the body of "fd".
 *
 * We extract a scop from the function body in a context with as
 * parameters the integer arguments of the function.
 * We turn off autodetection (in case it was set) to ensure that
 * the entire function body is considered.
 * We then collect the accessed array elements and attach them
 * to the corresponding array arguments, taking into account
 * that the function body may access members of array elements.
 * The function body is allowed to have a return statement at the end.
 *
 * The reason for representing the integer arguments as parameters in
 * the context is that if we were to instead start with a context
 * with the function arguments as initial dimensions, then we would not
 * be able to refer to them from the array extents, without turning
 * array extents into maps.
 *
 * The result is stored in the summary_cache cache so that we can reuse
 * it if this method gets called on the same function again later on.
 */
__isl_give pet_function_summary *PetScan::get_summary(FunctionDecl *fd)
{
	isl_space *space;
	isl_set *domain;
	pet_context *pc;
	pet_tree *tree;
	pet_function_summary *summary;
	unsigned n;
	ScopLoc loc;
	int save_autodetect;
	struct pet_scop *scop;
	int int_size;
	isl_union_set *may_read, *may_write, *must_write;
	isl_union_map *to_inner;

	if (summary_cache.find(fd) != summary_cache.end())
		return pet_function_summary_copy(summary_cache[fd]);

	space = isl_space_set_alloc(ctx, 0, 0);

	n = fd->getNumParams();
	summary = pet_function_summary_alloc(ctx, n);
	for (unsigned i = 0; i < n; ++i) {
		ParmVarDecl *parm = fd->getParamDecl(i);
		QualType type = parm->getType();
		isl_id *id;

		if (!type->isIntegerType())
			continue;
		id = pet_id_from_decl(ctx, parm);
		space = isl_space_insert_dims(space, isl_dim_param, 0, 1);
		space = isl_space_set_dim_id(space, isl_dim_param, 0,
						isl_id_copy(id));
		summary = pet_function_summary_set_int(summary, i, id);
	}

	save_autodetect = options->autodetect;
	options->autodetect = 0;
	PetScan body_scan(PP, ast_context, fd, loc, options,
				isl_union_map_copy(value_bounds), independent);

	body_scan.return_root = fd->getBody();
	tree = body_scan.extract(fd->getBody(), false);

	domain = isl_set_universe(space);
	pc = pet_context_alloc(domain);
	pc = pet_context_add_parameters(pc, tree,
						&::get_array_size, &body_scan);
	int_size = size_in_bytes(ast_context, ast_context.IntTy);
	scop = pet_scop_from_pet_tree(tree, int_size,
					&::extract_array, &body_scan, pc);
	scop = scan_arrays(scop, pc);
	may_read = isl_union_map_range(pet_scop_get_may_reads(scop));
	may_write = isl_union_map_range(pet_scop_get_may_writes(scop));
	must_write = isl_union_map_range(pet_scop_get_must_writes(scop));
	to_inner = pet_scop_compute_outer_to_inner(scop);
	pet_scop_free(scop);

	for (unsigned i = 0; i < n; ++i) {
		ParmVarDecl *parm = fd->getParamDecl(i);
		QualType type = parm->getType();
		struct pet_array *array;
		isl_space *space;
		isl_union_set *data_set;
		isl_union_set *may_read_i, *may_write_i, *must_write_i;

		if (pet_clang_array_depth(type) == 0)
			continue;

		array = body_scan.extract_array(parm, NULL, pc);
		space = array ? isl_set_get_space(array->extent) : NULL;
		pet_array_free(array);
		data_set = isl_union_set_from_set(isl_set_universe(space));
		data_set = isl_union_set_apply(data_set,
					isl_union_map_copy(to_inner));
		may_read_i = isl_union_set_intersect(
				isl_union_set_copy(may_read),
				isl_union_set_copy(data_set));
		may_write_i = isl_union_set_intersect(
				isl_union_set_copy(may_write),
				isl_union_set_copy(data_set));
		must_write_i = isl_union_set_intersect(
				isl_union_set_copy(must_write), data_set);
		summary = pet_function_summary_set_array(summary, i,
				may_read_i, may_write_i, must_write_i);
	}

	isl_union_set_free(may_read);
	isl_union_set_free(may_write);
	isl_union_set_free(must_write);
	isl_union_map_free(to_inner);

	options->autodetect = save_autodetect;
	pet_context_free(pc);

	summary_cache[fd] = pet_function_summary_copy(summary);

	return summary;
}

/* If "fd" has a function body, then extract a function summary from
 * this body and attach it to the call expression "expr".
 *
 * Even if a function body is available, "fd" itself may point
 * to a declaration without function body.  We therefore first
 * replace it by the declaration that comes with a body (if any).
 */
__isl_give pet_expr *PetScan::set_summary(__isl_take pet_expr *expr,
	FunctionDecl *fd)
{
	pet_function_summary *summary;

	if (!expr)
		return NULL;
	fd = pet_clang_find_function_decl_with_body(fd);
	if (!fd)
		return expr;

	summary = get_summary(fd);

	expr = pet_expr_call_set_summary(expr, summary);

	return expr;
}

/* Extract a pet_scop from "tree".
 *
 * We simply call pet_scop_from_pet_tree with the appropriate arguments and
 * then add pet_arrays for all accessed arrays.
 * We populate the pet_context with assignments for all parameters used
 * inside "tree" or any of the size expressions for the arrays accessed
 * by "tree" so that they can be used in affine expressions.
 */
struct pet_scop *PetScan::extract_scop(__isl_take pet_tree *tree)
{
	int int_size;
	isl_set *domain;
	pet_context *pc;
	pet_scop *scop;

	int_size = size_in_bytes(ast_context, ast_context.IntTy);

	domain = isl_set_universe(isl_space_set_alloc(ctx, 0, 0));
	pc = pet_context_alloc(domain);
	pc = pet_context_add_parameters(pc, tree, &::get_array_size, this);
	scop = pet_scop_from_pet_tree(tree, int_size,
					&::extract_array, this, pc);
	scop = scan_arrays(scop, pc);
	pet_context_free(pc);

	return scop;
}

/* Add a call to __pencil_kill to the end of "tree" that kills
 * all the variables in "locals" and return the result.
 *
 * No location is added to the kill because the most natural
 * location would lie outside the scop.  Attaching such a location
 * to this tree would extend the scope of the final result
 * to include the location.
 */
__isl_give pet_tree *PetScan::add_kills(__isl_take pet_tree *tree,
	set<ValueDecl *> locals)
{
	int i;
	pet_expr *expr;
	pet_tree *kill, *block;
	set<ValueDecl *>::iterator it;

	if (locals.size() == 0)
		return tree;
	expr = pet_expr_new_call(ctx, "__pencil_kill", locals.size());
	i = 0;
	for (it = locals.begin(); it != locals.end(); ++it) {
		pet_expr *arg;
		arg = extract_access_expr(*it);
		expr = pet_expr_set_arg(expr, i++, arg);
	}
	kill = pet_tree_new_expr(expr);
	block = pet_tree_new_block(ctx, 0, 2);
	block = pet_tree_block_add_child(block, tree);
	block = pet_tree_block_add_child(block, kill);

	return block;
}

/* Check if the scop marked by the user is exactly this Stmt
 * or part of this Stmt.
 * If so, return a pet_scop corresponding to the marked region.
 * Otherwise, return NULL.
 *
 * If the scop is not further nested inside a child of "stmt",
 * then check if there are any variable declarations before the scop
 * inside "stmt".  If so, and if these variables are not used
 * after the scop, then add kills to the variables.
 *
 * If the scop starts in the middle of one of the children, without
 * also ending in that child, then report an error.
 */
struct pet_scop *PetScan::scan(Stmt *stmt)
{
	SourceManager &SM = PP.getSourceManager();
	unsigned start_off, end_off;
	pet_tree *tree;

	start_off = getExpansionOffset(SM, begin_loc(stmt));
	end_off = getExpansionOffset(SM, end_loc(stmt));

	if (start_off > loc.end)
		return NULL;
	if (end_off < loc.start)
		return NULL;

	if (start_off >= loc.start && end_off <= loc.end)
		return extract_scop(extract(stmt));

	pet_killed_locals kl(SM);
	StmtIterator start;
	for (start = stmt->child_begin(); start != stmt->child_end(); ++start) {
		Stmt *child = *start;
		if (!child)
			continue;
		start_off = getExpansionOffset(SM, begin_loc(child));
		end_off = getExpansionOffset(SM, end_loc(child));
		if (start_off < loc.start && end_off >= loc.end)
			return scan(child);
		if (start_off >= loc.start)
			break;
		if (loc.start < end_off) {
			report_unbalanced_pragmas(loc.scop, loc.endscop);
			return NULL;
		}
		if (isa<DeclStmt>(child))
			kl.add_locals(cast<DeclStmt>(child));
	}

	StmtIterator end;
	for (end = start; end != stmt->child_end(); ++end) {
		Stmt *child = *end;
		start_off = SM.getFileOffset(begin_loc(child));
		if (start_off >= loc.end)
			break;
	}

	kl.remove_accessed_after(stmt, loc.start, loc.end);

	tree = extract(StmtRange(start, end), false, false, stmt);
	tree = add_kills(tree, kl.locals);
	return extract_scop(tree);
}

/* Set the size of index "pos" of "array" to "size".
 * In particular, add a constraint of the form
 *
 *	i_pos < size
 *
 * to array->extent and a constraint of the form
 *
 *	size >= 0
 *
 * to array->context.
 *
 * The domain of "size" is assumed to be zero-dimensional.
 */
static struct pet_array *update_size(struct pet_array *array, int pos,
	__isl_take isl_pw_aff *size)
{
	isl_set *valid;
	isl_set *univ;
	isl_set *bound;
	isl_space *dim;
	isl_aff *aff;
	isl_pw_aff *index;
	isl_id *id;

	if (!array)
		goto error;

	valid = isl_set_params(isl_pw_aff_nonneg_set(isl_pw_aff_copy(size)));
	array->context = isl_set_intersect(array->context, valid);

	dim = isl_set_get_space(array->extent);
	aff = isl_aff_zero_on_domain(isl_local_space_from_space(dim));
	aff = isl_aff_add_coefficient_si(aff, isl_dim_in, pos, 1);
	univ = isl_set_universe(isl_aff_get_domain_space(aff));
	index = isl_pw_aff_alloc(univ, aff);

	size = isl_pw_aff_add_dims(size, isl_dim_in,
				isl_set_dim(array->extent, isl_dim_set));
	id = isl_set_get_tuple_id(array->extent);
	size = isl_pw_aff_set_tuple_id(size, isl_dim_in, id);
	bound = isl_pw_aff_lt_set(index, size);

	array->extent = isl_set_intersect(array->extent, bound);

	if (!array->context || !array->extent)
		return pet_array_free(array);

	return array;
error:
	isl_pw_aff_free(size);
	return NULL;
}

#ifdef HAVE_DECAYEDTYPE

/* If "qt" is a decayed type, then set *decayed to true and
 * return the original type.
 */
static QualType undecay(QualType qt, bool *decayed)
{
	const Type *type = qt.getTypePtr();

	*decayed = isa<DecayedType>(type);
	if (*decayed)
		qt = cast<DecayedType>(type)->getOriginalType();
	return qt;
}

#else

/* If "qt" is a decayed type, then set *decayed to true and
 * return the original type.
 * Since this version of clang does not define a DecayedType,
 * we cannot obtain the original type even if it had been decayed and
 * we set *decayed to false.
 */
static QualType undecay(QualType qt, bool *decayed)
{
	*decayed = false;
	return qt;
}

#endif

/* Figure out the size of the array at position "pos" and all
 * subsequent positions from "qt" and update the corresponding
 * argument of "expr" accordingly.
 *
 * The initial type (when pos is zero) may be a pointer type decayed
 * from an array type, if this initial type is the type of a function
 * argument.  This only happens if the original array type has
 * a constant size in the outer dimension as otherwise we get
 * a VariableArrayType.  Try and obtain this original type (if available) and
 * take the outer array size into account if it was marked static.
 */
__isl_give pet_expr *PetScan::set_upper_bounds(__isl_take pet_expr *expr,
	QualType qt, int pos)
{
	const ArrayType *atype;
	pet_expr *size;
	bool decayed = false;

	if (!expr)
		return NULL;

	if (pos == 0)
		qt = undecay(qt, &decayed);

	if (qt->isPointerType()) {
		qt = qt->getPointeeType();
		return set_upper_bounds(expr, qt, pos + 1);
	}
	if (!qt->isArrayType())
		return expr;

	qt = qt->getCanonicalTypeInternal();
	atype = cast<ArrayType>(qt.getTypePtr());

	if (decayed && atype->getSizeModifier() != ArrayType::Static) {
		qt = atype->getElementType();
		return set_upper_bounds(expr, qt, pos + 1);
	}

	if (qt->isConstantArrayType()) {
		const ConstantArrayType *ca = cast<ConstantArrayType>(atype);
		size = extract_expr(ca->getSize());
		expr = pet_expr_set_arg(expr, pos, size);
	} else if (qt->isVariableArrayType()) {
		const VariableArrayType *vla = cast<VariableArrayType>(atype);
		size = extract_expr(vla->getSizeExpr());
		expr = pet_expr_set_arg(expr, pos, size);
	}

	qt = atype->getElementType();

	return set_upper_bounds(expr, qt, pos + 1);
}

/* Construct a pet_expr that holds the sizes of the array represented by "id".
 * The returned expression is a call expression with as arguments
 * the sizes in each dimension.  If we are unable to derive the size
 * in a given dimension, then the corresponding argument is set to infinity.
 * In fact, we initialize all arguments to infinity and then update
 * them if we are able to figure out the size.
 *
 * The result is stored in the id_size cache so that it can be reused
 * if this method is called on the same array identifier later.
 * The result is also stored in the type_size cache in case
 * it gets called on a different array identifier with the same type.
 */
__isl_give pet_expr *PetScan::get_array_size(__isl_keep isl_id *id)
{
	QualType qt = pet_id_get_array_type(id);
	int depth;
	pet_expr *expr, *inf;
	const Type *type = qt.getTypePtr();
	isl_maybe_pet_expr m;

	m = isl_id_to_pet_expr_try_get(id_size, id);
	if (m.valid < 0 || m.valid)
		return m.value;
	if (type_size.find(type) != type_size.end())
		return pet_expr_copy(type_size[type]);

	depth = pet_clang_array_depth(qt);
	inf = pet_expr_new_int(isl_val_infty(ctx));
	expr = pet_expr_new_call(ctx, "bounds", depth);
	for (int i = 0; i < depth; ++i)
		expr = pet_expr_set_arg(expr, i, pet_expr_copy(inf));
	pet_expr_free(inf);

	expr = set_upper_bounds(expr, qt, 0);
	type_size[type] = pet_expr_copy(expr);
	id_size = isl_id_to_pet_expr_set(id_size, isl_id_copy(id),
					pet_expr_copy(expr));

	return expr;
}

/* Set the array size of the array identified by "id" to "size",
 * replacing any previously stored value.
 */
void PetScan::set_array_size(__isl_take isl_id *id, __isl_take pet_expr *size)
{
	id_size = isl_id_to_pet_expr_set(id_size, id, size);
}

/* Does "expr" represent the "integer" infinity?
 */
static int is_infty(__isl_keep pet_expr *expr)
{
	isl_val *v;
	int res;

	if (pet_expr_get_type(expr) != pet_expr_int)
		return 0;
	v = pet_expr_int_get_val(expr);
	res = isl_val_is_infty(v);
	isl_val_free(v);

	return res;
}

/* Figure out the dimensions of an array "array" and
 * update "array" accordingly.
 *
 * We first construct a pet_expr that holds the sizes of the array
 * in each dimension.  The resulting expression may containing
 * infinity values for dimension where we are unable to derive
 * a size expression.
 *
 * The arguments of the size expression that have a value different from
 * infinity are then converted to an affine expression
 * within the context "pc" and incorporated into the size of "array".
 * If we are unable to convert a size expression to an affine expression or
 * if the size is not a (symbolic) constant,
 * then we leave the corresponding size of "array" untouched.
 */
struct pet_array *PetScan::set_upper_bounds(struct pet_array *array,
	__isl_keep pet_context *pc)
{
	int n;
	isl_id *id;
	pet_expr *expr;

	if (!array)
		return NULL;

	id = isl_set_get_tuple_id(array->extent);
	if (!id)
		return pet_array_free(array);
	expr = get_array_size(id);
	isl_id_free(id);

	n = pet_expr_get_n_arg(expr);
	for (int i = 0; i < n; ++i) {
		pet_expr *arg;
		isl_pw_aff *size;

		arg = pet_expr_get_arg(expr, i);
		if (!is_infty(arg)) {
			int dim;

			size = pet_expr_extract_affine(arg, pc);
			dim = isl_pw_aff_dim(size, isl_dim_in);
			if (!size)
				array = pet_array_free(array);
			else if (isl_pw_aff_involves_nan(size) ||
			    isl_pw_aff_involves_dims(size, isl_dim_in, 0, dim))
				isl_pw_aff_free(size);
			else {
				size = isl_pw_aff_drop_dims(size,
							    isl_dim_in, 0, dim);
				array = update_size(array, i, size);
			}
		}
		pet_expr_free(arg);
	}
	pet_expr_free(expr);

	return array;
}

/* Does "decl" have a definition that we can keep track of in a pet_type?
 */
static bool has_printable_definition(RecordDecl *decl)
{
	if (!decl->getDeclName())
		return false;
	return decl->getLexicalDeclContext() == decl->getDeclContext();
}

/* Add all TypedefType objects that appear when dereferencing "type"
 * to "types".
 */
static void insert_intermediate_typedefs(PetTypes *types, QualType type)
{
	type = pet_clang_base_or_typedef_type(type);
	while (isa<TypedefType>(type)) {
		const TypedefType *tt;

		tt = cast<TypedefType>(type);
		types->insert(tt->getDecl());
		type = tt->desugar();
		type = pet_clang_base_or_typedef_type(type);
	}
}

/* Construct and return a pet_array corresponding to the variable
 * represented by "id".
 * In particular, initialize array->extent to
 *
 *	{ name[i_1,...,i_d] : i_1,...,i_d >= 0 }
 *
 * and then call set_upper_bounds to set the upper bounds on the indices
 * based on the type of the variable.  The upper bounds are converted
 * to affine expressions within the context "pc".
 *
 * If the base type is that of a record with a top-level definition or
 * of a typedef and if "types" is not null, then the RecordDecl or
 * TypedefType corresponding to the type, as well as any intermediate
 * TypedefType, is added to "types".
 *
 * If the base type is that of a record with no top-level definition,
 * then we replace it by "<subfield>".
 *
 * If the variable is a scalar, i.e., a zero-dimensional array,
 * then the "const" qualifier, if any, is removed from the base type.
 * This makes it easier for users of pet to turn initializations
 * into assignments.
 */
struct pet_array *PetScan::extract_array(__isl_keep isl_id *id,
	PetTypes *types, __isl_keep pet_context *pc)
{
	struct pet_array *array;
	QualType qt = pet_id_get_array_type(id);
	int depth = pet_clang_array_depth(qt);
	QualType base = pet_clang_base_type(qt);
	string name;
	isl_space *space;

	array = isl_calloc_type(ctx, struct pet_array);
	if (!array)
		return NULL;

	space = isl_space_set_alloc(ctx, 0, depth);
	space = isl_space_set_tuple_id(space, isl_dim_set, isl_id_copy(id));

	array->extent = isl_set_nat_universe(space);

	space = isl_space_params_alloc(ctx, 0);
	array->context = isl_set_universe(space);

	array = set_upper_bounds(array, pc);
	if (!array)
		return NULL;

	if (depth == 0)
		base.removeLocalConst();
	name = base.getAsString();

	if (types) {
		insert_intermediate_typedefs(types, qt);
		if (isa<TypedefType>(base)) {
			types->insert(cast<TypedefType>(base)->getDecl());
		} else if (base->isRecordType()) {
			RecordDecl *decl = pet_clang_record_decl(base);
			TypedefNameDecl *typedecl;
			typedecl = decl->getTypedefNameForAnonDecl();
			if (typedecl)
				types->insert(typedecl);
			else if (has_printable_definition(decl))
				types->insert(decl);
			else
				name = "<subfield>";
		}
	}

	array->element_type = strdup(name.c_str());
	array->element_is_record = base->isRecordType();
	array->element_size = size_in_bytes(ast_context, base);

	return array;
}

/* Construct and return a pet_array corresponding to the variable "decl".
 */
struct pet_array *PetScan::extract_array(ValueDecl *decl,
	PetTypes *types, __isl_keep pet_context *pc)
{
	isl_id *id;
	pet_array *array;

	id = pet_id_from_decl(ctx, decl);
	array = extract_array(id, types, pc);
	isl_id_free(id);

	return array;
}

/* Construct and return a pet_array corresponding to the sequence
 * of declarations represented by "decls".
 * The upper bounds of the array are converted to affine expressions
 * within the context "pc".
 * If the sequence contains a single declaration, then it corresponds
 * to a simple array access.  Otherwise, it corresponds to a member access,
 * with the declaration for the substructure following that of the containing
 * structure in the sequence of declarations.
 * We start with the outermost substructure and then combine it with
 * information from the inner structures.
 *
 * Additionally, keep track of all required types in "types".
 */
struct pet_array *PetScan::extract_array(__isl_keep isl_id_list *decls,
	PetTypes *types, __isl_keep pet_context *pc)
{
	int i, n;
	isl_id *id;
	struct pet_array *array;

	id = isl_id_list_get_id(decls, 0);
	array = extract_array(id, types, pc);
	isl_id_free(id);

	n = isl_id_list_n_id(decls);
	for (i = 1; i < n; ++i) {
		struct pet_array *parent;
		const char *base_name, *field_name;
		char *product_name;

		parent = array;
		id = isl_id_list_get_id(decls, i);
		array = extract_array(id, types, pc);
		isl_id_free(id);
		if (!array)
			return pet_array_free(parent);

		base_name = isl_set_get_tuple_name(parent->extent);
		field_name = isl_set_get_tuple_name(array->extent);
		product_name = pet_array_member_access_name(ctx,
							base_name, field_name);

		array->extent = isl_set_product(isl_set_copy(parent->extent),
						array->extent);
		if (product_name)
			array->extent = isl_set_set_tuple_name(array->extent,
								product_name);
		array->context = isl_set_intersect(array->context,
						isl_set_copy(parent->context));

		pet_array_free(parent);
		free(product_name);

		if (!array->extent || !array->context || !product_name)
			return pet_array_free(array);
	}

	return array;
}

static struct pet_scop *add_type(isl_ctx *ctx, struct pet_scop *scop,
	RecordDecl *decl, Preprocessor &PP, PetTypes &types,
	std::set<TypeDecl *> &types_done);
static struct pet_scop *add_type(isl_ctx *ctx, struct pet_scop *scop,
	TypedefNameDecl *decl, Preprocessor &PP, PetTypes &types,
	std::set<TypeDecl *> &types_done);

/* For each of the fields of "decl" that is itself a record type
 * or a typedef, or an array of such type, add a corresponding pet_type
 * to "scop".
 */
static struct pet_scop *add_field_types(isl_ctx *ctx, struct pet_scop *scop,
	RecordDecl *decl, Preprocessor &PP, PetTypes &types,
	std::set<TypeDecl *> &types_done)
{
	RecordDecl::field_iterator it;

	for (it = decl->field_begin(); it != decl->field_end(); ++it) {
		QualType type = it->getType();

		type = pet_clang_base_or_typedef_type(type);
		if (isa<TypedefType>(type)) {
			TypedefNameDecl *typedefdecl;

			typedefdecl = cast<TypedefType>(type)->getDecl();
			scop = add_type(ctx, scop, typedefdecl,
				PP, types, types_done);
		} else if (type->isRecordType()) {
			RecordDecl *record;

			record = pet_clang_record_decl(type);
			scop = add_type(ctx, scop, record,
				PP, types, types_done);
		}
	}

	return scop;
}

/* Add a pet_type corresponding to "decl" to "scop", provided
 * it is a member of types.records and it has not been added before
 * (i.e., it is not a member of "types_done").
 *
 * Since we want the user to be able to print the types
 * in the order in which they appear in the scop, we need to
 * make sure that types of fields in a structure appear before
 * that structure.  We therefore call ourselves recursively
 * through add_field_types on the types of all record subfields.
 */
static struct pet_scop *add_type(isl_ctx *ctx, struct pet_scop *scop,
	RecordDecl *decl, Preprocessor &PP, PetTypes &types,
	std::set<TypeDecl *> &types_done)
{
	string s;
	llvm::raw_string_ostream S(s);

	if (types.records.find(decl) == types.records.end())
		return scop;
	if (types_done.find(decl) != types_done.end())
		return scop;

	add_field_types(ctx, scop, decl, PP, types, types_done);

	if (strlen(decl->getName().str().c_str()) == 0)
		return scop;

	decl->print(S, PrintingPolicy(PP.getLangOpts()));
	S.str();

	scop->types[scop->n_type] = pet_type_alloc(ctx,
				    decl->getName().str().c_str(), s.c_str());
	if (!scop->types[scop->n_type])
		return pet_scop_free(scop);

	types_done.insert(decl);

	scop->n_type++;

	return scop;
}

/* Add a pet_type corresponding to "decl" to "scop", provided
 * it is a member of types.typedefs and it has not been added before
 * (i.e., it is not a member of "types_done").
 *
 * If the underlying type is a structure, then we print the typedef
 * ourselves since clang does not print the definition of the structure
 * in the typedef.  We also make sure in this case that the types of
 * the fields in the structure are added first.
 * Since the definition of the structure also gets printed this way,
 * add it to types_done such that it will not be printed again,
 * not even without the typedef.
 */
static struct pet_scop *add_type(isl_ctx *ctx, struct pet_scop *scop,
	TypedefNameDecl *decl, Preprocessor &PP, PetTypes &types,
	std::set<TypeDecl *> &types_done)
{
	string s;
	llvm::raw_string_ostream S(s);
	QualType qt = decl->getUnderlyingType();

	if (types.typedefs.find(decl) == types.typedefs.end())
		return scop;
	if (types_done.find(decl) != types_done.end())
		return scop;

	if (qt->isRecordType()) {
		RecordDecl *rec = pet_clang_record_decl(qt);

		add_field_types(ctx, scop, rec, PP, types, types_done);
		S << "typedef ";
		rec->print(S, PrintingPolicy(PP.getLangOpts()));
		S << " ";
		S << decl->getName();
		types_done.insert(rec);
	} else {
		decl->print(S, PrintingPolicy(PP.getLangOpts()));
	}
	S.str();

	scop->types[scop->n_type] = pet_type_alloc(ctx,
				    decl->getName().str().c_str(), s.c_str());
	if (!scop->types[scop->n_type])
		return pet_scop_free(scop);

	types_done.insert(decl);

	scop->n_type++;

	return scop;
}

/* Construct a list of pet_arrays, one for each array (or scalar)
 * accessed inside "scop", add this list to "scop" and return the result.
 * The upper bounds of the arrays are converted to affine expressions
 * within the context "pc".
 *
 * The context of "scop" is updated with the intersection of
 * the contexts of all arrays, i.e., constraints on the parameters
 * that ensure that the arrays have a valid (non-negative) size.
 *
 * If any of the extracted arrays refers to a member access or
 * has a typedef'd type as base type,
 * then also add the required types to "scop".
 * The typedef types are printed first because their definitions
 * may include the definition of a struct and these struct definitions
 * should not be printed separately.  While the typedef definition
 * is being printed, the struct is marked as having been printed as well,
 * such that the later printing of the struct by itself can be prevented.
 *
 * If the sequence of nested array declarations from which the pet_array
 * is extracted appears as the prefix of some other sequence,
 * then the pet_array is marked as "outer".
 * The arrays that already appear in scop->arrays at the start of
 * this function are assumed to be simple arrays, so they are not marked
 * as outer.
 */
struct pet_scop *PetScan::scan_arrays(struct pet_scop *scop,
	__isl_keep pet_context *pc)
{
	int i, n;
	array_desc_set arrays, has_sub;
	array_desc_set::iterator it;
	PetTypes types;
	std::set<TypeDecl *> types_done;
	std::set<clang::RecordDecl *, less_name>::iterator records_it;
	std::set<clang::TypedefNameDecl *, less_name>::iterator typedefs_it;
	int n_array;
	struct pet_array **scop_arrays;

	if (!scop)
		return NULL;

	pet_scop_collect_arrays(scop, arrays);
	if (arrays.size() == 0)
		return scop;

	n_array = scop->n_array;

	scop_arrays = isl_realloc_array(ctx, scop->arrays, struct pet_array *,
					n_array + arrays.size());
	if (!scop_arrays)
		goto error;
	scop->arrays = scop_arrays;

	for (it = arrays.begin(); it != arrays.end(); ++it) {
		isl_id_list *list = isl_id_list_copy(*it);
		int n = isl_id_list_n_id(list);
		list = isl_id_list_drop(list, n - 1, 1);
		has_sub.insert(list);
	}

	for (it = arrays.begin(), i = 0; it != arrays.end(); ++it, ++i) {
		struct pet_array *array;
		array = extract_array(*it, &types, pc);
		scop->arrays[n_array + i] = array;
		if (!scop->arrays[n_array + i])
			goto error;
		if (has_sub.find(*it) != has_sub.end())
			array->outer = 1;
		scop->n_array++;
		scop->context = isl_set_intersect(scop->context,
						isl_set_copy(array->context));
		if (!scop->context)
			goto error;
	}

	n = types.records.size() + types.typedefs.size();
	if (n == 0)
		return scop;

	scop->types = isl_alloc_array(ctx, struct pet_type *, n);
	if (!scop->types)
		goto error;

	for (typedefs_it = types.typedefs.begin();
	     typedefs_it != types.typedefs.end(); ++typedefs_it)
		scop = add_type(ctx, scop, *typedefs_it, PP, types, types_done);

	for (records_it = types.records.begin();
	     records_it != types.records.end(); ++records_it)
		scop = add_type(ctx, scop, *records_it, PP, types, types_done);

	return scop;
error:
	pet_scop_free(scop);
	return NULL;
}

/* Bound all parameters in scop->context to the possible values
 * of the corresponding C variable.
 */
static struct pet_scop *add_parameter_bounds(struct pet_scop *scop)
{
	int n;

	if (!scop)
		return NULL;

	n = isl_set_dim(scop->context, isl_dim_param);
	for (int i = 0; i < n; ++i) {
		isl_id *id;
		ValueDecl *decl;

		id = isl_set_get_dim_id(scop->context, isl_dim_param, i);
		if (pet_nested_in_id(id)) {
			isl_id_free(id);
			isl_die(isl_set_get_ctx(scop->context),
				isl_error_internal,
				"unresolved nested parameter", goto error);
		}
		decl = pet_id_get_decl(id);
		isl_id_free(id);

		scop->context = set_parameter_bounds(scop->context, i, decl);

		if (!scop->context)
			goto error;
	}

	return scop;
error:
	pet_scop_free(scop);
	return NULL;
}

/* Construct a pet_scop from the given function.
 *
 * If the scop was delimited by scop and endscop pragmas, then we override
 * the file offsets by those derived from the pragmas.
 */
struct pet_scop *PetScan::scan(FunctionDecl *fd)
{
	pet_scop *scop;
	Stmt *stmt;

	stmt = fd->getBody();

	if (options->autodetect) {
		set_current_stmt(stmt);
		scop = extract_scop(extract(stmt, true));
	} else {
		current_line = loc.start_line;
		scop = scan(stmt);
		scop = pet_scop_update_start_end(scop, loc.start, loc.end);
	}
	scop = add_parameter_bounds(scop);
	scop = pet_scop_gist(scop, value_bounds);

	return scop;
}

/* Update this->last_line and this->current_line based on the fact
 * that we are about to consider "stmt".
 */
void PetScan::set_current_stmt(Stmt *stmt)
{
	SourceLocation loc = begin_loc(stmt);
	SourceManager &SM = PP.getSourceManager();

	last_line = current_line;
	current_line = SM.getExpansionLineNumber(loc);
}

/* Is the current statement marked by an independent pragma?
 * That is, is there an independent pragma on a line between
 * the line of the current statement and the line of the previous statement.
 * The search is not implemented very efficiently.  We currently
 * assume that there are only a few independent pragmas, if any.
 */
bool PetScan::is_current_stmt_marked_independent()
{
	for (unsigned i = 0; i < independent.size(); ++i) {
		unsigned line = independent[i].line;

		if (last_line < line && line < current_line)
			return true;
	}

	return false;
}
