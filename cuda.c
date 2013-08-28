/*
 * Copyright 2012      Ecole Normale Superieure
 *
 * Use of this software is governed by the MIT license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <isl/aff.h>
#include <isl/ast.h>

#include "cuda_common.h"
#include "cuda.h"
#include "gpu.h"
#include "gpu_print.h"
#include "print.h"

static __isl_give isl_printer *print_cuda_macros(__isl_take isl_printer *p)
{
	const char *macros =
		"#define cudaCheckReturn(ret) \\\n"
		"  do { \\\n"
		"    cudaError_t cudaCheckReturn_e = (ret); \\\n"
		"    if (cudaCheckReturn_e != cudaSuccess) { \\\n"
		"      fprintf(stderr, \"CUDA error: %s\\n\", "
		"cudaGetErrorString(cudaCheckReturn_e)); \\\n"
		"      fflush(stderr); \\\n"
		"    } \\\n"
		"    assert(cudaCheckReturn_e == cudaSuccess); \\\n"
		"  } while(0)\n"
		"#define cudaCheckKernel() \\\n"
		"  do { \\\n"
		"    cudaCheckReturn(cudaGetLastError()); \\\n"
		"  } while(0)\n\n";

	p = isl_printer_print_str(p, macros);
	return p;
}

/* Print a declaration for the device array corresponding to "array" on "p".
 */
static __isl_give isl_printer *declare_device_array(__isl_take isl_printer *p,
	struct gpu_array_info *array)
{
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, array->type);
	p = isl_printer_print_str(p, " *dev_");
	p = isl_printer_print_str(p, array->name);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

static __isl_give isl_printer *declare_device_arrays(__isl_take isl_printer *p,
	struct gpu_prog *prog)
{
	int i;

	for (i = 0; i < prog->n_array; ++i) {
		if (gpu_array_is_read_only_scalar(&prog->array[i]))
			continue;

		p = declare_device_array(p, &prog->array[i]);
	}
	p = isl_printer_start_line(p);
	p = isl_printer_end_line(p);
	return p;
}

static __isl_give isl_printer *allocate_device_arrays(
	__isl_take isl_printer *p, struct gpu_prog *prog)
{
	int i;

	for (i = 0; i < prog->n_array; ++i) {
		if (gpu_array_is_read_only_scalar(&prog->array[i]))
			continue;
		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p,
			"cudaCheckReturn(cudaMalloc((void **) &dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");
		p = gpu_array_info_print_size(p, &prog->array[i]);
		p = isl_printer_print_str(p, "));");
		p = isl_printer_end_line(p);
	}
	p = isl_printer_start_line(p);
	p = isl_printer_end_line(p);
	return p;
}

static __isl_give isl_printer *copy_arrays_to_device(__isl_take isl_printer *p,
	struct gpu_prog *prog)
{
	int i;

	for (i = 0; i < prog->n_array; ++i) {
		isl_space *dim;
		isl_set *read_i;
		int empty;

		if (gpu_array_is_read_only_scalar(&prog->array[i]))
			continue;

		dim = isl_space_copy(prog->array[i].space);
		read_i = isl_union_set_extract_set(prog->copy_in, dim);
		empty = isl_set_fast_is_empty(read_i);
		isl_set_free(read_i);
		if (empty)
			continue;

		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "cudaCheckReturn(cudaMemcpy(dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");

		if (gpu_array_is_scalar(&prog->array[i]))
			p = isl_printer_print_str(p, "&");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");

		p = gpu_array_info_print_size(p, &prog->array[i]);
		p = isl_printer_print_str(p, ", cudaMemcpyHostToDevice));");
		p = isl_printer_end_line(p);
	}
	p = isl_printer_start_line(p);
	p = isl_printer_end_line(p);
	return p;
}

static void print_reverse_list(FILE *out, int len, int *list)
{
	int i;

	if (len == 0)
		return;

	fprintf(out, "(");
	for (i = 0; i < len; ++i) {
		if (i)
			fprintf(out, ", ");
		fprintf(out, "%d", list[len - 1 - i]);
	}
	fprintf(out, ")");
}

/* Print the effective grid size as a list of the sizes in each
 * dimension, from innermost to outermost.
 */
static __isl_give isl_printer *print_grid_size(__isl_take isl_printer *p,
	struct ppcg_kernel *kernel)
{
	int i;
	int dim;

	dim = isl_multi_pw_aff_dim(kernel->grid_size, isl_dim_set);
	if (dim == 0)
		return p;

