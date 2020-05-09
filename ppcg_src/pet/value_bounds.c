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

#include <isl/map.h>

#include "expr.h"
#include "value_bounds.h"

/* If "value_bounds" contains any bounds on the variable accessed by "arg",
 * then intersect the range of "map" with the valid set of values.
 */
static __isl_give isl_map *access_apply_value_bounds(__isl_take isl_map *map,
	__isl_keep pet_expr *arg, __isl_keep isl_union_map *value_bounds)
{
	isl_id *id;
	isl_map *vb;
	isl_space *space;
	isl_ctx *ctx = isl_map_get_ctx(map);

	id = pet_expr_access_get_id(arg);
	space = isl_space_alloc(ctx, 0, 0, 1);
	space = isl_space_set_tuple_id(space, isl_dim_in, id);
	vb = isl_union_map_extract_map(value_bounds, space);
	if (!isl_map_plain_is_empty(vb))
		map = isl_map_intersect_range(map, isl_map_range(vb));
	else
		isl_map_free(vb);

	return map;
}

/* Given a set "domain", return a wrapped relation with the given set
 * as domain and a range of dimension "n_arg", where each coordinate
 * is either unbounded or, if the corresponding element of args is of
 * type pet_expr_access, bounded by the bounds specified by "value_bounds".
 */
__isl_give isl_set *pet_value_bounds_apply(__isl_take isl_set *domain,
	unsigned n_arg, __isl_keep pet_expr **args,
	__isl_keep isl_union_map *value_bounds)
{
	int i;
	isl_map *map;
	isl_space *space;

	map = isl_map_from_domain(domain);
	space = isl_map_get_space(map);
	space = isl_space_add_dims(space, isl_dim_out, 1);

	for (i = 0; i < n_arg; ++i) {
		isl_map *map_i;
		pet_expr *arg = args[i];

		map_i = isl_map_universe(isl_space_copy(space));
		if (arg->type == pet_expr_access)
			map_i = access_apply_value_bounds(map_i, arg,
							value_bounds);
		map = isl_map_flat_range_product(map, map_i);
	}
	isl_space_free(space);

	return isl_map_wrap(map);
}
