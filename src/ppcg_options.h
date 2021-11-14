#ifndef PPCG_OPTIONS_H
#define PPCG_OPTIONS_H

#include <isl/arg.h>
#include <isl/options.h>

#ifdef __cplusplus
extern "C"
{
#endif

	struct ppcg_debug_options
	{
		int dump_schedule_constraints;
		int dump_schedule;
		int dump_final_schedule;
		int dump_sizes;
		int verbose;
	};

	struct autosa_options
	{
		/* Generate systolic array using AutoSA. */
		int autosa;
		/* Use HBM memory. */
		int hbm;
		int n_hbm_port;
		/* Enable double buffering. */
		int double_buffer;
		/* Double buffer assignment. */
		char *double_buffer_assignment;
		/* Dump the intermediate code. */
		int dump_code;
		/* Maximal systolic array dimension. */
		int max_sa_dim;
		/* Systolic array type. */
		int sa_type;
		/* Universal tile size. */
		int sa_tile_size;
		/* Tile sizes for PE optimization. */
		char *sa_sizes;
		/* Generate T2S code from tiled program. */
		int t2s_tile;
		/* Phases of T2S codegen for tiled program. */
		int t2s_tile_phase;
		/* Take advantage of FPGA local memory. */
		int use_local_memory;
		/* Maximal amount of local memory. */
		int max_local_memory;
		/* Memory port mapping (for Intel OpenCL). */
		char *mem_port_map;
		/* Enable data pack for transferring data. */
		int data_pack;
		/* Data pack factors at different I/O levels. */
		char *data_pack_sizes;
		/* Enable credit control between different array partitions. */
		int credit_control;
		/* Enable two-level buffering in I/O modules. */
		int two_level_buffer;
		/* Configuration file. */
		char *config;
		/* Output directory. */
		char *output_dir;
		/* SIMD information file. */
		char *simd_info;
		/* Generate HLS host instead of OpenCL host. */
		int hls;
		/* Use URAM. */
		int uram;
		/* Print verbose information. */
		int verbose;
		/* Insert HLS dependence pragma. */
		int insert_hls_dependence;
		/* Embed I/O modules inside PEs. */
		int io_module_embedding;
		/* Enable loop infinitization optimization. Only for Intel. */
		int loop_infinitize;
		/* Enable data serialization/deserialization on the host side. */
		int host_serialize;
		/* Use non-blocking FIFO access. Note: Not supported. */
		int non_block_fifo;
		/* Double buffer coding style. 0: for loop (default) 1: while loop */
		int double_buffer_style;
		/* Enable local reduce */
		int local_reduce;
		/* Reduce op */
		char *reduce_op;
		/* Interior I/O elimination direction. 
		 * 0: set the first dim to 1 (default). 
		 * 1: Set the last dim to 1.
		 */
		/* Select the RAR dependence candidate. */
		char *select_rar_dep;
		int int_io_dir;
		/* Lower the interior I/O module L1 buffer */
		int lower_int_io_L1_buffer;
		/* Use C++ template in codegen (necessary for irregular PEs) */
		int use_cplusplus_template;
		/* Default FIFO depth */
		int fifo_depth;
		/* Touch space loops in the SIMD vectorization */
		int simd_touch_space;
		/* Use block sparsity */
		int block_sparse;
		/* Block sparse ratio [nonzero, vec_len] */
		char* block_sparse_ratio;
		/* Generate code for HeteroCL integration. */
		int hcl;
		/* Apply array contraction. */
		int array_contraction;
		/* Sinking time loops using ISL default APIs. */
		int isl_sink;
		/* Reverse the loop tiling order. */
		int reverse_order;
		/* Use AXI Stream Interface. */
		int axi_stream;
		/* Tuning method: [0: Exhaustive search 1: Others] */
		int tuning_method;
		/* Explore loop permutation in the array partitioning. */
		int explore_loop_permute;
		int loop_permute_order;
		/* Parameter names */
		char *param_names;
		/* Lowering if-branch in inter-trans I/O module. */
		int lower_if_branch;
	};	

	struct ppcg_options
	{
		struct isl_options *isl;
		struct ppcg_debug_options *debug;
		/* Options to pass to the AutoSA compiler. */
		struct autosa_options *autosa;

		/* Group chains of consecutive statements before scheduling. */
		int group_chains;

		/* Use isl to compute a schedule replacing the original schedule. */
		int reschedule;
		int scale_tile_loops;
		int wrap;

		/* Assume all parameters are non-negative. */
		int non_negative_parameters;
		char *ctx;
		char *sizes;

		/* Perform tiling (C target). */
		int tile;
		int tile_size;

		/* Isolate full tiles from partial tiles. */
		int isolate_full_tiles;

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

		/* Allow the use of GNU extensions in generated code. */
		int allow_gnu_extensions;

		/* Allow live range to be reordered. */
		int live_range_reordering;

		/* Allow hybrid tiling whenever a suitable input pattern is found. */
		int hybrid;

		/* Unroll the code for copying to/from shared memory. */
		int unroll_copy_shared;
		/* Unroll code inside tile on GPU targets. */
		int unroll_gpu_tile;

		/* Options to pass to the OpenCL compiler.  */
		char *opencl_compiler_options;
		/* Prefer GPU device over CPU. */
		int opencl_use_gpu;
		/* Number of files to include. */
		int opencl_n_include_file;
		/* Files to include. */
		const char **opencl_include_files;
		/* Print definitions of types in kernels. */
		int opencl_print_kernel_types;
		/* Embed OpenCL kernel code in host code. */
		int opencl_embed_kernel_code;

		/* Name of file for saving isl computed schedule or NULL. */
		char *save_schedule_file;
		/* Name of file for loading schedule or NULL. */
		char *load_schedule_file;
	};

	ISL_ARG_DECL(ppcg_debug_options, struct ppcg_debug_options,
				 ppcg_debug_options_args)
	ISL_ARG_DECL(autosa_options, struct autosa_options, autosa_options_args)
	ISL_ARG_DECL(ppcg_options, struct ppcg_options, ppcg_options_args)

#define PPCG_TARGET_C 0
#define PPCG_TARGET_CUDA 1
#define PPCG_TARGET_OPENCL 2
#define AUTOSA_TARGET_XILINX_HLS_C 3
#define AUTOSA_TARGET_INTEL_OPENCL 4
#define AUTOSA_TARGET_T2S 5
#define AUTOSA_TARGET_C 6
#define AUTOSA_TARGET_CATAPULT_HLS_C 7
#define AUTOSA_TARGET_TAPA_CPP 8

#define AUTOSA_SA_TYPE_SYNC 0
#define AUTOSA_SA_TYPE_ASYNC 1

	void ppcg_options_set_target_defaults(struct ppcg_options *options);

#ifdef __cplusplus
}
#endif

#endif
