/*
 * Copyright 2011 Leiden University. All rights reserved.
 * Copyright 2013 Ecole Normale Superieure. All rights reserved.
 * Copyright 2017 Sven Verdoolaege. All rights reserved.
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

#include <set>
#include <vector>

#include "clang.h"
#include "expr.h"
#include "id.h"
#include "scop_plus.h"
#include "tree.h"

using namespace std;
using namespace clang;

/* Add the sequence of nested arrays of structures of the direct
 * subfields of the record type represented by "ancestors"
 * to "arrays".  The final element in the sequence is guaranteed
 * to refer to a record type.
 *
 * If any of the subfields is anonymous, then add its subfields as well.
 */
static void collect_direct_sub_arrays(ValueDecl *decl,
	__isl_keep isl_id_list *ancestors, array_desc_set &arrays)
{
	isl_ctx *ctx;
	QualType type = decl->getType();
	RecordDecl *record;
	RecordDecl::field_iterator it;

	type = pet_clang_base_type(type);
	record = pet_clang_record_decl(type);

	ctx = isl_id_list_get_ctx(ancestors);
	for (it = record->field_begin(); it != record->field_end(); ++it) {
		FieldDecl *field = *it;
		bool anonymous = field->isAnonymousStructOrUnion();
		isl_id *id;
		isl_id_list *extended;

		if (anonymous) {
			collect_direct_sub_arrays(field, ancestors, arrays);
			continue;
		}
		extended = isl_id_list_copy(ancestors);
		id = pet_id_from_decl(ctx, field);
		extended = isl_id_list_add(extended, id);
		arrays.insert(extended);
	}
}

/* Add the sequence of nested array declarations "list" to "arrays".
 *
 * If "list" represents a member access (i.e., the list has at least
 * two elements), then also add the other members in each of its
 * outer arrays.
 */
static void add_with_siblings(__isl_take isl_id_list *list,
	array_desc_set &arrays)
{
	int n;

	arrays.insert(isl_id_list_copy(list));

	n = isl_id_list_n_id(list);
	while (n > 1) {
		isl_id *id;
		ValueDecl *decl;

		list = isl_id_list_drop(list, --n, 1);
		arrays.insert(isl_id_list_copy(list));
		id = isl_id_list_get_id(list, n - 1);
		decl = pet_id_get_decl(id);
		isl_id_free(id);
		collect_direct_sub_arrays(decl, list, arrays);
	}
	isl_id_list_free(list);
}

/* Construct a sequence of nested array declarations containing
 * a single element corresponding to the tuple identifier
 * of the set space "space".
 *
 * If the array being accessed has a NULL ValueDecl, then it
 * is a virtual scalar.  These do not need to be collected
 * because they are added to the scop of the statement writing
 * to the scalar.  Return an empty list instead.
 */
static __isl_give isl_id_list *extract_list_from_tuple_id(
	__isl_keep isl_space *space)
{
	isl_ctx *ctx;
	isl_id *id;

	id = isl_space_get_tuple_id(space, isl_dim_set);
	if (pet_id_get_decl(id))
		return isl_id_list_from_id(id);
	isl_id_free(id);
	ctx = isl_space_get_ctx(space);
	return isl_id_list_alloc(ctx, 0);
}

/* Construct a sequence of nested array declarations corresponding
 * to the accessed data space "space".
 *
 * If "space" represents an array access, then the extracted sequence
 * contains a single element corresponding to the array declaration.
 * Otherwise, if "space" represents a member access, then the extracted
 * sequence contains an element for the outer array of structures and
 * for each nested array or scalar.
 *
 * If the array being accessed has a NULL ValueDecl, then it
 * is a virtual scalar.  These do not need to be collected
 * because they are added to the scop of the statement writing
 * to the scalar.  Return an empty list instead.
 */
static __isl_give isl_id_list *extract_list(__isl_keep isl_space *space)
{
	isl_bool is_wrapping;
	isl_space *range;
	isl_id_list *list;

	is_wrapping = isl_space_is_wrapping(space);
	if (is_wrapping < 0)
		return NULL;
	if (!is_wrapping)
		return extract_list_from_tuple_id(space);
	space = isl_space_unwrap(isl_space_copy(space));
	range = isl_space_range(isl_space_copy(space));
	list = extract_list(range);
	isl_space_free(range);
	space = isl_space_domain(space);
	list = isl_id_list_concat(extract_list(space), list);
	isl_space_free(space);
	return list;
}

