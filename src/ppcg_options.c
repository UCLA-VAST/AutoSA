/*
 * Copyright 2010-2011 INRIA Saclay
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include "ppcg_options.h"

static struct isl_arg_choice target[] = {
	//{"c", PPCG_TARGET_C},
	//{"cuda", PPCG_TARGET_CUDA},
	//{"opencl", PPCG_TARGET_OPENCL},
	//{"autosa_c", AUTOSA_TARGET_C},
	{"autosa_hls_c", AUTOSA_TARGET_XILINX_HLS_C},
	{"autosa_opencl", AUTOSA_TARGET_INTEL_OPENCL},
	//{"autosa_t2s", AUTOSA_TARGET_T2S},
	{"autosa_catapult_c", AUTOSA_TARGET_CATAPULT_HLS_C},
	{"autosa_tapa", AUTOSA_TARGET_TAPA_CPP},
	{0}};

static struct isl_arg_choice sa_type[] = {
	{"sync", AUTOSA_SA_TYPE_SYNC},
	{"async", AUTOSA_SA_TYPE_ASYNC},
	{0}};

/* Set defaults that depend on the target.
 * In particular, set --schedule-outer-coincidence iff target is a GPU.
 */
void ppcg_options_set_target_defaults(struct ppcg_options *options)
{
	char *argv[2] = {NULL};

	argv[0] = "ppcg_options_set_target_defaults";
	if (options->target == PPCG_TARGET_C)
		argv[1] = "--no-schedule-outer-coincidence";
	else
		argv[1] = "--schedule-outer-coincidence";

	isl_options_parse(options->isl, 2, argv, ISL_ARG_ALL);
}

/* Callback that is called whenever the "target" option is set (to "val").
 * The callback is called after target has been updated.
 *
 * Call ppcg_options_set_target_defaults to reset the target-dependent options.
 */
static int set_target(void *opt, unsigned val)
{
	struct ppcg_options *options = opt;

	ppcg_options_set_target_defaults(options);

	return 0;
}

ISL_ARGS_START(struct ppcg_debug_options, ppcg_debug_options_args)
ISL_ARG_BOOL(struct ppcg_debug_options, dump_schedule_constraints, 0,
			 "dump-schedule-constraints", 0, "dump schedule constraints")
ISL_ARG_BOOL(struct ppcg_debug_options, dump_schedule, 0,
			 "dump-schedule", 0, "dump isl computed schedule")
ISL_ARG_BOOL(struct ppcg_debug_options, dump_final_schedule, 0,
			 "dump-final-schedule", 0, "dump PPCG computed schedule")
ISL_ARG_BOOL(struct ppcg_debug_options, dump_sizes, 0,
			 "dump-sizes", 0,
			 "dump effectively used per kernel tile, grid and block sizes")
ISL_ARG_BOOL(struct ppcg_debug_options, verbose, 'v', "verbose", 0, NULL)
ISL_ARGS_END

//ISL_ARGS_START(struct ppcg_options, ppcg_opencl_options_args)
//ISL_ARG_STR(struct ppcg_options, opencl_compiler_options, 0, "compiler-options",
//			"options", NULL, "options to pass to the OpenCL compiler")
//ISL_ARG_BOOL(struct ppcg_options, opencl_use_gpu, 0, "use-gpu", 1,
//			 "use GPU device (if available)")
//ISL_ARG_STR_LIST(struct ppcg_options, opencl_n_include_file,
//				 opencl_include_files, 0, "include-file", "filename",
//				 "file to #include in generated OpenCL code")
//ISL_ARG_BOOL(struct ppcg_options, opencl_print_kernel_types, 0,
//			 "print-kernel-types", 1,
//			 "print definitions of types in the kernel file")
//ISL_ARG_BOOL(struct ppcg_options, opencl_embed_kernel_code, 0,
//			 "embed-kernel-code", 0, "embed kernel code into host code")
//ISL_ARGS_END

ISL_ARGS_START(struct autosa_options, autosa_options_args)
ISL_ARG_BOOL(struct autosa_options, autosa, 0, "autosa", 1,
				"generate systolic arrays using AutoSA")
