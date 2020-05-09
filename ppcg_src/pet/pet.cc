/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2012-2014 Ecole Normale Superieure. All rights reserved.
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
#undef PACKAGE

#include <stdlib.h>
#include <map>
#include <vector>
#include <iostream>
#ifdef HAVE_ADT_OWNINGPTR_H
#include <llvm/ADT/OwningPtr.h>
#else
#include <memory>
#endif
#ifdef HAVE_LLVM_OPTION_ARG_H
#include <llvm/Option/Arg.h>
#endif
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/Host.h>
#include <clang/Basic/Version.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Tool.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#ifdef HAVE_BASIC_DIAGNOSTICOPTIONS_H
#include <clang/Basic/DiagnosticOptions.h>
#else
#include <clang/Frontend/DiagnosticOptions.h>
#endif
#include <clang/Frontend/TextDiagnosticPrinter.h>
#ifdef HAVE_LEX_HEADERSEARCHOPTIONS_H
#include <clang/Lex/HeaderSearchOptions.h>
#else
#include <clang/Frontend/HeaderSearchOptions.h>
#endif
#ifdef HAVE_CLANG_BASIC_LANGSTANDARD_H
#include <clang/Basic/LangStandard.h>
#else
#include <clang/Frontend/LangStandard.h>
#endif
#ifdef HAVE_LEX_PREPROCESSOROPTIONS_H
#include <clang/Lex/PreprocessorOptions.h>
#else
#include <clang/Frontend/PreprocessorOptions.h>
#endif
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Frontend/Utils.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/Pragma.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/SemaDiagnostic.h>
#include <clang/Parse/Parser.h>
#include <clang/Parse/ParseAST.h>

#include <isl/ctx.h>
#include <isl/constraint.h>

#include <pet.h>

#include "clang_compatibility.h"
#include "id.h"
#include "options.h"
#include "scan.h"
#include "print.h"

#define ARRAY_SIZE(array) (sizeof(array)/sizeof(*array))

using namespace std;
using namespace clang;
using namespace clang::driver;
#ifdef HAVE_LLVM_OPTION_ARG_H
using namespace llvm::opt;
#endif

#ifdef HAVE_ADT_OWNINGPTR_H
#define unique_ptr	llvm::OwningPtr
#endif

/* Called if we found something we didn't expect in one of the pragmas.
 * We'll provide more informative warnings later.
 */
static void unsupported(Preprocessor &PP, SourceLocation loc)
{
	DiagnosticsEngine &diag = PP.getDiagnostics();
	unsigned id = diag.getCustomDiagID(DiagnosticsEngine::Warning,
					   "unsupported");
	DiagnosticBuilder B = diag.Report(loc, id);
}

static int get_int(const char *s)
{
	return s[0] == '"' ? atoi(s + 1) : atoi(s);
}

static ValueDecl *get_value_decl(Sema &sema, Token &token)
{
	IdentifierInfo *name;
	Decl *decl;

	if (token.isNot(tok::identifier))
		return NULL;

	name = token.getIdentifierInfo();
	decl = sema.LookupSingleName(sema.TUScope, name,
				token.getLocation(), Sema::LookupOrdinaryName);
	return decl ? cast_or_null<ValueDecl>(decl) : NULL;
}

/* Handle pragmas of the form
 *
 *	#pragma value_bounds identifier lower_bound upper_bound
 *
 * For each such pragma, add a mapping
 *	{ identifier[] -> [i] : lower_bound <= i <= upper_bound }
 * to value_bounds.
 */
struct PragmaValueBoundsHandler : public PragmaHandler {
	Sema &sema;
	isl_ctx *ctx;
	isl_union_map *value_bounds;

	PragmaValueBoundsHandler(isl_ctx *ctx, Sema &sema) :
	    PragmaHandler("value_bounds"), sema(sema), ctx(ctx) {
		isl_space *space = isl_space_params_alloc(ctx, 0);
		value_bounds = isl_union_map_empty(space);
	}

	~PragmaValueBoundsHandler() {
		isl_union_map_free(value_bounds);
	}

	virtual void HandlePragma(Preprocessor &PP,
				  PragmaIntroducer Introducer,
				  Token &ScopTok) {
		isl_id *id;
		isl_space *dim;
		isl_map *map;
		ValueDecl *vd;
		Token token;
		int lb;
		int ub;

		PP.Lex(token);
		vd = get_value_decl(sema, token);
		if (!vd) {
			unsupported(PP, token.getLocation());
			return;
		}

		PP.Lex(token);
		if (!token.isLiteral()) {
			unsupported(PP, token.getLocation());
			return;
		}

		lb = get_int(token.getLiteralData());

		PP.Lex(token);
		if (!token.isLiteral()) {
			unsupported(PP, token.getLocation());
			return;
		}

		ub = get_int(token.getLiteralData());

		dim = isl_space_alloc(ctx, 0, 0, 1);
		map = isl_map_universe(dim);
		map = isl_map_lower_bound_si(map, isl_dim_out, 0, lb);
		map = isl_map_upper_bound_si(map, isl_dim_out, 0, ub);
		id = isl_id_alloc(ctx, vd->getName().str().c_str(), vd);
		map = isl_map_set_tuple_id(map, isl_dim_in, id);

		value_bounds = isl_union_map_add_map(value_bounds, map);
	}
};

