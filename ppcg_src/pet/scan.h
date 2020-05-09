#ifndef PET_SCAN_H
#define PET_SCAN_H

#include <set>
#include <map>

#include <clang/Basic/SourceManager.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <clang/Lex/Preprocessor.h>

#include <isl/ctx.h>
#include <isl/map.h>
#include <isl/val.h>

#include "context.h"
#include "inliner.h"
#include "isl_id_to_pet_expr.h"
#include "loc.h"
#include "scop.h"
#include "summary.h"
#include "tree.h"

#include "config.h"

namespace clang {

#ifndef HAVE_STMTRANGE
/* StmtRange was replaced by iterator_range in more recent versions of clang.
 * Implement a StmtRange in terms of this iterator_range if StmtRange
 * is not available.
 */
struct StmtRange : std::pair<StmtIterator,StmtIterator> {
	StmtRange(const StmtIterator &begin, const StmtIterator &end) :
		std::pair<StmtIterator,StmtIterator>(begin, end) {}
	StmtRange(Stmt::child_range range) :
		std::pair<StmtIterator,StmtIterator>(range.begin(),
							range.end()) {}
};
#endif

}

/* The location of the scop, as delimited by scop and endscop
 * pragmas by the user.
 * "scop" and "endscop" are the source locations of the scop and
 * endscop pragmas.
 * "start_line" is the line number of the start position.
 */
struct ScopLoc {
	ScopLoc() : end(0) {}

	clang::SourceLocation scop;
	clang::SourceLocation endscop;
	unsigned start_line;
	unsigned start;
	unsigned end;
};

/* The information extracted from a pragma pencil independent.
 * We currently only keep track of the line number where
 * the pragma appears.
 */
struct Independent {
	Independent(unsigned line) : line(line) {}

	unsigned line;
};

/* Compare two TypeDecl pointers based on their names.
 */
struct less_name {
	bool operator()(const clang::TypeDecl *x,
			const clang::TypeDecl *y) {
		return x->getNameAsString().compare(y->getNameAsString()) < 0;
	}
};

/* The PetTypes structure collects a set of RecordDecl and
 * TypedefNameDecl pointers.
 * The pointers are sorted using a fixed order.  The actual order
 * is not important, only that it is consistent across platforms.
 */
struct PetTypes {
	std::set<clang::RecordDecl *, less_name> records;
	std::set<clang::TypedefNameDecl *, less_name> typedefs;

	void insert(clang::RecordDecl *decl) {
		records.insert(decl);
	}
	void insert(clang::TypedefNameDecl *decl) {
		typedefs.insert(decl);
	}
};

struct PetScan {
	clang::Preprocessor &PP;
	clang::ASTContext &ast_context;
	/* The DeclContext of the function containing the scop.
	 */
	clang::DeclContext *decl_context;
	/* If autodetect is false, then loc contains the location
	 * of the scop to be extracted.
	 */
	ScopLoc &loc;
	isl_ctx *ctx;
	pet_options *options;
	/* If not NULL, then return_root represents the compound statement
	 * in which a return statement is allowed as the final child.
	 * If return_root is NULL, then no return statements are allowed.
	 */
	clang::Stmt *return_root;
	/* Set if the pet_scop returned by an extract method only
	 * represents part of the input tree.
	 */
	bool partial;

	/* A cache of size expressions for array identifiers as computed
	 * by PetScan::get_array_size, or set by PetScan::set_array_size.
	 */
	isl_id_to_pet_expr *id_size;
	/* A cache of size expressions for array types as computed
	 * by PetScan::get_array_size.
	 */
	std::map<const clang::Type *, pet_expr *> type_size;

	/* A cache of funtion summaries for function declarations
	 * as extracted by PetScan::get_summary.
	 */
	std::map<clang::FunctionDecl *, pet_function_summary *> summary_cache;

	/* A union of mappings of the form
	 *	{ identifier[] -> [i] : lower_bound <= i <= upper_bound }
	 */
	isl_union_map *value_bounds;

	/* The line number of the previously considered Stmt. */
	unsigned last_line;
	/* The line number of the Stmt currently being considered. */
	unsigned current_line;
	/* Information about the independent pragmas in the source code. */
	std::vector<Independent> &independent;

	/* All variables that have already been declared
	 * in the current compound statement.
	 */
	std::vector<clang::VarDecl *> declarations;
	/* Sequence number of the next rename. */
	int n_rename;
	/* Have the declared names been collected? */
	bool declared_names_collected;
	/* The names of the variables declared in decl_context,
	 * if declared_names_collected is set.
	 */
	std::set<std::string> declared_names;
	/* A set of names known to be in use. */
	std::set<std::string> used_names;