ISL_ARG_BOOL(struct autosa_options, array_contraction, 0, "array-contraction", 1,
				"apply array contraction")
ISL_ARG_BOOL(struct autosa_options, axi_stream, 0, "axi-stream", 0,
				"generate AXI stream interface, must be used together with host serialization.")
ISL_ARG_BOOL(struct autosa_options, block_sparse, 0, "block-sparse", 0,
				"use block sparsity")
ISL_ARG_STR(struct autosa_options, block_sparse_ratio, 0, "block-sparse-ratio", "ratio",
				NULL, "block sparsity ratio (e.g., kernel[]->A[2,4])")
ISL_ARG_STR(struct autosa_options, config, 0, "config", "config", NULL,
				"AutoSA configuration file")
ISL_ARG_BOOL(struct autosa_options, credit_control, 0, "credit-control", 0,
			 	"enable credit control between different array partitions")
ISL_ARG_BOOL(struct autosa_options, data_pack, 0, "data-pack", 1,
			 	"enable data packing")
ISL_ARG_STR(struct autosa_options, data_pack_sizes, 0, "data-pack-sizes", "sizes",
				NULL, "data pack sizes upper bound (bytes) at innermost, intermediate, outermost I/O level [default: kernel[]->data_pack[8,32,64]]")
ISL_ARG_BOOL(struct autosa_options, double_buffer, 0, "double-buffer", 1,
			 	"enable double-buffering for data transfer")
ISL_ARG_STR(struct autosa_options, double_buffer_assignment, 0, "double-buffer-assign", "assignment",
				NULL, "assign arrays to be double bufferred (e.g., kernel[]->A[])")
ISL_ARG_INT(struct autosa_options, double_buffer_style, 0, "double-buffer-style", "id", 1,
				"change double-buffering logic coding style (0: while loop 1: for loop)")
ISL_ARG_BOOL(struct autosa_options, dump_code, 0, "dump-code", 0,
			 	"dump the intermediate code")
ISL_ARG_BOOL(struct autosa_options, explore_loop_permute, 0, "explore-loop-permute", 0,
				"explore loop permutation in the step of array partitioning")
ISL_ARG_INT(struct autosa_options, loop_permute_order, 0, "loop-permute-order", "order", 0,
				"specify which loop ordering to be explored")
ISL_ARG_INT(struct autosa_options, fifo_depth, 0, "fifo-depth", "depth", 2, "default FIFO depth")
ISL_ARG_BOOL(struct autosa_options, hbm, 0, "hbm", 0,
			 	"use multi-port DRAM/HBM")
ISL_ARG_INT(struct autosa_options, n_hbm_port, 0, "hbm-port-num", "num", 2,
				"default HBM port number per array")
ISL_ARG_BOOL(struct autosa_options, hls, 0, "hls", 0,
			 	"generate Xilinx HLS host")
ISL_ARG_BOOL(struct autosa_options, host_serialize, 0, "host-serialize", 0,
			 	"serialize/deserialize the host data")
ISL_ARG_BOOL(struct autosa_options, insert_hls_dependence, 0, "insert-hls-dependence", 0,
			 	"insert Xilinx HLS dependence pragma (alpha version)")
ISL_ARG_INT(struct autosa_options, int_io_dir, 0, "int-io-dir", "dir", 0,
			 	"set the default interior I/O direction (0: [1,x] 1: [x,1])")
ISL_ARG_BOOL(struct autosa_options, io_module_embedding, 0, "io-module-embedding", 0,
			 	"embed the I/O modules inside PEs if possible")
ISL_ARG_BOOL(struct autosa_options, isl_sink, 0, "isl-sink", 1,
			 	"sink time loops using ISL default APIs")
ISL_ARG_BOOL(struct autosa_options, loop_infinitize, 0, "loop-infinitize", 0,
			 	"apply loop infinitization optimization (Intel OpenCL only)")
ISL_ARG_BOOL(struct autosa_options, local_reduce, 0, "local-reduce", 0,
			 	"generate non-output-stationary array with local reduction")
ISL_ARG_STR(struct autosa_options, reduce_op, 0, "reduce-op", "op",
				NULL, "reduction operator (must be used with local-reduce together)")			 