/* Extract one or more sequences of declarations from the accessed
 * data space "space" and add them to "arrays".
 *
 * If "space" represents an array access, then the extracted sequence
 * contains a single element corresponding to the array declaration.
 * Otherwise, if "space" represents a member access, then the extracted
 * sequences contain an element for the outer array of structures and
 * for each nested array or scalar.  If such a sequence is constructed
 * corresponding to a given member access, then such sequences are
 * also constructed for the other members in the same structure.
 *
 * If the array being accessed has a NULL ValueDecl, then it
 * is a virtual scalar.  We don't need to collect such scalars
 * because they are added to the scop of the statement writing
 * to the scalar.  extract_list returns an empty list for
 * such arrays.
 *
 * If the sequence corresponding to "space" already appears
 * in "arrays", then its siblings have already been added as well,
 * so there is nothing left to do.
 */
static isl_stat space_collect_arrays(__isl_take isl_space *space, void *user)
{
	array_desc_set *arrays = (array_desc_set *) user;
	int n;
	isl_id_list *list;

	list = extract_list(space);
	n = isl_id_list_n_id(list);
	if (n > 0 && arrays->find(list) == arrays->end())
		add_with_siblings(list, *arrays);
	else
		isl_id_list_free(list);
	isl_space_free(space);

	return isl_stat_ok;
}

/* Extract one or more sequences of declarations from the access expression
 * "expr" and add them to "arrays".
 */
static void access_collect_arrays(__isl_keep pet_expr *expr,
	array_desc_set &arrays)
{
	if (pet_expr_is_affine(expr))
		return;

	pet_expr_access_foreach_data_space(expr,
					    &space_collect_arrays, &arrays);
}

static void expr_collect_arrays(__isl_keep pet_expr *expr,
	array_desc_set &arrays)
{
	int n;

	if (!expr)
		return;

	n = pet_expr_get_n_arg(expr);
	for (int i = 0; i < n; ++i) {
		pet_expr *arg;

		arg = pet_expr_get_arg(expr, i);
		expr_collect_arrays(arg, arrays);
		pet_expr_free(arg);
	}

	if (pet_expr_get_type(expr) == pet_expr_access)
		access_collect_arrays(expr, arrays);
}

/* Wrapper around access_collect_arrays for use as a callback function
 * to pet_tree_foreach_access_expr.
 */
static int access_collect_wrap(__isl_keep pet_expr *expr, void *user)
{
	array_desc_set *arrays = (array_desc_set *) user;

	access_collect_arrays(expr, *arrays);

	return 0;
}

static void stmt_collect_arrays(struct pet_stmt *stmt,
	array_desc_set &arrays)
{
	if (!stmt)
		return;

	for (unsigned i = 0; i < stmt->n_arg; ++i)
		expr_collect_arrays(stmt->args[i], arrays);

	pet_tree_foreach_access_expr(stmt->body, &access_collect_wrap, &arrays);
}

/* Collect the set of all accessed arrays (or scalars) in "scop",
 * except those that already appear in scop->arrays,
 * and put them in "arrays".
 *
 * Each accessed array is represented by a sequence of nested
 * array declarations, one for the outer array of structures
 * and one for each member access.
 *
 * The arrays that already appear in scop->arrays are assumed
 * to be simple arrays, represented by a sequence of a single element.
 */
void pet_scop_collect_arrays(struct pet_scop *scop,
	array_desc_set &arrays)
{
	if (!scop)
		return;

	for (int i = 0; i < scop->n_stmt; ++i)
		stmt_collect_arrays(scop->stmts[i], arrays);

	for (int i = 0; i < scop->n_array; ++i) {
		ValueDecl *decl;
		isl_id_list *ancestors;

		isl_id *id = isl_set_get_tuple_id(scop->arrays[i]->extent);
		decl = pet_id_get_decl(id);

		if (!decl) {
			isl_id_free(id);
			continue;
		}

		ancestors = isl_id_list_from_id(id);
		arrays.erase(ancestors);
		isl_id_list_free(ancestors);
	}
}
