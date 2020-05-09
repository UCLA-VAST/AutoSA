#ifndef PET_EXPR_PLUS_H
#define PET_EXPR_PLUS_H

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>

#include <isl/id.h>

#include <pet.h>

__isl_give pet_expr *pet_expr_access_from_index(clang::QualType qt,
	__isl_take pet_expr *index, clang::ASTContext &ast_context);
__isl_give pet_expr *pet_expr_access_from_id(__isl_take isl_id *id,
	clang::ASTContext &ast_context);

#endif