ISL_ARG_BOOL(struct autosa_options, lower_int_io_L1_buffer, 0, "lower-int-io-L1-buffer", 0,
			 	"lower the L1 buffer for interior I/O modules")
ISL_ARG_BOOL(struct autosa_options, lower_if_branch, 0, "lower-if-branch", 0,
				"lower if branch in the I/O module")
ISL_ARG_INT(struct autosa_options, max_local_memory, 0,
				"max-local-memory", "size", 8192, "maximal amount of local memory")
ISL_ARG_INT(struct autosa_options, max_sa_dim, 0,
				"max-sa-dim", "dim", 2, "maximal systolic array dimension")			 
ISL_ARG_STR(struct autosa_options, mem_port_map, 0, "mem-port-map", "map", NULL,
				"memory port mapping")
ISL_ARG_BOOL(struct autosa_options, non_block_fifo, 0, "non-blocking-fifo", 0,
			 	"use non-blocking fifo interface")
ISL_ARG_STR(struct autosa_options, output_dir, 0, "output-dir", "dir", "./autosa.tmp/output",
				"AutoSA Output directory")
ISL_ARG_BOOL(struct autosa_options, reverse_order, 0, "reverse-order", 1,
			 	"reverse latency hiding loop tiling order")			
ISL_ARG_STR(struct autosa_options, select_rar_dep, 0, "select-rar-dep", "choice",
				NULL, "select the RAR dependence for the array access. [example: kernel[]->__pet_ref_4[1]]")
ISL_ARG_STR(struct autosa_options, sa_sizes, 0, "sa-sizes", "sizes", NULL,
				"per kernel PE optimization tile sizes")
ISL_ARG_INT(struct autosa_options, sa_tile_size, 0, "sa-tile-size", "size", 4,
				"default tile size in PE optmization")
ISL_ARG_USER_OPT_CHOICE(struct autosa_options, sa_type, 0, "sa-type", sa_type,
				NULL, AUTOSA_SA_TYPE_ASYNC, AUTOSA_SA_TYPE_ASYNC, "systolic array type")
ISL_ARG_STR(struct autosa_options, simd_info, 0, "simd-info", "info", NULL,
				"per kernel SIMD information")
ISL_ARG_BOOL(struct autosa_options, simd_touch_space, 0, "simd-touch-space", 0,
				"use space loops as SIMD vectorization loops")
ISL_ARG_INT(struct autosa_options, tuning_method, 0, "tuning-method", "method", -1,
				"tuning method (0: exhaustive search 1: others)")
ISL_ARG_BOOL(struct autosa_options, two_level_buffer, 0, "two-level-buffer", 0,
			 	"enable two-level buffering in I/O modules")
ISL_ARG_BOOL(struct autosa_options, t2s_tile, 0, "t2s-tile", 0,
			 	"generate T2S code from tiled code")
ISL_ARG_INT(struct autosa_options, t2s_tile_phase, 0,
				"t2s-tile-phase", "phase", 0, "T2S tiled URE codegen phase")
ISL_ARG_STR(struct autosa_options, param_names, 0, "param-names", "name", NULL,
				"customized parameter names (for tuning)")
ISL_ARG_BOOL(struct autosa_options, uram, 0, "uram", 0,
			 	"use Xilinx FPGA URAM")
ISL_ARG_BOOL(struct autosa_options, use_local_memory, 0, "local-memory", 1,
			 	"use local memory in kernel code")
ISL_ARG_BOOL(struct autosa_options, use_cplusplus_template, 0, "use-cplusplus-template", 0,
			 	"use C++ template in codegen (necessary for irregular PEs)")			 
ISL_ARG_BOOL(struct autosa_options, verbose, 'v', "verbose", 0,
			 	"print verbose compilation information")
ISL_ARG_BOOL(struct autosa_options, hcl, 0, "hcl", 0,
			 	"generate code for integrating with HeteroCL")			 
ISL_ARGS_END

ISL_ARGS_START(struct ppcg_options, ppcg_options_args)
ISL_ARG_CHILD(struct ppcg_options, isl, "isl", &isl_options_args, "isl options")
ISL_ARG_CHILD(struct ppcg_options, debug, NULL, &ppcg_debug_options_args,
			  "debugging options")