	p = isl_printer_print_str(p, "(");
	for (i = dim - 1; i >= 0; --i) {
		isl_pw_aff *bound;

		bound = isl_multi_pw_aff_get_pw_aff(kernel->grid_size, i);
		p = isl_printer_print_pw_aff(p, bound);
		isl_pw_aff_free(bound);

		if (i > 0)
			p = isl_printer_print_str(p, ", ");
	}

	p = isl_printer_print_str(p, ")");

	return p;
}

/* Print the grid definition.
 */
static __isl_give isl_printer *print_grid(__isl_take isl_printer *p,
	struct ppcg_kernel *kernel)
{
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "dim3 k");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p, "_dimGrid");
	p = print_grid_size(p, kernel);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

/* Print the arguments to a kernel declaration or call.  If "types" is set,
 * then print a declaration (including the types of the arguments).
 *
 * The arguments are printed in the following order
 * - the arrays accessed by the kernel
 * - the parameters
 * - the host loop iterators
 */
static __isl_give isl_printer *print_kernel_arguments(__isl_take isl_printer *p,
	struct gpu_prog *prog, struct ppcg_kernel *kernel, int types)
{
	int i, n;
	int first = 1;
	unsigned nparam;
	isl_space *space;
	const char *type;

	for (i = 0; i < prog->n_array; ++i) {
		isl_set *arr;
		int empty;

		space = isl_space_copy(prog->array[i].space);
		arr = isl_union_set_extract_set(kernel->arrays, space);
		empty = isl_set_fast_is_empty(arr);
		isl_set_free(arr);
		if (empty)
			continue;

		if (!first)
			p = isl_printer_print_str(p, ", ");

		if (types)
			p = gpu_array_info_print_declaration_argument(p,
				&prog->array[i]);
		else
			p = gpu_array_info_print_call_argument(p,
				&prog->array[i]);

		first = 0;
	}

	space = isl_union_set_get_space(kernel->arrays);
	nparam = isl_space_dim(space, isl_dim_param);
	for (i = 0; i < nparam; ++i) {
		const char *name;

		name = isl_space_get_dim_name(space, isl_dim_param, i);

		if (!first)
			p = isl_printer_print_str(p, ", ");
		if (types)
			p = isl_printer_print_str(p, "int ");
		p = isl_printer_print_str(p, name);

		first = 0;
	}
	isl_space_free(space);

	n = isl_space_dim(kernel->space, isl_dim_set);
	type = isl_options_get_ast_iterator_type(prog->ctx);
	for (i = 0; i < n; ++i) {
		const char *name;
		isl_id *id;

		if (!first)
			p = isl_printer_print_str(p, ", ");
		name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
		if (types) {
			p = isl_printer_print_str(p, type);
			p = isl_printer_print_str(p, " ");
		}
		p = isl_printer_print_str(p, name);

		first = 0;
	}

	return p;
}

/* Print the header of the given kernel.
 */
static __isl_give isl_printer *print_kernel_header(__isl_take isl_printer *p,
	struct gpu_prog *prog, struct ppcg_kernel *kernel)
{
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "__global__ void kernel");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p, "(");
	p = print_kernel_arguments(p, prog, kernel, 1);
	p = isl_printer_print_str(p, ")");

	return p;
}

/* Print the header of the given kernel to both gen->cuda.kernel_h
 * and gen->cuda.kernel_c.
 */
static void print_kernel_headers(struct gpu_prog *prog,
	struct ppcg_kernel *kernel, struct cuda_info *cuda)
{
	isl_printer *p;

	p = isl_printer_to_file(prog->ctx, cuda->kernel_h);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = print_kernel_header(p, prog, kernel);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);
	isl_printer_free(p);

	p = isl_printer_to_file(prog->ctx, cuda->kernel_c);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = print_kernel_header(p, prog, kernel);
	p = isl_printer_end_line(p);
	isl_printer_free(p);
}

static void print_indent(FILE *dst, int indent)
{
	fprintf(dst, "%*s", indent, "");
}

static void print_kernel_iterators(FILE *out, struct ppcg_kernel *kernel)
{
	int i, n_grid;
	isl_ctx *ctx = isl_ast_node_get_ctx(kernel->tree);
	const char *type;
	const char *block_dims[] = { "blockIdx.x", "blockIdx.y" };
	const char *thread_dims[] = { "threadIdx.x", "threadIdx.y",
					"threadIdx.z" };

	type = isl_options_get_ast_iterator_type(ctx);

	n_grid = isl_multi_pw_aff_dim(kernel->grid_size, isl_dim_set);
	if (n_grid > 0) {
		print_indent(out, 4);
		fprintf(out, "%s ", type);
		for (i = 0; i < n_grid; ++i) {
			if (i)
				fprintf(out, ", ");
			fprintf(out, "b%d = %s",
				i, block_dims[n_grid - 1 - i]);
		}
		fprintf(out, ";\n");
	}

	if (kernel->n_block > 0) {
		print_indent(out, 4);
		fprintf(out, "%s ", type);
		for (i = 0; i < kernel->n_block; ++i) {
			if (i)
				fprintf(out, ", ");
			fprintf(out, "t%d = %s",
				i, thread_dims[kernel->n_block - 1 - i]);
		}
		fprintf(out, ";\n");
	}
}

