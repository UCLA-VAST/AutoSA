#ifndef PET_CLANG_H
#define PET_CLANG_H

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Type.h>

clang::QualType pet_clang_base_type(clang::QualType qt);
int pet_clang_array_depth(const clang::QualType qt);
clang::QualType pet_clang_base_or_typedef_type(clang::QualType qt);
clang::RecordDecl *pet_clang_record_decl(clang::QualType T);
clang::Expr *pet_clang_strip_casts(clang::Expr *expr);
int pet_clang_get_type_size(clang::QualType qt, clang::ASTContext &ast_context);
clang::FunctionDecl *pet_clang_find_function_decl_with_body(
	clang::FunctionDecl *fd);

#endif
