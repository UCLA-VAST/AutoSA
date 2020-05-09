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

#include "filter.h"

/* Construct a function that (upon precomposition) inserts
 * a filter value with name "id" and value "satisfied"
 * in the list of filter values embedded in the set space "space".
 *
 * If "space" does not contain any filter values yet, we first create
 * a function that inserts 0 filter values, i.e.,
 *
 *	[space -> []] -> space
 *
 * We can now assume that space is of the form [dom -> [filters]]
 * We construct an identity mapping on dom and a mapping on filters
 * that (upon precomposition) inserts the new filter
 *
 *	dom -> dom
 *	[satisfied, filters] -> [filters]
 *
 * and then compute the cross product
 *
 *	[dom -> [satisfied, filters]] -> [dom -> [filters]]
 */
__isl_give isl_pw_multi_aff *pet_filter_insert_pma(__isl_take isl_space *space,
	__isl_take isl_id *id, int satisfied)
{
	isl_space *space2;
	isl_multi_aff *ma;
	isl_pw_multi_aff *pma0, *pma, *pma_dom, *pma_ran;

	if (isl_space_is_wrapping(space)) {
		space2 = isl_space_map_from_set(isl_space_copy(space));
		ma = isl_multi_aff_identity(space2);
		space = isl_space_unwrap(space);
	} else {
		space = isl_space_from_domain(space);
		ma = isl_multi_aff_domain_map(isl_space_copy(space));
	}

	space2 = isl_space_domain(isl_space_copy(space));
	pma_dom = isl_pw_multi_aff_identity(isl_space_map_from_set(space2));
	space = isl_space_range(space);
	space = isl_space_insert_dims(space, isl_dim_set, 0, 1);
	pma_ran = isl_pw_multi_aff_project_out_map(space, isl_dim_set, 0, 1);
	pma_ran = isl_pw_multi_aff_set_dim_id(pma_ran, isl_dim_in, 0, id);
	pma_ran = isl_pw_multi_aff_fix_si(pma_ran, isl_dim_in, 0, satisfied);
	pma = isl_pw_multi_aff_product(pma_dom, pma_ran);

	pma0 = isl_pw_multi_aff_from_multi_aff(ma);
	pma = isl_pw_multi_aff_pullback_pw_multi_aff(pma0, pma);

	return pma;
}