static __isl_give isl_printer *print_kernel_var(__isl_take isl_printer *p,
	struct ppcg_kernel_var *var)
{
	int j;

	p = isl_printer_start_line(p);
	if (var->type == ppcg_access_shared)
		p = isl_printer_print_str(p, "__shared__ ");
	p = isl_printer_print_str(p, var->array->type);
	p = isl_printer_print_str(p, " ");
	p = isl_printer_print_str(p,  var->name);
	for (j = 0; j < var->array->n_index; ++j) {
		isl_val *v;

		p = isl_printer_print_str(p, "[");
		v = isl_vec_get_element_val(var->size, j);
		p = isl_printer_print_val(p, v);
		isl_val_free(v);
		p = isl_printer_print_str(p, "]");
	}
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	return p;
}

static __isl_give isl_printer *print_kernel_vars(__isl_take isl_printer *p,
	struct ppcg_kernel *kernel)
{
	int i;

	for (i = 0; i < kernel->n_var; ++i)
		p = print_kernel_var(p, &kernel->var[i]);

	return p;
}

/* Print a sync statement.
 */
static __isl_give isl_printer *print_sync(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "__syncthreads();");
	p = isl_printer_end_line(p);

	return p;
}

/* This function is called for each user statement in the AST,
 * i.e., for each kernel body statement, copy statement or sync statement.
 */
static __isl_give isl_printer *print_kernel_stmt(__isl_take isl_printer *p,
	__isl_take isl_ast_print_options *print_options,
	__isl_keep isl_ast_node *node, void *user)
{
	isl_id *id;
	struct ppcg_kernel_stmt *stmt;

	id = isl_ast_node_get_annotation(node);
	stmt = isl_id_get_user(id);
	isl_id_free(id);

	isl_ast_print_options_free(print_options);

	switch (stmt->type) {
	case ppcg_kernel_copy:
		return ppcg_kernel_print_copy(p, stmt);
	case ppcg_kernel_sync:
		return print_sync(p, stmt);
	case ppcg_kernel_domain:
		return ppcg_kernel_print_domain(p, stmt);
	}

	return p;
}

static void print_kernel(struct gpu_prog *prog, struct ppcg_kernel *kernel,
	struct cuda_info *cuda)
{
	isl_ctx *ctx = isl_ast_node_get_ctx(kernel->tree);
	isl_ast_print_options *print_options;
	isl_printer *p;

	print_kernel_headers(prog, kernel, cuda);
	fprintf(cuda->kernel_c, "{\n");
	print_kernel_iterators(cuda->kernel_c, kernel);

	p = isl_printer_to_file(ctx, cuda->kernel_c);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = isl_printer_indent(p, 4);

	p = print_kernel_vars(p, kernel);
	p = isl_printer_end_line(p);
	p = gpu_print_macros(p, kernel->tree);

	print_options = isl_ast_print_options_alloc(ctx);
	print_options = isl_ast_print_options_set_print_user(print_options,
						    &print_kernel_stmt, NULL);
	p = isl_ast_node_print(kernel->tree, p, print_options);
	isl_printer_free(p);

	fprintf(cuda->kernel_c, "}\n");
}

struct print_host_user_data {
	struct cuda_info *cuda;
	struct gpu_prog *prog;
};

/* Print the user statement of the host code to "p".
 *
 * In particular, print a block of statements that defines the grid
 * and the block and then launches the kernel.
 */
static __isl_give isl_printer *print_host_user(__isl_take isl_printer *p,
	__isl_take isl_ast_print_options *print_options,
	__isl_keep isl_ast_node *node, void *user)
{
	isl_id *id;
	struct ppcg_kernel *kernel;
	struct print_host_user_data *data;

	id = isl_ast_node_get_annotation(node);
	kernel = isl_id_get_user(id);
	isl_id_free(id);

	data = (struct print_host_user_data *) user;

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "{");
	p = isl_printer_end_line(p);
	p = isl_printer_indent(p, 2);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "dim3 k");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p, "_dimBlock");
	print_reverse_list(isl_printer_get_file(p),
				kernel->n_block, kernel->block_dim);
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

	p = print_grid(p, kernel);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "kernel");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p, " <<<k");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p, "_dimGrid, k");
	p = isl_printer_print_int(p, kernel->id);
	p = isl_printer_print_str(p, "_dimBlock>>> (");
	p = print_kernel_arguments(p, data->prog, kernel, 0);
	p = isl_printer_print_str(p, ");");
	p = isl_printer_end_line(p);

	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "cudaCheckKernel();");
	p = isl_printer_end_line(p);

	p = isl_printer_indent(p, -2);
	p = isl_printer_start_line(p);
	p = isl_printer_print_str(p, "}");
	p = isl_printer_end_line(p);

	p = isl_printer_start_line(p);
	p = isl_printer_end_line(p);

	print_kernel(data->prog, kernel, data->cuda);

	isl_ast_print_options_free(print_options);

	return p;
}

