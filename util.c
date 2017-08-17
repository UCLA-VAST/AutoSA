/*
 * Copyright 2012-2013 Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d'Ulm, 75230 Paris, France
 */

#include <isl/space.h>
#include <isl/val.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/union_map.h>

#include "util.h"

/* Construct an isl_multi_val living in "space" with all values equal to "val".
 */
__isl_give isl_multi_val *ppcg_multi_val_from_int(__isl_take isl_space *space,
	int val)
{
	int i, n;
	isl_ctx *ctx;
	isl_val *v;
	isl_multi_val *mv;

	if (!space)
		return NULL;

	ctx = isl_space_get_ctx(space);
	n = isl_space_dim(space, isl_dim_set);
	mv = isl_multi_val_zero(space);
	v = isl_val_int_from_si(ctx, val);
	for (i = 0; i < n; ++i)
		mv = isl_multi_val_set_val(mv, i, isl_val_copy(v));
	isl_val_free(v);

	return mv;
}

/* Construct an isl_multi_val living in "space" with values specified
 * by "list".  "list" is assumed to have at least as many entries
 * as the set dimension of "space".
 */
__isl_give isl_multi_val *ppcg_multi_val_from_int_list(
	__isl_take isl_space *space, int *list)
{
	int i, n;
	isl_ctx *ctx;
	isl_multi_val *mv;

	if (!space)
		return NULL;

	ctx = isl_space_get_ctx(space);
	n = isl_space_dim(space, isl_dim_set);
	mv = isl_multi_val_zero(space);
	for (i = 0; i < n; ++i) {
		isl_val *v;

		v = isl_val_int_from_si(ctx, list[i]);
		mv = isl_multi_val_set_val(mv, i, v);
	}

	return mv;
}

/* Compute the size of a bounding box around the origin and "set",
 * where "set" is assumed to contain only non-negative elements.
 * In particular, compute the maximal value of "set" in each direction
 * and add one.
 */
__isl_give isl_multi_pw_aff *ppcg_size_from_extent(__isl_take isl_set *set)
{
	int i, n;
	isl_multi_pw_aff *mpa;

	n = isl_set_dim(set, isl_dim_set);
	mpa = isl_multi_pw_aff_zero(isl_set_get_space(set));
	for (i = 0; i < n; ++i) {
		isl_space *space;
		isl_aff *one;
		isl_pw_aff *bound;

		if (!isl_set_dim_has_upper_bound(set, isl_dim_set, i)) {
			const char *name;
			name = isl_set_get_tuple_name(set);
			if (!name)
				name = "";
			fprintf(stderr, "unable to determine extent of '%s' "
				"in dimension %d\n", name, i);
			set = isl_set_free(set);
		}
		bound = isl_set_dim_max(isl_set_copy(set), i);

		space = isl_pw_aff_get_domain_space(bound);
		one = isl_aff_zero_on_domain(isl_local_space_from_space(space));
		one = isl_aff_add_constant_si(one, 1);
		bound = isl_pw_aff_add(bound, isl_pw_aff_from_aff(one));
		mpa = isl_multi_pw_aff_set_pw_aff(mpa, i, bound);
	}
	isl_set_free(set);

	return mpa;
}

/* Construct a function from tagged iteration domains to the corresponding
 * untagged iteration domains with as range of the wrapped map in the domain
 * the reference tags that appear in the domain of "accesses".
 *
 * For example, if "accesses" contains tagged accesses with as domains
 * { [S[i,j] -> R_1[]] } and { [S[i,j] -> R_2[]] }, then the result contains
 *
 *	{ [S[i,j] -> R_1[]] -> S[i,j]; [S[i,j] -> R_2[]] -> S[i,j] }
 */
__isl_give isl_union_pw_multi_aff *ppcg_untag_from_tagged_accesses(
	__isl_take isl_union_map *accesses)
{
	isl_union_map *tagged;

	accesses = isl_union_map_universe(accesses);
	tagged = isl_union_set_unwrap(isl_union_map_domain(accesses));

	return isl_union_map_domain_map_union_pw_multi_aff(tagged);
}

/* Construct a map from domain_space to domain_space that increments
 * the dimension at position "pos" and leaves all other dimensions
 * constant.
 */
__isl_give isl_map *ppcg_next(__isl_take isl_space *domain_space, int pos)
{
	isl_space *space;
	isl_aff *aff;
	isl_multi_aff *next;

	space = isl_space_map_from_set(domain_space);
	next = isl_multi_aff_identity(space);
	aff = isl_multi_aff_get_aff(next, pos);
	aff = isl_aff_add_constant_si(aff, 1);
	next = isl_multi_aff_set_aff(next, pos, aff);

	return isl_map_from_multi_aff(next);
}
