/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2013 Ecole Normale Superieure. All rights reserved.
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

#include "clang.h"

using namespace clang;

/* Return the element type of the given array type.
 */
QualType pet_clang_base_type(QualType qt)
{
	const Type *type = qt.getTypePtr();

	if (type->isPointerType())
		return pet_clang_base_type(type->getPointeeType());
	if (type->isArrayType()) {
		const ArrayType *atype;
		type = type->getCanonicalTypeInternal().getTypePtr();
		atype = cast<ArrayType>(type);
		return pet_clang_base_type(atype->getElementType());
	}
	return qt;
}

/* Return the first typedef type that "qt" points to
 * or the base type if there is no such typedef type.
 * Do not call getCanonicalTypeInternal as in pet_clang_base_type
 * because that throws away all internal typedef types.
 */
QualType pet_clang_base_or_typedef_type(QualType qt)
{
	const Type *type = qt.getTypePtr();

	if (isa<TypedefType>(type))
		return qt;
	if (type->isPointerType())
		return pet_clang_base_type(type->getPointeeType());
	if (type->isArrayType()) {
		const ArrayType *atype;
		atype = cast<ArrayType>(type);
		return pet_clang_base_type(atype->getElementType());
	}
	return qt;
}

/* Given a record type, return the corresponding RecordDecl.
 */
RecordDecl *pet_clang_record_decl(QualType T)
{
	const Type *type = T->getCanonicalTypeInternal().getTypePtr();
	const RecordType *record;
	record = cast<RecordType>(type);
	return record->getDecl();
}

/* Strip off all outer casts from "expr" that are either implicit or a no-op.
 */
Expr *pet_clang_strip_casts(Expr *expr)
{
	while (isa<CastExpr>(expr)) {
		CastExpr *ce = cast<CastExpr>(expr);
		CastKind kind = ce->getCastKind();
		if (!isa<ImplicitCastExpr>(expr) && kind != CK_NoOp)
			break;
		expr = ce->getSubExpr();
	}

	return expr;
}

/* Return the number of bits needed to represent the type "qt",
 * if it is an integer type.  Otherwise return 0.
 * If qt is signed then return the opposite of the number of bits.
 */
int pet_clang_get_type_size(QualType qt, ASTContext &ast_context)
{
	int size;

	if (!qt->isIntegerType())
		return 0;

	size = ast_context.getIntWidth(qt);
	if (!qt->isUnsignedIntegerType())
		size = -size;

	return size;
}

/* Return the FunctionDecl that refers to the same function
 * that "fd" refers to, but that has a body.
 * Return NULL if no such FunctionDecl is available.
 *
 * It is not clear why hasBody takes a reference to a const FunctionDecl *.
 * It seems that it is possible to directly use the iterators to obtain
 * a non-const pointer.
 * Since we are not going to use the pointer to modify anything anyway,
 * it seems safe to drop the constness.  The alternative would be to
 * modify a lot of other functions to include const qualifiers.
 */
FunctionDecl *pet_clang_find_function_decl_with_body(FunctionDecl *fd)
{
	const FunctionDecl *def;

	if (!fd->hasBody(def))
		return NULL;

	return const_cast<FunctionDecl *>(def);
}

/* Return the depth of an array of the given type.
 */
int pet_clang_array_depth(QualType qt)
{
	const Type *type = qt.getTypePtr();

	if (type->isPointerType())
		return 1 + pet_clang_array_depth(type->getPointeeType());
	if (type->isArrayType()) {
		const ArrayType *atype;
		type = type->getCanonicalTypeInternal().getTypePtr();
		atype = cast<ArrayType>(type);
		return 1 + pet_clang_array_depth(atype->getElementType());
	}
	return 0;
}
