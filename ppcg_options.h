#ifndef PPCG_OPTIONS_H
#define PPCG_OPTIONS_H

#include <isl/arg.h>

struct ppcg_debug_options {
	int dump_schedule_constraints;
	int dump_schedule;
	int dump_sizes;
};

struct ppcg_options {
	struct ppcg_debug_options *debug;

	int scale_tile_loops;
	int wrap;

	char *ctx;
	char *sizes;

	int tile_size;

	/* Take advantage of private memory. */
	int use_private_memory;

	/* Take advantage of shared memory. */
	int use_shared_memory;

	/* Maximal amount of shared memory. */
	int max_shared_memory;

	/* The target we generate code for. */
	int target;

	/* Generate OpenMP macros (C target only). */
	int openmp;

	/* Linearize all device arrays. */
	int linearize_device_arrays;

	/* Allow live range to be reordered. */
	int live_range_reordering;

	/* Options to pass to the OpenCL compiler.  */
	char *opencl_compiler_options;
	/* Prefer GPU device over CPU. */
	int opencl_use_gpu;
};

ISL_ARG_DECL(ppcg_debug_options, struct ppcg_debug_options,
	ppcg_debug_options_args)
ISL_ARG_DECL(ppcg_options, struct ppcg_options, ppcg_options_args)

#define		PPCG_TARGET_C		0
#define		PPCG_TARGET_CUDA	1
#define		PPCG_TARGET_OPENCL      2

#endif
