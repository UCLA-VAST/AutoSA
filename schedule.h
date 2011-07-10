#ifndef _SCHEDULE_H
#define _SCHEDULE_H

/* An access to an array element or an iterator.
 * Accesses to iterators have an access relation that maps to an unnamed space.
 * An access may be both read and write.
 */
struct cuda_stmt_access {
	/* Access reads elements */
	int read;
	/* Access writes elements */
	int write;

	/* Index of the array reference group this reference belong to. */
	int group;

	/* Access relation */
	isl_map *access;

	struct cuda_stmt_access *next;
};

struct cuda_stmt {
	isl_set *domain;
	struct pet_expr *body;

	/* Number of tile dimensions. */
	int tile_len;
	/* Number of initial parallel loops among tile dimensions. */
	int n_parallel;

	/* Linked list of accesses. */
	struct cuda_stmt_access *accesses;
};

__isl_give isl_map *wavefront(__isl_take isl_dim *dim, int len,
        int first, int wave_len);
__isl_give isl_map *project_out(__isl_take isl_dim *dim,
	int len, int first, int n);
__isl_give isl_map *projection(__isl_take isl_dim *dim,
	int src_len, int dst_len);
__isl_give isl_set *extend(__isl_take isl_set *set, int dst_len);

#endif
