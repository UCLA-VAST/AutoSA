#ifndef PET_STATE_H
#define PET_STATE_H

#include <pet.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Global state of pet_scop_from_pet_tree.
 *
 * "extract_array" is a callback specified by the user that can be
 * used to create a pet_array corresponding to the variable accessed
 * by "access".
 * "int_size" is the number of bytes needed to represent an integer.
 *
 * "n_loop" is the sequence number of the next loop.
 * "n_stmt" is the sequence number of the next statement.
 * "n_test" is the sequence number of the next virtual scalar.
 */
struct pet_state {
	isl_ctx *ctx;

	struct pet_array *(*extract_array)(__isl_keep pet_expr *access,
		__isl_keep pet_context *pc, void *user);
	void *user;
	int int_size;

	int n_loop;
	int n_stmt;
	int n_test;
};

#if defined(__cplusplus)
}
#endif

#endif