/* Given a variable declaration, check if it has an integer initializer
 * and if so, add a parameter corresponding to the variable to "value"
 * with its value fixed to the integer initializer and return the result.
 */
static __isl_give isl_set *extract_initialization(__isl_take isl_set *value,
	ValueDecl *decl)
{
	VarDecl *vd;
	Expr *expr;
	IntegerLiteral *il;
	isl_val *v;
	isl_ctx *ctx;
	isl_id *id;
	isl_space *space;
	isl_set *set;

	vd = cast<VarDecl>(decl);
	if (!vd)
		return value;
	if (!vd->getType()->isIntegerType())
		return value;
	expr = vd->getInit();
	if (!expr)
		return value;
	il = cast<IntegerLiteral>(expr);
	if (!il)
		return value;

	ctx = isl_set_get_ctx(value);
	id = isl_id_alloc(ctx, vd->getName().str().c_str(), vd);
	space = isl_space_params_alloc(ctx, 1);
	space = isl_space_set_dim_id(space, isl_dim_param, 0, id);
	set = isl_set_universe(space);

	v = PetScan::extract_int(ctx, il);
	set = isl_set_fix_val(set, isl_dim_param, 0, v);

	return isl_set_intersect(value, set);
}

/* Handle pragmas of the form
 *
 *	#pragma parameter identifier lower_bound
 * and
 *	#pragma parameter identifier lower_bound upper_bound
 *
 * For each such pragma, intersect the context with the set
 * [identifier] -> { [] : lower_bound <= identifier <= upper_bound }
 */
struct PragmaParameterHandler : public PragmaHandler {
	Sema &sema;
	isl_set *&context;
	isl_set *&context_value;

	PragmaParameterHandler(Sema &sema, isl_set *&context,
		isl_set *&context_value) :
		PragmaHandler("parameter"), sema(sema), context(context),
		context_value(context_value) {}

	virtual void HandlePragma(Preprocessor &PP,
				  PragmaIntroducer Introducer,
				  Token &ScopTok) {
		isl_id *id;
		isl_ctx *ctx = isl_set_get_ctx(context);
		isl_space *dim;
		isl_set *set;
		ValueDecl *vd;
		Token token;
		int lb;
		int ub;
		bool has_ub = false;

		PP.Lex(token);
		vd = get_value_decl(sema, token);
		if (!vd) {
			unsupported(PP, token.getLocation());
			return;
		}

		PP.Lex(token);
		if (!token.isLiteral()) {
			unsupported(PP, token.getLocation());
			return;
		}

		lb = get_int(token.getLiteralData());

		PP.Lex(token);
		if (token.isLiteral()) {
			has_ub = true;
			ub = get_int(token.getLiteralData());
		} else if (token.isNot(tok::eod)) {
			unsupported(PP, token.getLocation());
			return;
		}

		id = isl_id_alloc(ctx, vd->getName().str().c_str(), vd);
		dim = isl_space_params_alloc(ctx, 1);
		dim = isl_space_set_dim_id(dim, isl_dim_param, 0, id);

		set = isl_set_universe(dim);

		set = isl_set_lower_bound_si(set, isl_dim_param, 0, lb);
		if (has_ub)
			set = isl_set_upper_bound_si(set, isl_dim_param, 0, ub);

		context = isl_set_intersect(context, set);

		context_value = extract_initialization(context_value, vd);
	}
};

/* Handle pragmas of the form
 *
 *	#pragma pencil independent
 *
 * For each such pragma, add an entry to the "independent" vector.
 */
struct PragmaPencilHandler : public PragmaHandler {
	std::vector<Independent> &independent;

	PragmaPencilHandler(std::vector<Independent> &independent) :
		PragmaHandler("pencil"), independent(independent) {}

	virtual void HandlePragma(Preprocessor &PP,
				  PragmaIntroducer Introducer,
				  Token &PencilTok) {
		Token token;
		IdentifierInfo *info;

		PP.Lex(token);
		if (token.isNot(tok::identifier))
			return;

		info = token.getIdentifierInfo();
		if (!info->isStr("independent"))
			return;

		PP.Lex(token);
		if (token.isNot(tok::eod))
			return;

		SourceManager &SM = PP.getSourceManager();
		SourceLocation sloc = PencilTok.getLocation();
		unsigned line = SM.getExpansionLineNumber(sloc);
		independent.push_back(Independent(line));
	}
};

#ifdef HAVE_TRANSLATELINECOL

/* Return a SourceLocation for line "line", column "col" of file "FID".
 */
SourceLocation translateLineCol(SourceManager &SM, FileID FID, unsigned line,
	unsigned col)
{
	return SM.translateLineCol(FID, line, col);
}

#else

/* Return a SourceLocation for line "line", column "col" of file "FID".
 */
SourceLocation translateLineCol(SourceManager &SM, FileID FID, unsigned line,
	unsigned col)
{
	return SM.getLocation(SM.getFileEntryForID(FID), line, col);
}

#endif

/* List of pairs of #pragma scop and #pragma endscop locations.
 */
