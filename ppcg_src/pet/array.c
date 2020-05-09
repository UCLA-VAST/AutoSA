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

#include <string.h>

#include "array.h"

/* Given a partial index expression "base" and an extra index "index",
 * append the extra index to "base" and return the result.
 * Additionally, add the constraints that the extra index is non-negative.
 * If "index" represent a member access, i.e., if its range is a wrapped
 * relation, then we recursively extend the range of this nested relation.
 *
 * The inputs "base" and "index", as well as the result, all have
 * an anonymous zero-dimensional domain.
 */
__isl_give isl_multi_pw_aff *pet_array_subscript(
	__isl_take isl_multi_pw_aff *base, __isl_take isl_pw_aff *index)
{
	isl_id *id;
	isl_set *domain;
	isl_multi_pw_aff *access;
	int member_access;

	member_access = isl_multi_pw_aff_range_is_wrapping(base);
	if (member_access < 0)
		goto error;
	if (member_access) {
		isl_multi_pw_aff *domain, *range;
		isl_id *id;

		id = isl_multi_pw_aff_get_tuple_id(base, isl_dim_out);
		domain = isl_multi_pw_aff_copy(base);
		domain = isl_multi_pw_aff_range_factor_domain(domain);
		range = isl_multi_pw_aff_range_factor_range(base);
		range = pet_array_subscript(range, index);
		access = isl_multi_pw_aff_range_product(domain, range);
		access = isl_multi_pw_aff_set_tuple_id(access, isl_dim_out, id);
		return access;
	}

	id = isl_multi_pw_aff_get_tuple_id(base, isl_dim_set);
	domain = isl_pw_aff_nonneg_set(isl_pw_aff_copy(index));
	index = isl_pw_aff_intersect_domain(index, domain);
	access = isl_multi_pw_aff_from_pw_aff(index);
	access = isl_multi_pw_aff_flat_range_product(base, access);
	access = isl_multi_pw_aff_set_tuple_id(access, isl_dim_set, id);

	return access;
error:
	isl_multi_pw_aff_free(base);
	isl_pw_aff_free(index);
	return NULL;
}

/* Construct a name for a member access by concatenating the name
 * of the array of structures and the member, separated by an underscore.
 *
 * The caller is responsible for freeing the result.
 */
char *pet_array_member_access_name(isl_ctx *ctx, const char *base,
	const char *field)
{
	int len;
	char *name;

	len = strlen(base) + 1 + strlen(field);
	name = isl_alloc_array(ctx, char, len + 1);
	if (!name)
		return NULL;
	snprintf(name, len + 1, "%s_%s", base, field);

	return name;
}

/* Given an index expression "base" for an element of an array of structures
 * and an expression "field" for the field member being accessed, construct
 * an index expression for an access to that member of the given structure.
 * In particular, take the range product of "base" and "field" and
 * attach a name to the result.
 */
__isl_give isl_multi_pw_aff *pet_array_member(
	__isl_take isl_multi_pw_aff *base, __isl_take isl_multi_pw_aff *field)
{
	isl_ctx *ctx;
	isl_multi_pw_aff *access;
	const char *base_name, *field_name;
	char *name;

	ctx = isl_multi_pw_aff_get_ctx(base);

	base_name = isl_multi_pw_aff_get_tuple_name(base, isl_dim_out);
	field_name = isl_multi_pw_aff_get_tuple_name(field, isl_dim_out);
	name = pet_array_member_access_name(ctx, base_name, field_name);

	access = isl_multi_pw_aff_range_product(base, field);

	access = isl_multi_pw_aff_set_tuple_name(access, isl_dim_out, name);
	free(name);

	return access;
}
