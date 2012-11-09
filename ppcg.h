#ifndef PPCG_H
#define PPCG_H

#include <isl/set.h>
#include <isl/union_set.h>
#include <isl/union_map.h>
#include <pet.h>

/* Representation of the scop for use inside PPCG.
 *
 * "context" represents constraints on the parameters.
 * "domain" is the union of all iteration domains.
 * "reads" contains all read accesses.
 * "live_in" contains read accesses that have no corresponding
 *	writes in the scop.
 * "writes" contains all write accesses.
 * "kills" contains all kill accesses.
 * "dep_flow" represents the flow dependences.
 * "schedule" represents the (original) schedule.
 *
 * "arrays" and "stmts" are copies of the corresponding elements
 * of the original pet_scop.
 */
struct ppcg_scop {
	isl_set *context;
	isl_union_set *domain;
	isl_union_map *reads;
	isl_union_map *live_in;
	isl_union_map *writes;
	isl_union_map *kills;
	isl_union_map *dep_flow;
	isl_union_map *schedule;

	int n_array;
	struct pet_array **arrays;
	int n_stmt;
	struct pet_stmt **stmts;
};

#endif