struct ScopLocList {
	std::vector<ScopLoc> list;

	/* Add a new start (#pragma scop) location to the list.
	 * If the last #pragma scop did not have a matching
	 * #pragma endscop then overwrite it.
	 * "start" points to the location of the scop pragma.
	 */
	void add_start(SourceManager &SM, SourceLocation start) {
		ScopLoc loc;

		loc.scop = start;
		int line = SM.getExpansionLineNumber(start);
		start = translateLineCol(SM, SM.getFileID(start), line, 1);
		loc.start_line = line;
		loc.start = SM.getFileOffset(start);
		if (list.size() == 0 || list[list.size() - 1].end != 0)
			list.push_back(loc);
		else
			list[list.size() - 1] = loc;
	}

	/* Set the end location (#pragma endscop) of the last pair
	 * in the list.
	 * If there is no such pair of if the end of that pair
	 * is already set, then ignore the spurious #pragma endscop.
	 * "end" points to the location of the endscop pragma.
	 */
	void add_end(SourceManager &SM, SourceLocation end) {
		if (list.size() == 0 || list[list.size() - 1].end != 0)
			return;
		list[list.size() - 1].endscop = end;
		int line = SM.getExpansionLineNumber(end);
		end = translateLineCol(SM, SM.getFileID(end), line + 1, 1);
		list[list.size() - 1].end = SM.getFileOffset(end);
	}
};

/* Handle pragmas of the form
 *
 *	#pragma scop
 *
 * In particular, store the location of the line containing
 * the pragma in the list "scops".
 */
struct PragmaScopHandler : public PragmaHandler {
	ScopLocList &scops;

	PragmaScopHandler(ScopLocList &scops) :
		PragmaHandler("scop"), scops(scops) {}

	virtual void HandlePragma(Preprocessor &PP,
				  PragmaIntroducer Introducer,
				  Token &ScopTok) {
		SourceManager &SM = PP.getSourceManager();
		SourceLocation sloc = ScopTok.getLocation();
		scops.add_start(SM, sloc);
	}
};

/* Handle pragmas of the form
 *
 *	#pragma endscop
 *
 * In particular, store the location of the line following the one containing
 * the pragma in the list "scops".
 */
struct PragmaEndScopHandler : public PragmaHandler {
	ScopLocList &scops;

	PragmaEndScopHandler(ScopLocList &scops) :
		PragmaHandler("endscop"), scops(scops) {}

	virtual void HandlePragma(Preprocessor &PP,
				  PragmaIntroducer Introducer,
				  Token &EndScopTok) {
		SourceManager &SM = PP.getSourceManager();
		SourceLocation sloc = EndScopTok.getLocation();
		scops.add_end(SM, sloc);
	}
};

/* Handle pragmas of the form
 *
 *	#pragma live-out identifier, identifier, ...
 *
 * Each identifier on the line is stored in live_out.
 */
struct PragmaLiveOutHandler : public PragmaHandler {
	Sema &sema;
	set<ValueDecl *> &live_out;

	PragmaLiveOutHandler(Sema &sema, set<ValueDecl *> &live_out) :
		PragmaHandler("live"), sema(sema), live_out(live_out) {}

	virtual void HandlePragma(Preprocessor &PP,
				  PragmaIntroducer Introducer,
				  Token &ScopTok) {
		Token token;

		PP.Lex(token);
		if (token.isNot(tok::minus))
			return;
		PP.Lex(token);
		if (token.isNot(tok::identifier) ||
		    !token.getIdentifierInfo()->isStr("out"))
			return;

		PP.Lex(token);
		while (token.isNot(tok::eod)) {
			ValueDecl *vd;

			vd = get_value_decl(sema, token);
			if (!vd) {
				unsupported(PP, token.getLocation());
				return;
			}
			live_out.insert(vd);
			PP.Lex(token);
			if (token.is(tok::comma))
				PP.Lex(token);
		}
	}
};

/* For each array in "scop", set its value_bounds property
 * based on the information in "value_bounds" and
 * mark it as live_out if it appears in "live_out".
 */
static void update_arrays(struct pet_scop *scop,
	__isl_take isl_union_map *value_bounds, set<ValueDecl *> &live_out)
{
	set<ValueDecl *>::iterator lo_it;
	isl_ctx *ctx = isl_union_map_get_ctx(value_bounds);

	if (!scop) {
		isl_union_map_free(value_bounds);
		return;
	}

	for (int i = 0; i < scop->n_array; ++i) {
		isl_id *id;
		isl_space *space;
		isl_map *bounds;
		ValueDecl *decl;
		pet_array *array = scop->arrays[i];

		id = isl_set_get_tuple_id(array->extent);
		decl = pet_id_get_decl(id);

		space = isl_space_alloc(ctx, 0, 0, 1);
		space = isl_space_set_tuple_id(space, isl_dim_in, id);

		bounds = isl_union_map_extract_map(value_bounds, space);
		if (!isl_map_plain_is_empty(bounds))
			array->value_bounds = isl_map_range(bounds);
		else
			isl_map_free(bounds);

		lo_it = live_out.find(decl);
		if (lo_it != live_out.end())
			array->live_out = 1;
	}

	isl_union_map_free(value_bounds);
}

