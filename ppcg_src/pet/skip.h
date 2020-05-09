#ifndef PET_SKIP_H
#define PET_SKIP_H

#include <pet.h>

#include "context.h"
#include "state.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum pet_skip_type {
	pet_skip_if,
	pet_skip_if_else,
	pet_skip_seq
};

/* Structure that handles the construction of skip conditions.
 *
 * scop_then and scop_else represent the then and else branches
 *	of the if statement
 *
 * scop1 and scop2 represent the two statements that are combined
 *
 * skip[type] is true if we need to construct a skip condition of that type
 * equal is set if the skip conditions of types pet_skip_now and pet_skip_later
 *	are equal to each other
 * index[type] is an index expression from a zero-dimension domain
 *	to the virtual array representing the skip condition
 * scop[type] is a scop for computing the skip condition
 */
struct pet_skip_info {
	isl_ctx *ctx;

	enum pet_skip_type type;

	int skip[2];
	int equal;
	isl_multi_pw_aff *index[2];
	struct pet_scop *scop[2];

	union {
		struct {
			struct pet_scop *scop_then;
			struct pet_scop *scop_else;
		} i;
		struct {
			struct pet_scop *scop1;
			struct pet_scop *scop2;
		} s;
	} u;
};

int pet_skip_info_has_skip(struct pet_skip_info *skip);

void pet_skip_info_if_init(struct pet_skip_info *skip, isl_ctx *ctx,
	struct pet_scop *scop_then, struct pet_scop *scop_else,
	int have_else, int affine);
void pet_skip_info_if_extract_index(struct pet_skip_info *skip,
	__isl_keep isl_multi_pw_aff *index, __isl_keep pet_context *pc,
	struct pet_state *state);
void pet_skip_info_if_extract_cond(struct pet_skip_info *skip,
	__isl_keep isl_pw_aff *cond, __isl_keep pet_context *pc,
	struct pet_state *state);

void pet_skip_info_seq_init(struct pet_skip_info *skip, isl_ctx *ctx,
	struct pet_scop *scop1, struct pet_scop *scop2);
void pet_skip_info_seq_extract(struct pet_skip_info *skip,
	__isl_keep pet_context *pc, struct pet_state *state);

struct pet_scop *pet_skip_info_add(struct pet_skip_info *skip,
	struct pet_scop *scop);

#if defined(__cplusplus)
}
#endif

#endif