ISL_ARG_CHILD(struct ppcg_options, autosa, "autosa", &autosa_options_args,
			  "AutoSA options")
//ISL_ARG_BOOL(struct ppcg_options, group_chains, 0, "group-chains", 1,
//			 "group chains of interdependent statements that are executed "
//			 "consecutively in the original schedule before scheduling")
ISL_ARG_BOOL(struct ppcg_options, reschedule, 0, "reschedule", 1,
			 "replace original schedule by isl computed schedule")
//ISL_ARG_BOOL(struct ppcg_options, scale_tile_loops, 0,
//			 "scale-tile-loops", 1, NULL)
//ISL_ARG_BOOL(struct ppcg_options, wrap, 0, "wrap", 1, NULL)
//ISL_ARG_BOOL(struct ppcg_options, use_shared_memory, 0, "shared-memory", 1,
//			 "use shared memory in kernel code")
//ISL_ARG_BOOL(struct ppcg_options, use_private_memory, 0, "private-memory", 1,
//			 "use private memory in kernel code")
//ISL_ARG_STR(struct ppcg_options, ctx, 0, "ctx", "context", NULL,
//			"Constraints on parameters")
//ISL_ARG_BOOL(struct ppcg_options, non_negative_parameters, 0,
//			 "assume-non-negative-parameters", 0,
//			 "assume all parameters are non-negative)")
//ISL_ARG_BOOL(struct ppcg_options, tile, 0, "tile", 0,
//			 "perform tiling (C target)")
//ISL_ARG_INT(struct ppcg_options, tile_size, 'S', "tile-size", "size", 32, NULL)
//ISL_ARG_BOOL(struct ppcg_options, isolate_full_tiles, 0, "isolate-full-tiles",
//			 0, "isolate full tiles from partial tiles (hybrid tiling)")
//ISL_ARG_STR(struct ppcg_options, sizes, 0, "sizes", "sizes", NULL,
//			"Per kernel tile, grid and block sizes")
//ISL_ARG_INT(struct ppcg_options, max_shared_memory, 0,
//			"max-shared-memory", "size", 8192, "maximal amount of shared memory")
//ISL_ARG_BOOL(struct ppcg_options, openmp, 0, "openmp", 0,
//			 "Generate OpenMP macros (only for C target)")
ISL_ARG_USER_OPT_CHOICE(struct ppcg_options, target, 0, "target", target,
						&set_target, PPCG_TARGET_CUDA, PPCG_TARGET_CUDA,
						"the target to generate code for")
ISL_ARG_BOOL(struct ppcg_options, linearize_device_arrays, 0,
			 "linearize-device-arrays", 1,
			 "linearize all device arrays, even those of fixed size")
//ISL_ARG_BOOL(struct ppcg_options, allow_gnu_extensions, 0,
//			 "allow-gnu-extensions", 1,
//			 "allow the use of GNU extensions in generated code")
ISL_ARG_BOOL(struct ppcg_options, live_range_reordering, 0,
			 "live-range-reordering", 0,
			 "allow successive live ranges on the same memory element "
			 "to be reordered")
//ISL_ARG_BOOL(struct ppcg_options, hybrid, 0, "hybrid", 0,
//			 "apply hybrid tiling whenever a suitable input pattern is found "
//			 "(GPU targets)")
//ISL_ARG_BOOL(struct ppcg_options, unroll_copy_shared, 0, "unroll-copy-shared",
//			 0, "unroll code for copying to/from shared memory")
//ISL_ARG_BOOL(struct ppcg_options, unroll_gpu_tile, 0, "unroll-gpu-tile", 0,
//			 "unroll code inside tile on GPU targets")
//ISL_ARG_GROUP("opencl", &ppcg_opencl_options_args, "OpenCL options")
//ISL_ARG_STR(struct ppcg_options, save_schedule_file, 0, "save-schedule",
//			"file", NULL, "save isl computed schedule to <file>")
//ISL_ARG_STR(struct ppcg_options, load_schedule_file, 0, "load-schedule",
//			"file", NULL, "load schedule from <file>, "
//						  "using it instead of an isl computed schedule")
ISL_ARGS_END
