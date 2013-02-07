#ifndef _CUDA_COMMON_H_
#define _CUDA_COMMON_H_

#include <stdio.h>

/* start and end are file offsets of the program text that corresponds
 * to the scop being transformed.
 */
struct cuda_info {
	unsigned start;
	unsigned end;

	FILE *input;
	FILE *host_c;
	FILE *kernel_c;
	FILE *kernel_h;
};

void cuda_open_files(struct cuda_info *info, const char *input);
void cuda_close_files(struct cuda_info *info);

#endif