static __isl_give isl_printer *print_host_code(__isl_take isl_printer *p,
	struct gpu_prog *prog, __isl_keep isl_ast_node *tree,
	struct cuda_info *cuda)
{
	isl_ast_print_options *print_options;
	isl_ctx *ctx = isl_ast_node_get_ctx(tree);
	struct print_host_user_data data = { cuda, prog };

	print_options = isl_ast_print_options_alloc(ctx);
	print_options = isl_ast_print_options_set_print_user(print_options,
						&print_host_user, &data);

	p = gpu_print_macros(p, tree);
	p = isl_ast_node_print(tree, p, print_options);

	return p;
}

/* For each array that needs to be copied out (based on prog->copy_out),
 * copy the contents back from the GPU to the host.
 *
 * If any element of a given array appears in prog->copy_out, then its
 * entire extent is in prog->copy_out.  The bounds on this extent have
 * been precomputed in extract_array_info and are used in
 * gpu_array_info_print_size.
 */
static __isl_give isl_printer *copy_arrays_from_device(
	__isl_take isl_printer *p, struct gpu_prog *prog)
{
	int i;
	isl_union_set *copy_out;
	copy_out = isl_union_set_copy(prog->copy_out);

	for (i = 0; i < prog->n_array; ++i) {
		isl_space *dim;
		isl_set *copy_out_i;
		int empty;

		dim = isl_space_copy(prog->array[i].space);
		copy_out_i = isl_union_set_extract_set(copy_out, dim);
		empty = isl_set_fast_is_empty(copy_out_i);
		isl_set_free(copy_out_i);
		if (empty)
			continue;

		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "cudaCheckReturn(cudaMemcpy(");
		if (gpu_array_is_scalar(&prog->array[i]))
			p = isl_printer_print_str(p, "&");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");
		p = gpu_array_info_print_size(p, &prog->array[i]);
		p = isl_printer_print_str(p, ", cudaMemcpyDeviceToHost));");
		p = isl_printer_end_line(p);
	}

	isl_union_set_free(copy_out);
	p = isl_printer_start_line(p);
	p = isl_printer_end_line(p);
	return p;
}

static __isl_give isl_printer *free_device_arrays(__isl_take isl_printer *p,
	struct gpu_prog *prog)
{
	int i;

	for (i = 0; i < prog->n_array; ++i) {
		if (gpu_array_is_read_only_scalar(&prog->array[i]))
			continue;
		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, "cudaCheckReturn(cudaFree(dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, "));");
		p = isl_printer_end_line(p);
	}

	return p;
}

/* Given a gpu_prog "prog" and the corresponding transformed AST
 * "tree", print the entire CUDA code to "p".
 */
static __isl_give isl_printer *print_cuda(__isl_take isl_printer *p,
	struct gpu_prog *prog, __isl_keep isl_ast_node *tree,
	void *user)
{
	struct cuda_info *cuda = user;

	p = ppcg_start_block(p);

	p = print_cuda_macros(p);

	p = declare_device_arrays(p, prog);
	p = allocate_device_arrays(p, prog);
	p = copy_arrays_to_device(p, prog);

	p = print_host_code(p, prog, tree, cuda);

	p = copy_arrays_from_device(p, prog);
	p = free_device_arrays(p, prog);

	p = ppcg_end_block(p);

	return p;
}

/* Transform the code in the file called "input" by replacing
 * all scops by corresponding CUDA code.
 * The names of the output files are derived from "input".
 *
 * We let generate_gpu do all the hard work and then let it call
 * us back for printing the AST in print_cuda.
 *
 * To prepare for this printing, we first open the output files
 * and we close them after generate_gpu has finished.
 */
int generate_cuda(isl_ctx *ctx, struct ppcg_options *options,
	const char *input)
{
	struct cuda_info cuda;
	int r;

	cuda_open_files(&cuda, input);

	r = generate_gpu(ctx, input, cuda.host_c, options, &print_cuda, &cuda);

	cuda_close_files(&cuda);

	return r;
}
