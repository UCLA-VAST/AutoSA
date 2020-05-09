/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2013-2015 Ecole Normale Superieure. All rights reserved.
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

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

#include <isl/aff.h>
#include <isl/id.h>

#include "clang.h"
#include "expr.h"
#include "expr_plus.h"
#include "id.h"

using namespace clang;

/* Return the depth of the array accessed by the index expression "index".
 * If "index" is an affine expression, i.e., if it does not access
 * any array, then return 1.
 * If "index" represent a member access, i.e., if its range is a wrapped
 * relation, then return the sum of the depth of the array of structures
 * and that of the member inside the structure.
 */
static int extract_depth(__isl_keep isl_multi_pw_aff *index)
{
	isl_id *id;
	QualType qt;

	if (!index)
		return -1;

	if (isl_multi_pw_aff_range_is_wrapping(index)) {
		int domain_depth, range_depth;
		isl_multi_pw_aff *domain, *range;

		domain = isl_multi_pw_aff_copy(index);
		domain = isl_multi_pw_aff_range_factor_domain(domain);
		domain_depth = extract_depth(domain);
		isl_multi_pw_aff_free(domain);
		range = isl_multi_pw_aff_copy(index);
		range = isl_multi_pw_aff_range_factor_range(range);
		range_depth = extract_depth(range);
		isl_multi_pw_aff_free(range);

		return domain_depth + range_depth;
	}

	if (!isl_multi_pw_aff_has_tuple_id(index, isl_dim_out))
		return 1;

	id = isl_multi_pw_aff_get_tuple_id(index, isl_dim_out);
	if (!id)
		return -1;
	qt = pet_id_get_array_type(id);
	isl_id_free(id);

	return pet_clang_array_depth(qt);
}

/* Return the depth of the array accessed by the access expression "expr".
 */
static int extract_depth(__isl_keep pet_expr *expr)
{
	isl_multi_pw_aff *index;
	int depth;

	index = pet_expr_access_get_index(expr);
	depth = extract_depth(index);
	isl_multi_pw_aff_free(index);

	return depth;
}

/* Convert the index expression "index" into an access pet_expr of type "qt".
 */
__isl_give pet_expr *pet_expr_access_from_index(QualType qt,
	__isl_take pet_expr *index, ASTContext &ast_context)
{
	int depth;
	int type_size;

	depth = extract_depth(index);
	type_size = pet_clang_get_type_size(qt, ast_context);

	index = pet_expr_set_type_size(index, type_size);
	index = pet_expr_access_set_depth(index, depth);

	return index;
}

/* Construct an access pet_expr for an access
 * to the variable represented by "id".
 */
__isl_give pet_expr *pet_expr_access_from_id(__isl_take isl_id *id,
	ASTContext &ast_context)
{
	QualType qt;
	pet_expr *expr;

	qt = pet_id_get_array_type(id);
	expr = pet_id_create_index_expr(id);
	return pet_expr_access_from_index(qt, expr, ast_context);
}