	/* If not NULL, then "call2id" maps inlined call expressions
	 * that return a value to the corresponding variables.
	 */
	std::map<clang::Stmt *, isl_id *> *call2id;

	/* Sequence number of the next temporary inlined argument variable. */
	int n_arg;
	/* Sequence number of the next temporary inlined return variable. */
	int n_ret;

	PetScan(clang::Preprocessor &PP, clang::ASTContext &ast_context,
		clang::DeclContext *decl_context, ScopLoc &loc,
		pet_options *options, __isl_take isl_union_map *value_bounds,
		std::vector<Independent> &independent) :
		PP(PP),
		ast_context(ast_context), decl_context(decl_context), loc(loc),
		ctx(isl_union_map_get_ctx(value_bounds)),
		options(options), return_root(NULL), partial(false),
		value_bounds(value_bounds), last_line(0), current_line(0),
		independent(independent), n_rename(0),
		declared_names_collected(false), call2id(NULL),
		n_arg(0), n_ret(0) {
		id_size = isl_id_to_pet_expr_alloc(ctx, 0);
	}

	~PetScan();

	struct pet_scop *scan(clang::FunctionDecl *fd);

	static __isl_give isl_val *extract_int(isl_ctx *ctx,
		clang::IntegerLiteral *expr);
	__isl_give pet_expr *get_array_size(__isl_keep isl_id *id);
	void set_array_size(__isl_take isl_id *id, __isl_take pet_expr *size);
	struct pet_array *extract_array(__isl_keep isl_id *id,
		PetTypes *types, __isl_keep pet_context *pc);
	__isl_give pet_tree *extract_inlined_call(clang::CallExpr *call,
		clang::FunctionDecl *fd, __isl_keep isl_id *return_id);
private:
	void set_current_stmt(clang::Stmt *stmt);
	bool is_current_stmt_marked_independent();

	void collect_declared_names();
	void add_new_used_names(const std::set<std::string> &used_names);
	bool name_in_use(const std::string &name, clang::Decl *decl);
	std::string generate_new_name(const std::string &name);

	__isl_give pet_tree *add_kills(__isl_take pet_tree *tree,
		std::set<clang::ValueDecl *> locals);

	struct pet_scop *scan(clang::Stmt *stmt);

	struct pet_scop *scan_arrays(struct pet_scop *scop,
		__isl_keep pet_context *pc);
	struct pet_array *extract_array(clang::ValueDecl *decl,
		PetTypes *types, __isl_keep pet_context *pc);
	struct pet_array *extract_array(__isl_keep isl_id_list *decls,
		PetTypes *types, __isl_keep pet_context *pc);
	__isl_give pet_expr *set_upper_bounds(__isl_take pet_expr *expr,
		clang::QualType qt, int pos);
	struct pet_array *set_upper_bounds(struct pet_array *array,
		__isl_keep pet_context *pc);
	int substitute_array_sizes(__isl_keep pet_tree *tree,
		pet_substituter *substituter);

	__isl_give pet_tree *insert_initial_declarations(
		__isl_take pet_tree *tree, int n_decl,
		clang::StmtRange stmt_range);
	__isl_give pet_tree *extract(clang::Stmt *stmt,
		bool skip_declarations = false);
	__isl_give pet_tree *extract(clang::StmtRange stmt_range, bool block,
		bool skip_declarations, clang::Stmt *parent);
	__isl_give pet_tree *extract(clang::IfStmt *stmt);
	__isl_give pet_tree *extract(clang::WhileStmt *stmt);
	__isl_give pet_tree *extract(clang::CompoundStmt *stmt,
		bool skip_declarations = false);
	__isl_give pet_tree *extract(clang::LabelStmt *stmt);
	__isl_give pet_tree *extract(clang::Decl *decl);
	__isl_give pet_tree *extract(clang::DeclStmt *expr);
	__isl_give pet_tree *extract(clang::ReturnStmt *stmt);

	__isl_give pet_loc *construct_pet_loc(clang::SourceRange range,
		bool skip_semi);
	__isl_give pet_tree *extract(__isl_take pet_expr *expr,
		clang::SourceRange range, bool skip_semi);
	__isl_give pet_tree *update_loc(__isl_take pet_tree *tree,
		clang::Stmt *stmt);

	struct pet_scop *extract_scop(__isl_take pet_tree *tree);