/* Extract a pet_scop (if any) from each appropriate function.
 * Each detected scop is passed to "fn".
 * When autodetecting, at most one scop is extracted from each function.
 * If "function" is not NULL, then we only extract a pet_scop if the
 * name of the function matches.
 * If "autodetect" is false, then we only extract if we have seen
 * scop and endscop pragmas and if these are situated inside the function
 * body.
 */
struct PetASTConsumer : public ASTConsumer {
	Preprocessor &PP;
	ASTContext &ast_context;
	DiagnosticsEngine &diags;
	ScopLocList &scops;
	std::vector<Independent> independent;
	const char *function;
	pet_options *options;
	isl_ctx *ctx;
	isl_set *context;
	isl_set *context_value;
	set<ValueDecl *> live_out;
	PragmaValueBoundsHandler *vb_handler;
	isl_stat (*fn)(struct pet_scop *scop, void *user);
	void *user;
	bool error;

	PetASTConsumer(isl_ctx *ctx, Preprocessor &PP, ASTContext &ast_context,
		DiagnosticsEngine &diags, ScopLocList &scops,
		const char *function, pet_options *options,
		isl_stat (*fn)(struct pet_scop *scop, void *user), void *user) :
		PP(PP), ast_context(ast_context), diags(diags),
		scops(scops), function(function), options(options),
		ctx(ctx),
		vb_handler(NULL), fn(fn), user(user), error(false)
	{
		isl_space *space;
		space = isl_space_params_alloc(ctx, 0);
		context = isl_set_universe(isl_space_copy(space));
		context_value = isl_set_universe(space);
	}

	~PetASTConsumer() {
		isl_set_free(context);
		isl_set_free(context_value);
	}

	void handle_value_bounds(Sema *sema) {
		vb_handler = new PragmaValueBoundsHandler(ctx, *sema);
		PP.AddPragmaHandler(vb_handler);
	}

	/* Add all pragma handlers to this->PP.
	 * The pencil pragmas are only handled if the pencil option is set.
	 */
	void add_pragma_handlers(Sema *sema) {
		PP.AddPragmaHandler(new PragmaParameterHandler(*sema, context,
								context_value));
		if (options->pencil) {
			PragmaHandler *PH;
			PH = new PragmaPencilHandler(independent);
			PP.AddPragmaHandler(PH);
		}
		handle_value_bounds(sema);
	}

	__isl_give isl_union_map *get_value_bounds() {
		return isl_union_map_copy(vb_handler->value_bounds);
	}

	/* Pass "scop" to "fn" after performing some postprocessing.
	 * In particular, add the context and value_bounds constraints
	 * speficied through pragmas, add reference identifiers and
	 * reset user pointers on parameters and tuple ids.
	 *
	 * If "scop" does not contain any statements and autodetect
	 * is turned on, then skip it.
	 */
	void call_fn(pet_scop *scop) {
		if (!scop) {
			error = true;
			return;
		}
		if (diags.hasErrorOccurred()) {
			error = true;
			pet_scop_free(scop);
			return;
		}
		if (options->autodetect && scop->n_stmt == 0) {
			pet_scop_free(scop);
			return;
		}
		scop->context = isl_set_intersect(scop->context,
						isl_set_copy(context));
		scop->context_value = isl_set_intersect(scop->context_value,
						isl_set_copy(context_value));

		update_arrays(scop, get_value_bounds(), live_out);

		scop = pet_scop_add_ref_ids(scop);
		scop = pet_scop_anonymize(scop);

		if (fn(scop, user) < 0)
			error = true;
	}

	/* For each explicitly marked scop (using pragmas),
	 * extract the scop and call "fn" on it if it is inside "fd".
	 */
	void scan_scops(FunctionDecl *fd) {
		unsigned start, end;
		vector<ScopLoc>::iterator it;
		isl_union_map *vb = vb_handler->value_bounds;
		SourceManager &SM = PP.getSourceManager();
		pet_scop *scop;

		if (scops.list.size() == 0)
			return;

		start = SM.getFileOffset(begin_loc(fd));
		end = SM.getFileOffset(end_loc(fd));

		for (it = scops.list.begin(); it != scops.list.end(); ++it) {
			ScopLoc loc = *it;
			if (!loc.end)
				continue;
			if (start > loc.end)
				continue;
			if (end < loc.start)
				continue;
			PetScan ps(PP, ast_context, fd, loc, options,
				    isl_union_map_copy(vb), independent);
			scop = ps.scan(fd);
			call_fn(scop);
		}
	}

	virtual HandleTopLevelDeclReturn HandleTopLevelDecl(DeclGroupRef dg) {
		DeclGroupRef::iterator it;

		if (error)
			return HandleTopLevelDeclContinue;

		for (it = dg.begin(); it != dg.end(); ++it) {
			isl_union_map *vb = vb_handler->value_bounds;
			FunctionDecl *fd = dyn_cast<clang::FunctionDecl>(*it);
			if (!fd)
				continue;
			if (!fd->hasBody())
				continue;
			if (function &&
			    fd->getNameInfo().getAsString() != function)
				continue;
			if (options->autodetect) {
				ScopLoc loc;
				pet_scop *scop;
				PetScan ps(PP, ast_context, fd, loc, options,
					    isl_union_map_copy(vb),
					    independent);
				scop = ps.scan(fd);
				if (!scop)
					continue;
				call_fn(scop);
				continue;
			}
			scan_scops(fd);
		}

		return HandleTopLevelDeclContinue;
	}
};

