#ifndef _GPUCODE_H
#define _GPUCODE_H

#include <cloog/isl/cloog.h>

struct gpucode_info {
	int indent;
	FILE *dst;
	void (*print_user_stmt)(struct gpucode_info *info,
				struct clast_user_stmt *s);
	void (*print_user_stmt_list)(struct gpucode_info *info,
				     struct clast_user_stmt *s);
	void (*print_for_head)(struct gpucode_info *info, struct clast_for *f);
	void (*print_for_foot)(struct gpucode_info *info, struct clast_for *f);
	void *user;
};

void print_cloog_macros(FILE *dst);
void print_indent(FILE *dst, int indent);
void gpu_print_host_stmt(struct gpucode_info *info, struct clast_stmt *s);

__isl_give isl_set *extract_host_domain(struct clast_user_stmt *u);
__isl_give isl_set *extract_entire_host_domain(struct clast_stmt *s);

#endif