	clang::BinaryOperator *initialization_assignment(clang::Stmt *init);
	clang::Decl *initialization_declaration(clang::Stmt *init);
	clang::ValueDecl *extract_induction_variable(clang::BinaryOperator *stmt);
	clang::VarDecl *extract_induction_variable(clang::Stmt *init,
				clang::Decl *stmt);
	__isl_give pet_expr *extract_unary_increment(clang::UnaryOperator *op,
				clang::ValueDecl *iv);
	__isl_give pet_expr *extract_binary_increment(
				clang::BinaryOperator *op,
				clang::ValueDecl *iv);
	__isl_give pet_expr *extract_compound_increment(
				clang::CompoundAssignOperator *op,
				clang::ValueDecl *iv);
	__isl_give pet_expr *extract_increment(clang::ForStmt *stmt,
				clang::ValueDecl *iv);
	__isl_give pet_tree *extract_for(clang::ForStmt *stmt);
	__isl_give pet_tree *extract_expr_stmt(clang::Stmt *stmt);
	int set_inliner_arguments(pet_inliner &inliner, clang::CallExpr *call,
		clang::FunctionDecl *fd);

	__isl_give pet_expr *extract_assume(clang::Expr *expr);
	__isl_give pet_function_summary *get_summary(clang::FunctionDecl *fd);
	__isl_give pet_expr *set_summary(__isl_take pet_expr *expr,
		clang::FunctionDecl *fd);
	__isl_give pet_expr *extract_argument(clang::FunctionDecl *fd, int pos,
		clang::Expr *expr, bool detect_writes);
	__isl_give pet_expr *extract_expr(const llvm::APInt &val);
	__isl_give pet_expr *extract_expr(clang::Expr *expr);
	__isl_give pet_expr *extract_expr(clang::UnaryOperator *expr);
	__isl_give pet_expr *extract_expr(clang::BinaryOperator *expr);
	__isl_give pet_expr *extract_expr(clang::ImplicitCastExpr *expr);
	__isl_give pet_expr *extract_expr(clang::IntegerLiteral *expr);
	__isl_give pet_expr *extract_expr(clang::EnumConstantDecl *expr);
	__isl_give pet_expr *extract_expr(clang::FloatingLiteral *expr);
	__isl_give pet_expr *extract_expr(clang::ParenExpr *expr);
	__isl_give pet_expr *extract_expr(clang::ConditionalOperator *expr);
	__isl_give pet_expr *extract_expr(clang::CallExpr *expr);
	__isl_give pet_expr *extract_expr(clang::CStyleCastExpr *expr);

	__isl_give pet_expr *extract_access_expr(clang::Expr *expr);
	__isl_give pet_expr *extract_access_expr(clang::ValueDecl *decl);

	__isl_give pet_expr *extract_index_expr(
		clang::ArraySubscriptExpr *expr);
	__isl_give pet_expr *extract_index_expr(clang::Expr *expr);
	__isl_give pet_expr *extract_index_expr(clang::ImplicitCastExpr *expr);
	__isl_give pet_expr *extract_index_expr(clang::DeclRefExpr *expr);
	__isl_give pet_expr *extract_index_expr(clang::ValueDecl *decl);
	__isl_give pet_expr *extract_index_expr(clang::MemberExpr *expr);

	__isl_give isl_val *extract_int(clang::Expr *expr);
	__isl_give isl_val *extract_int(clang::ParenExpr *expr);

	clang::FunctionDecl *find_decl_from_name(clang::CallExpr *call,
		std::string name);
	clang::FunctionDecl *get_summary_function(clang::CallExpr *call);

	void report(clang::SourceRange range, unsigned id);
	void report(clang::Stmt *stmt, unsigned id);
	void report(clang::Decl *decl, unsigned id);
	void unsupported(clang::Stmt *stmt);
	void report_unsupported_unary_operator(clang::Stmt *stmt);
	void report_unsupported_binary_operator(clang::Stmt *stmt);
	void report_unsupported_statement_type(clang::Stmt *stmt);
	void report_prototype_required(clang::Stmt *stmt);
	void report_missing_increment(clang::Stmt *stmt);
	void report_missing_summary_function(clang::Stmt *stmt);
	void report_missing_summary_function_body(clang::Stmt *stmt);
	void report_unsupported_inline_function_argument(clang::Stmt *stmt);
	void report_unsupported_declaration(clang::Decl *decl);
	void report_unbalanced_pragmas(clang::SourceLocation scop,
		clang::SourceLocation endscop);
	void report_unsupported_return(clang::Stmt *stmt);
	void report_return_not_at_end_of_function(clang::Stmt *stmt);
};

#endif
