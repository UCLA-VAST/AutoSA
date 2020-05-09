/*
 * Copyright 2011      Leiden University. All rights reserved.
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

#include <string.h>

#include "id.h"

using namespace clang;

/* Create an isl_id that refers to the variable declarator "decl".
 */
__isl_give isl_id *pet_id_from_decl(isl_ctx *ctx, ValueDecl *decl)
{
	return isl_id_alloc(ctx, decl->getName().str().c_str(), decl);
}

/* Create an isl_id that refers to the variable declarator "decl", but
 * has name "name".
 */
__isl_give isl_id *pet_id_from_name_and_decl(isl_ctx *ctx, const char *name,
	ValueDecl *decl)
{
	return isl_id_alloc(ctx, name, decl);
}

/* Create an isl_id with name specified by "name_template" and "n" and
 * associated type "qt".
 */
static __isl_give isl_id *pet_id_from_type(isl_ctx *ctx,
	const char *name_template, int n, QualType qt)
{
	char name[50];
	const Type *type = qt.getTypePtr();

	snprintf(name, sizeof(name), name_template, n);
	return isl_id_alloc(ctx, name, const_cast<Type *>(type));
}

/* Create an isl_id with name "__pet_arg_<n>" and associated type "qt".
 */
__isl_give isl_id *pet_id_arg_from_type(isl_ctx *ctx, int n, QualType qt)
{
	return pet_id_from_type(ctx, "__pet_arg_%d", n, qt);
}

/* Create an isl_id with name "__pet_ret_<n>" and associated type "qt".
 */
__isl_give isl_id *pet_id_ret_from_type(isl_ctx *ctx, int n, QualType qt)
{
	return pet_id_from_type(ctx, "__pet_ret_%d", n, qt);
}

/* Compare the prefix of "s" to "prefix" up to the length of "prefix".
 */
static int prefixcmp(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix));
}

/* Is "id" an identifier created by pet_id_arg_from_type or
 * pet_id_ret_from_type?
 */
static int pet_id_is_arg_or_ret(__isl_keep isl_id *id)
{
	const char *name;

	if (!id)
		return -1;
	name = isl_id_get_name(id);
	if (!name)
		return 0;
	return !prefixcmp(name, "__pet_arg") || !prefixcmp(name, "__pet_ret");
}

/* Extract the ValueDecl that was associated to "id"
 * in pet_id_from_decl.
 *
 * If "id" was create by pet_id_arg_from_type or pet_id_ret_from_type,
 * then there is no ValueDecl associated to it, so return NULL instead.
 */
ValueDecl *pet_id_get_decl(__isl_keep isl_id *id)
{
	if (pet_id_is_arg_or_ret(id))
		return NULL;

	return (ValueDecl *) isl_id_get_user(id);
}

/* Construct a pet_expr representing an index expression for an access
 * to the variable represented by "id".
 */
__isl_give pet_expr *pet_id_create_index_expr(__isl_take isl_id *id)
{
	isl_space *space;

	if (!id)
		return NULL;

	space = isl_space_alloc(isl_id_get_ctx(id), 0, 0, 0);
	space = isl_space_set_tuple_id(space, isl_dim_out, id);

	return pet_expr_from_index(isl_multi_pw_aff_zero(space));
}

/* Is "T" the type of a variable length array with static size?
 */
static bool is_vla_with_static_size(QualType T)
{
	const VariableArrayType *vlatype;

	if (!T->isVariableArrayType())
		return false;
	vlatype = cast<VariableArrayType>(T);
	return vlatype->getSizeModifier() == VariableArrayType::Static;
}

/* Return the type of the variable represented by "id" as an array.
 *
 * In particular, if the declaration associated to "id" is a parameter
 * declaration that is a variable length array with a static size, then
 * return the original type (i.e., the variable length array).
 * Otherwise, return the type of decl.
 *
 * If "id" was created by pet_id_arg_from_type or pet_id_ret_from_type,
 * then it has a type associated to it, so return that type instead.
 */
QualType pet_id_get_array_type(__isl_keep isl_id *id)
{
	ValueDecl *decl;
	ParmVarDecl *parm;
	QualType T;

	if (pet_id_is_arg_or_ret(id)) {
		const Type *type;
		type = (const Type *) isl_id_get_user(id);
		return QualType(type, 0);
	}

	decl = pet_id_get_decl(id);

	parm = dyn_cast<ParmVarDecl>(decl);
	if (!parm)
		return decl->getType();

	T = parm->getOriginalType();
	if (!is_vla_with_static_size(T))
		return decl->getType();
	return T;
}