static const char *ResourceDir =
	CLANG_PREFIX "/lib/clang/" CLANG_VERSION_STRING;

static const char *implicit_functions[] = {
	"min", "max", "intMod", "intCeil", "intFloor", "ceild", "floord"
};
static const char *pencil_implicit_functions[] = {
	"imin", "umin", "imax", "umax", "__pencil_kill"
};

/* Should "ident" be treated as an implicit function?
 * If "pencil" is set, then also allow pencil specific builtins.
 */
static bool is_implicit(const IdentifierInfo *ident, int pencil)
{
	const char *name = ident->getNameStart();
	for (size_t i = 0; i < ARRAY_SIZE(implicit_functions); ++i)
		if (!strcmp(name, implicit_functions[i]))
			return true;
	if (!pencil)
		return false;
	for (size_t i = 0; i < ARRAY_SIZE(pencil_implicit_functions); ++i)
		if (!strcmp(name, pencil_implicit_functions[i]))
			return true;
	return false;
}

/* Ignore implicit function declaration warnings on
 * "min", "max", "ceild" and "floord" as we detect and handle these
 * in PetScan.
 * If "pencil" is set, then also ignore them on pencil specific
 * builtins.
 */
struct MyDiagnosticPrinter : public TextDiagnosticPrinter {
	const DiagnosticOptions *DiagOpts;
	int pencil;
#ifdef HAVE_BASIC_DIAGNOSTICOPTIONS_H
	MyDiagnosticPrinter(DiagnosticOptions *DO, int pencil) :
		TextDiagnosticPrinter(llvm::errs(), DO), pencil(pencil) {}
	virtual DiagnosticConsumer *clone(DiagnosticsEngine &Diags) const {
		return new MyDiagnosticPrinter(&Diags.getDiagnosticOptions(),
						pencil);
	}
#else
	MyDiagnosticPrinter(const DiagnosticOptions &DO, int pencil) :
		DiagOpts(&DO), TextDiagnosticPrinter(llvm::errs(), DO),
		pencil(pencil) {}
	virtual DiagnosticConsumer *clone(DiagnosticsEngine &Diags) const {
		return new MyDiagnosticPrinter(*DiagOpts, pencil);
	}
#endif
	virtual void HandleDiagnostic(DiagnosticsEngine::Level level,
					const DiagnosticInfo &info) {
		if (info.getID() == diag::ext_implicit_function_decl &&
		    info.getNumArgs() >= 1 &&
		    info.getArgKind(0) == DiagnosticsEngine::ak_identifierinfo &&
		    is_implicit(info.getArgIdentifier(0), pencil))
			/* ignore warning */;
		else
			TextDiagnosticPrinter::HandleDiagnostic(level, info);
	}
};

#ifdef USE_ARRAYREF

#ifdef HAVE_CXXISPRODUCTION
static Driver *construct_driver(const char *binary, DiagnosticsEngine &Diags)
{
	return new Driver(binary, llvm::sys::getDefaultTargetTriple(),
			    "", false, false, Diags);
}
#elif defined(HAVE_ISPRODUCTION)
static Driver *construct_driver(const char *binary, DiagnosticsEngine &Diags)
{
	return new Driver(binary, llvm::sys::getDefaultTargetTriple(),
			    "", false, Diags);
}
#elif defined(DRIVER_CTOR_TAKES_DEFAULTIMAGENAME)
static Driver *construct_driver(const char *binary, DiagnosticsEngine &Diags)
{
	return new Driver(binary, llvm::sys::getDefaultTargetTriple(),
			    "", Diags);
}
#else
static Driver *construct_driver(const char *binary, DiagnosticsEngine &Diags)
{
	return new Driver(binary, llvm::sys::getDefaultTargetTriple(), Diags);
}
#endif

namespace clang { namespace driver { class Job; } }

/* Clang changed its API from 3.5 to 3.6 and once more in 3.7.
 * We fix this with a simple overloaded function here.
 */
struct ClangAPI {
	static Job *command(Job *J) { return J; }
	static Job *command(Job &J) { return &J; }
	static Command *command(Command &C) { return &C; }
};

#ifdef CREATE_FROM_ARGS_TAKES_ARRAYREF

/* Call CompilerInvocation::CreateFromArgs with the right arguments.
 * In this case, an ArrayRef<const char *>.
 */
static void create_from_args(CompilerInvocation &invocation,
	const ArgStringList *args, DiagnosticsEngine &Diags)
{
	CompilerInvocation::CreateFromArgs(invocation, *args, Diags);
}

#else

/* Call CompilerInvocation::CreateFromArgs with the right arguments.
 * In this case, two "const char *" pointers.
 */
static void create_from_args(CompilerInvocation &invocation,
	const ArgStringList *args, DiagnosticsEngine &Diags)
{
	CompilerInvocation::CreateFromArgs(invocation, args->data() + 1,
						args->data() + args->size(),
						Diags);
}

