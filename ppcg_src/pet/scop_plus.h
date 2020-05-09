#ifndef PET_SCOP_PLUS_H
#define PET_SCOP_PLUS_H

#include <string.h>
#include <set>

#include <isl/id.h>

#include "scop.h"

/* Compare two sequences of identifiers based on their names.
 */
struct array_desc_less {
	bool operator()(isl_id_list *x, isl_id_list *y) {
		int x_n = isl_id_list_n_id(x);
		int y_n = isl_id_list_n_id(y);

		for (int i = 0; i < x_n && i < y_n; ++i) {
			isl_id *x_i = isl_id_list_get_id(x, i);
			isl_id *y_i = isl_id_list_get_id(y, i);
			const char *x_name = isl_id_get_name(x_i);
			const char *y_name = isl_id_get_name(y_i);
			int cmp = strcmp(x_name, y_name);
			isl_id_free(x_i);
			isl_id_free(y_i);
			if (cmp)
				return cmp < 0;
		}

		return x_n < y_n;
	}
};

/* array_desc_set is a wrapper around a sorted set of identifier sequences,
 * with each identifier representing a (possibly renamed) ValueDecl.
 * The actual order is not important, only that it is consistent
 * across platforms.
 * The wrapper takes care of the memory management of the isl_id_list objects.
 * In particular, the set keeps hold of its own reference to these objects.
 */
struct array_desc_set : public std::set<isl_id_list *, array_desc_less>
{
	void insert(__isl_take isl_id_list *list) {
		if (find(list) == end())
			set::insert(list);
		else
			isl_id_list_free(list);
	}
	void erase(__isl_keep isl_id_list *list) {
		iterator it;

		it = find(list);
		if (it == end())
			return;

		isl_id_list_free(*it);
		set::erase(it);
	}
	~array_desc_set() {
		iterator it;

		for (it = begin(); it != end(); ++it)
			isl_id_list_free(*it);
	}
};

void pet_scop_collect_arrays(struct pet_scop *scop, array_desc_set &arrays);

#endif
