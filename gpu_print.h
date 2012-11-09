#ifndef GPU_PRINT_H
#define GPU_PRINT_H

#include "gpu.h"

__isl_give isl_printer *gpu_print_macros(__isl_take isl_printer *p,
	__isl_keep isl_ast_node *node);

__isl_give isl_printer *gpu_array_info_print_size(__isl_take isl_printer *prn,
	struct gpu_array_info *array);

__isl_give isl_printer *ppcg_kernel_print_copy(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt);
__isl_give isl_printer *ppcg_kernel_print_domain(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt);

#endif