#endif

/* Create a CompilerInvocation object that stores the command line
 * arguments constructed by the driver.
 * The arguments are mainly useful for setting up the system include
 * paths on newer clangs and on some platforms.
 */
static CompilerInvocation *construct_invocation(const char *filename,
	DiagnosticsEngine &Diags)
{
	const char *binary = CLANG_PREFIX"/bin/clang";
	const unique_ptr<Driver> driver(construct_driver(binary, Diags));
	std::vector<const char *> Argv;
	Argv.push_back(binary);
	Argv.push_back(filename);
	const unique_ptr<Compilation> compilation(
		driver->BuildCompilation(llvm::ArrayRef<const char *>(Argv)));
	JobList &Jobs = compilation->getJobs();
	if (Jobs.size() < 1)
		return NULL;

	Command *cmd = cast<Command>(ClangAPI::command(*Jobs.begin()));
	if (strcmp(cmd->getCreator().getName(), "clang"))
		return NULL;

	const ArgStringList *args = &cmd->getArguments();

	CompilerInvocation *invocation = new CompilerInvocation;
	create_from_args(*invocation, args, Diags);
	return invocation;
}

#else

static CompilerInvocation *construct_invocation(const char *filename,
	DiagnosticsEngine &Diags)
{
	return NULL;
}

#endif

#ifdef HAVE_BASIC_DIAGNOSTICOPTIONS_H

static MyDiagnosticPrinter *construct_printer(CompilerInstance *Clang,
	int pencil)
{
	return new MyDiagnosticPrinter(new DiagnosticOptions(), pencil);
}

#else

static MyDiagnosticPrinter *construct_printer(CompilerInstance *Clang,
	int pencil)
{
	return new MyDiagnosticPrinter(Clang->getDiagnosticOpts(), pencil);
}

#endif

#ifdef CREATETARGETINFO_TAKES_SHARED_PTR

static TargetInfo *create_target_info(CompilerInstance *Clang,
	DiagnosticsEngine &Diags)
{
	shared_ptr<TargetOptions> TO = Clang->getInvocation().TargetOpts;
	TO->Triple = llvm::sys::getDefaultTargetTriple();
	return TargetInfo::CreateTargetInfo(Diags, TO);
}

#elif defined(CREATETARGETINFO_TAKES_POINTER)

static TargetInfo *create_target_info(CompilerInstance *Clang,
	DiagnosticsEngine &Diags)
{
	TargetOptions &TO = Clang->getTargetOpts();
	TO.Triple = llvm::sys::getDefaultTargetTriple();
	return TargetInfo::CreateTargetInfo(Diags, &TO);
}

#else

static TargetInfo *create_target_info(CompilerInstance *Clang,
	DiagnosticsEngine &Diags)
{
	TargetOptions &TO = Clang->getTargetOpts();
	TO.Triple = llvm::sys::getDefaultTargetTriple();
	return TargetInfo::CreateTargetInfo(Diags, TO);
}

#endif

#ifdef CREATEDIAGNOSTICS_TAKES_ARG

static void create_diagnostics(CompilerInstance *Clang)
{
	Clang->createDiagnostics(0, NULL);
}

#else

static void create_diagnostics(CompilerInstance *Clang)
{
	Clang->createDiagnostics();
}

#endif

#ifdef CREATEPREPROCESSOR_TAKES_TUKIND

static void create_preprocessor(CompilerInstance *Clang)
{
	Clang->createPreprocessor(TU_Complete);
}

#else

static void create_preprocessor(CompilerInstance *Clang)
{
	Clang->createPreprocessor();
}

#endif

#ifdef ADDPATH_TAKES_4_ARGUMENTS

void add_path(HeaderSearchOptions &HSO, string Path)
{
	HSO.AddPath(Path, frontend::Angled, false, false);
}

#else

void add_path(HeaderSearchOptions &HSO, string Path)
{
	HSO.AddPath(Path, frontend::Angled, true, false, false);
}

#endif

#ifdef HAVE_SETMAINFILEID

static void create_main_file_id(SourceManager &SM, const FileEntry *file)
{
	SM.setMainFileID(SM.createFileID(file, SourceLocation(),
					SrcMgr::C_User));
}

#else

static void create_main_file_id(SourceManager &SM, const FileEntry *file)
{
	SM.createMainFileID(file);
}

#endif

#ifdef SETLANGDEFAULTS_TAKES_5_ARGUMENTS

static void set_lang_defaults(CompilerInstance *Clang)
{
	PreprocessorOptions &PO = Clang->getPreprocessorOpts();
	TargetOptions &TO = Clang->getTargetOpts();
	llvm::Triple T(TO.Triple);
	CompilerInvocation::setLangDefaults(Clang->getLangOpts(), IK_C, T, PO,
					    LangStandard::lang_unspecified);
}

#else

static void set_lang_defaults(CompilerInstance *Clang)
{
	CompilerInvocation::setLangDefaults(Clang->getLangOpts(), IK_C,
					    LangStandard::lang_unspecified);
}

#endif

#ifdef SETINVOCATION_TAKES_SHARED_PTR

static void set_invocation(CompilerInstance *Clang,
	CompilerInvocation *invocation)
{
	Clang->setInvocation(std::shared_ptr<CompilerInvocation>(invocation));
}

#else

static void set_invocation(CompilerInstance *Clang,
	CompilerInvocation *invocation)
{
	Clang->setInvocation(invocation);
}

#endif

/* Helper function for ignore_error that only gets enabled if T
 * (which is either const FileEntry * or llvm::ErrorOr<const FileEntry *>)
 * has getError method, i.e., if it is llvm::ErrorOr<const FileEntry *>.
 */
template <class T>
static const FileEntry *ignore_error_helper(const T obj, int,
	int[1][sizeof(obj.getError())])
{
	return *obj;
}

/* Helper function for ignore_error that is always enabled,
 * but that only gets selected if the variant above is not enabled,
 * i.e., if T is const FileEntry *.
 */
template <class T>
static const FileEntry *ignore_error_helper(const T obj, long, void *)
{
	return obj;
}

/* Given either a const FileEntry * or a llvm::ErrorOr<const FileEntry *>,
 * extract out the const FileEntry *.
 */
template <class T>
static const FileEntry *ignore_error(const T obj)
{
	return ignore_error_helper(obj, 0, NULL);
}

/* Return the FileEntry corresponding to the given file name
 * in the given compiler instances, ignoring any error.
 */
static const FileEntry *getFile(CompilerInstance *Clang, std::string Filename)
{
	return ignore_error(Clang->getFileManager().getFile(Filename));
}

/* Add pet specific predefines to the preprocessor.
 * Currently, these are all pencil specific, so they are only
 * added if "pencil" is set.
 *
 * We mimic the way <command line> is handled inside clang.
 */
void add_predefines(Preprocessor &PP, int pencil)
{
	string s;

	if (!pencil)
		return;

	s = PP.getPredefines();
	s += "# 1 \"<pet>\" 1\n"
	     "void __pencil_assume(int assumption);\n"
	     "#define pencil_access(f) annotate(\"pencil_access(\" #f \")\")\n"
	     "# 1 \"<built-in>\" 2\n";
	PP.setPredefines(s);
}

/* Extract a pet_scop from each function in the C source file called "filename".
 * Each detected scop is passed to "fn".
 * If "function" is not NULL, only extract a pet_scop from the function
 * with that name.
 * If "autodetect" is set, extract any pet_scop we can find.
 * Otherwise, extract the pet_scop from the region delimited
 * by "scop" and "endscop" pragmas.
 *
 * We first set up the clang parser and then try to extract the
 * pet_scop from the appropriate function(s) in PetASTConsumer.
 */
static isl_stat foreach_scop_in_C_source(isl_ctx *ctx,
	const char *filename, const char *function, pet_options *options,
	isl_stat (*fn)(struct pet_scop *scop, void *user), void *user)
{
	CompilerInstance *Clang = new CompilerInstance();
	create_diagnostics(Clang);
	DiagnosticsEngine &Diags = Clang->getDiagnostics();
	Diags.setSuppressSystemWarnings(true);
	TargetInfo *target = create_target_info(Clang, Diags);
	Clang->setTarget(target);
	set_lang_defaults(Clang);
	CompilerInvocation *invocation = construct_invocation(filename, Diags);
	if (invocation)
		set_invocation(Clang, invocation);
	Diags.setClient(construct_printer(Clang, options->pencil));
	Clang->createFileManager();
	Clang->createSourceManager(Clang->getFileManager());
	HeaderSearchOptions &HSO = Clang->getHeaderSearchOpts();
	HSO.ResourceDir = ResourceDir;
	for (int i = 0; i < options->n_path; ++i)
		add_path(HSO, options->paths[i]);
	PreprocessorOptions &PO = Clang->getPreprocessorOpts();
	for (int i = 0; i < options->n_define; ++i)
		PO.addMacroDef(options->defines[i]);
	create_preprocessor(Clang);
	Preprocessor &PP = Clang->getPreprocessor();
	add_predefines(PP, options->pencil);
	PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
		PP.getLangOpts());

	ScopLocList scops;

	const FileEntry *file = getFile(Clang, filename);
	if (!file)
		isl_die(ctx, isl_error_unknown, "unable to open file",
			do { delete Clang; return isl_stat_error; } while (0));
	create_main_file_id(Clang->getSourceManager(), file);

	Clang->createASTContext();
	PetASTConsumer consumer(ctx, PP, Clang->getASTContext(), Diags,
				scops, function, options, fn, user);
	Sema *sema = new Sema(PP, Clang->getASTContext(), consumer);

	if (!options->autodetect) {
		PP.AddPragmaHandler(new PragmaScopHandler(scops));
		PP.AddPragmaHandler(new PragmaEndScopHandler(scops));
		PP.AddPragmaHandler(new PragmaLiveOutHandler(*sema,
							consumer.live_out));
	}

	consumer.add_pragma_handlers(sema);

	Diags.getClient()->BeginSourceFile(Clang->getLangOpts(), &PP);
	ParseAST(*sema);
	Diags.getClient()->EndSourceFile();

	delete sema;
	delete Clang;

	return consumer.error ? isl_stat_error : isl_stat_ok;
}

/* Extract a pet_scop from each function in the C source file called "filename".
 * Each detected scop is passed to "fn".
 *
 * This wrapper around foreach_scop_in_C_source is mainly used to ensure
 * that all objects on the stack (of that function) are destroyed before we
 * call llvm_shutdown.
 */
static isl_stat pet_foreach_scop_in_C_source(isl_ctx *ctx,
	const char *filename, const char *function,
	isl_stat (*fn)(struct pet_scop *scop, void *user), void *user)
{
	isl_stat r;
	pet_options *options;
	bool allocated = false;

	options = isl_ctx_peek_pet_options(ctx);
	if (!options) {
		options = pet_options_new_with_defaults();
		allocated = true;
	}

	r = foreach_scop_in_C_source(ctx, filename, function, options,
					fn, user);
	llvm::llvm_shutdown();

	if (allocated)
		pet_options_free(options);

	return r;
}

/* Store "scop" into the address pointed to by "user".
 * Return -1 to indicate that we are not interested in any further scops.
 * This function should therefore not be called a second call
 * so in principle there is no need to check if we have already set *user.
 */
static isl_stat set_first_scop(pet_scop *scop, void *user)
{
	pet_scop **p = (pet_scop **) user;

	if (!*p)
		*p = scop;
	else
		pet_scop_free(scop);

	return isl_stat_error;
}

/* Extract a pet_scop from the C source file called "filename".
 * If "function" is not NULL, extract the pet_scop from the function
 * with that name.
 *
 * We start extracting scops from every function and then abort
 * as soon as we have extracted one scop.
 */
struct pet_scop *pet_scop_extract_from_C_source(isl_ctx *ctx,
	const char *filename, const char *function)
{
	pet_scop *scop = NULL;

	pet_foreach_scop_in_C_source(ctx, filename, function,
					&set_first_scop, &scop);

	return scop;
}

/* Internal data structure for pet_transform_C_source
 *
 * transform is the function that should be called to print a scop
 * in is the input source file
 * out is the output source file
 * end is the offset of the end of the previous scop (zero if we have not
 *	found any scop yet)
 * p is a printer that prints to out.
 */
struct pet_transform_data {
	__isl_give isl_printer *(*transform)(__isl_take isl_printer *p,
		struct pet_scop *scop, void *user);
	void *user;

	FILE *in;
	FILE *out;
	unsigned end;
	isl_printer *p;
};

/* This function is called each time a scop is detected.
 *
 * We first copy the input text code from the end of the previous scop
 * until the start of "scop" and then print the scop itself through
 * a call to data->transform.  We set up the printer to print
 * the transformed code with the same (initial) indentation as
 * the original code.
 * Finally, we keep track of the end of "scop" so that we can
 * continue copying when we find the next scop.
 *
 * Before calling data->transform, we store a pointer to the original
 * input file in the extended scop in case the user wants to call
 * pet_scop_print_original from the callback.
 */
static isl_stat pet_transform(struct pet_scop *scop, void *user)
{
	struct pet_transform_data *data = (struct pet_transform_data *) user;
	unsigned start;

	if (!scop)
		return isl_stat_error;
	start = pet_loc_get_start(scop->loc);
	if (copy(data->in, data->out, data->end, start) < 0)
		goto error;
	data->end = pet_loc_get_end(scop->loc);
	scop = pet_scop_set_input_file(scop, data->in);
	data->p = isl_printer_set_indent_prefix(data->p,
					pet_loc_get_indent(scop->loc));
	data->p = data->transform(data->p, scop, data->user);
	if (!data->p)
		return isl_stat_error;
	return isl_stat_ok;
error:
	pet_scop_free(scop);
	return isl_stat_error;
}

/* Transform the C source file "input" by rewriting each scop
 * through a call to "transform".
 * When autodetecting scops, at most one scop per function is rewritten.
 * The transformed C code is written to "output".
 *
 * For each scop we find, we first copy the input text code
 * from the end of the previous scop (or the beginning of the file
 * in case of the first scop) until the start of the scop
 * and then print the scop itself through a call to "transform".
 * At the end we copy everything from the end of the final scop
 * until the end of the input file to "output".
 */
int pet_transform_C_source(isl_ctx *ctx, const char *input, FILE *out,
	__isl_give isl_printer *(*transform)(__isl_take isl_printer *p,
		struct pet_scop *scop, void *user), void *user)
{
	struct pet_transform_data data;
	int r;

	data.in = stdin;
	data.out = out;
	if (input && strcmp(input, "-")) {
		data.in = fopen(input, "r");
		if (!data.in)
			isl_die(ctx, isl_error_unknown, "unable to open file",
				return -1);
	}

	data.p = isl_printer_to_file(ctx, data.out);
	data.p = isl_printer_set_output_format(data.p, ISL_FORMAT_C);

	data.transform = transform;
	data.user = user;
	data.end = 0;
	r = pet_foreach_scop_in_C_source(ctx, input, NULL,
					&pet_transform, &data);

	isl_printer_free(data.p);
	if (!data.p)
		r = -1;
	if (r == 0 && copy(data.in, data.out, data.end, -1) < 0)
		r = -1;

	if (data.in != stdin)
		fclose(data.in);

	return r;
}
