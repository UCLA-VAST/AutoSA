/*
 * Copyright 2012      Ecole Normale Superieure
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <isl/aff.h>
#include <isl/ast.h>

#include "cuda_common.h"
#include "cuda.h"
#include "gpu.h"
#include "pet_printer.h"
#include "print.h"
#include "schedule.h"

static __isl_give isl_printer *print_cuda_macros(__isl_take isl_printer *p)
{
	const char *macros =
		"#define cudaCheckReturn(ret) assert((ret) == cudaSuccess)\n"
		"#define cudaCheckKernel()"
		" assert(cudaGetLastError() == cudaSuccess)\n\n";
	p = isl_printer_print_str(p, macros);
	return p;
}

static __isl_give isl_printer *print_array_size(__isl_take isl_printer *prn,
	struct gpu_array_info *array)
{
	int i;

	for (i = 0; i < array->n_index; ++i) {
		prn = isl_printer_print_str(prn, "(");
		prn = isl_printer_print_pw_aff(prn, array->bound[i]);
		prn = isl_printer_print_str(prn, ") * ");
	}
	prn = isl_printer_print_str(prn, "sizeof(");
	prn = isl_printer_print_str(prn, array->type);
	prn = isl_printer_print_str(prn, ")");

	return prn;
}

static __isl_give isl_printer *declare_device_arrays(__isl_take isl_printer *p,
	struct gpu_prog *prog)
{
	int i;

	for (i = 0; i < prog->n_array; ++i) {
		if (gpu_array_is_read_only_scalar(&prog->array[i]))
			continue;
		p = isl_printer_start_line(p);
		p = isl_printer_print_str(p, prog->array[i].type);
		p = isl_printer_print_str(p, " *dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ";");
		p = isl_printer_end_line(p);
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
		p = print_array_size(p, &prog->array[i]);
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

		dim = isl_space_copy(prog->array[i].dim);
		read_i = isl_union_set_extract_set(prog->copy_in, dim);
		empty = isl_set_fast_is_empty(read_i);
		isl_set_free(read_i);
		if (empty)
			continue;

		p = isl_printer_print_str(p, "cudaCheckReturn(cudaMemcpy(dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");

		if (gpu_array_is_scalar(&prog->array[i]))
			p = isl_printer_print_str(p, "&");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");

		p = print_array_size(p, &prog->array[i]);
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

		space = isl_space_copy(prog->array[i].dim);
		arr = isl_union_set_extract_set(kernel->arrays, space);
		empty = isl_set_fast_is_empty(arr);
		isl_set_free(arr);
		if (empty)
			continue;

		if (!first)
			p = isl_printer_print_str(p, ", ");

		if (types) {
			p = isl_printer_print_str(p, prog->array[i].type);
			p = isl_printer_print_str(p, " ");
		}

		if (gpu_array_is_read_only_scalar(&prog->array[i])) {
			p = isl_printer_print_str(p, prog->array[i].name);
		} else {
			if (types)
				p = isl_printer_print_str(p, "*");
			else
				p = isl_printer_print_str(p, "dev_");
			p = isl_printer_print_str(p, prog->array[i].name);
		}

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
	int i;
	const char *block_dims[] = { "blockIdx.x", "blockIdx.y" };
	const char *thread_dims[] = { "threadIdx.x", "threadIdx.y",
					"threadIdx.z" };

	if (kernel->n_grid > 0) {
		print_indent(out, 4);
		fprintf(out, "int ");
		for (i = 0; i < kernel->n_grid; ++i) {
			if (i)
				fprintf(out, ", ");
			fprintf(out, "b%d = %s",
				i, block_dims[kernel->n_grid - 1 - i]);
		}
		fprintf(out, ";\n");
	}

	if (kernel->n_block > 0) {
		print_indent(out, 4);
		fprintf(out, "int ");
		for (i = 0; i < kernel->n_block; ++i) {
			if (i)
				fprintf(out, ", ");
			fprintf(out, "t%d = %s",
				i, thread_dims[kernel->n_block - 1 - i]);
		}
		fprintf(out, ";\n");
	}
}

static void print_kernel_var(FILE *out, struct ppcg_kernel_var *var)
{
	int j;
	isl_int v;

	print_indent(out, 4);
	if (var->type == ppcg_access_shared)
		fprintf(out, "__shared__ ");
	fprintf(out, "%s %s", var->array->type, var->name);
	isl_int_init(v);
	for (j = 0; j < var->array->n_index; ++j) {
		fprintf(out, "[");
		isl_vec_get_element(var->size, j, &v);
		isl_int_print(out, v, 0);
		fprintf(out, "]");
	}
	isl_int_clear(v);
	fprintf(out, ";\n");
}

static void print_kernel_vars(FILE *out, struct ppcg_kernel *kernel)
{
	int i;

	for (i = 0; i < kernel->n_var; ++i)
		print_kernel_var(out, &kernel->var[i]);
}

/* Print an access to the element in the private/shared memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->local_index as a "call" to the array.
 */
static __isl_give isl_printer *stmt_print_local_index(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	int i;
	isl_ast_expr *expr;
	struct gpu_array_info *array = stmt->u.c.array;

	expr = isl_ast_expr_get_op_arg(stmt->u.c.local_index, 0);
	p = isl_printer_print_ast_expr(p, expr);
	isl_ast_expr_free(expr);

	for (i = 0; i < array->n_index; ++i) {
		expr = isl_ast_expr_get_op_arg(stmt->u.c.local_index, 1 + i);

		p = isl_printer_print_str(p, "[");
		p = isl_printer_print_ast_expr(p, expr);
		p = isl_printer_print_str(p, "]");

		isl_ast_expr_free(expr);
	}

	return p;
}

/* Print an access to the element in the global memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->index as a "call" to the array.
 *
 * The copy in global memory has been linearized, so we need to take
 * the array size into account.
 */
static __isl_give isl_printer *stmt_print_global_index(
	__isl_take isl_printer *p, struct ppcg_kernel_stmt *stmt)
{
	int i;
	struct gpu_array_info *array = stmt->u.c.array;
	isl_pw_aff_list *bound = stmt->u.c.local_array->bound;

	if (gpu_array_is_scalar(array)) {
		if (!array->read_only)
			p = isl_printer_print_str(p, "*");
		p = isl_printer_print_str(p, array->name);
		return p;
	}

	p = isl_printer_print_str(p, array->name);
	p = isl_printer_print_str(p, "[");
	for (i = 0; i + 1 < array->n_index; ++i)
		p = isl_printer_print_str(p, "(");
	for (i = 0; i < array->n_index; ++i) {
		isl_ast_expr *expr;
		expr = isl_ast_expr_get_op_arg(stmt->u.c.index, 1 + i);
		if (i) {
			isl_pw_aff *bound_i;
			bound_i = isl_pw_aff_list_get_pw_aff(bound, i);
			p = isl_printer_print_str(p, ") * (");
			p = isl_printer_print_pw_aff(p, bound_i);
			p = isl_printer_print_str(p, ") + ");
			isl_pw_aff_free(bound_i);
		}
		p = isl_printer_print_ast_expr(p, expr);
		isl_ast_expr_free(expr);
	}
	p = isl_printer_print_str(p, "]");

	return p;
}

/* Print a copy statement.
 *
 * A read copy statement is printed as
 *
 *	local = global;
 *
 * while a write copy statement is printed as
 *
 *	global = local;
 */
static __isl_give isl_printer *print_copy(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	p = isl_printer_start_line(p);
	if (stmt->u.c.read) {
		p = stmt_print_local_index(p, stmt);
		p = isl_printer_print_str(p, " = ");
		p = stmt_print_global_index(p, stmt);
	} else {
		p = stmt_print_global_index(p, stmt);
		p = isl_printer_print_str(p, " = ");
		p = stmt_print_local_index(p, stmt);
	}
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);

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

/* Print an access based on the information in "access".
 * If this an access to global memory, then the index expression
 * is linearized.
 *
 * If access->array is NULL, then we are
 * accessing an iterator in the original program.
 */
static __isl_give isl_printer *print_access(__isl_take isl_printer *p,
	struct ppcg_kernel_access *access)
{
	int i;
	unsigned n_index;
	struct gpu_array_info *array;
	isl_pw_aff_list *bound;

	array = access->array;
	bound = array ? access->local_array->bound : NULL;
	if (!array)
		p = isl_printer_print_str(p, "(");
	else {
		if (access->type == ppcg_access_global &&
		    gpu_array_is_scalar(array) && !array->read_only)
			p = isl_printer_print_str(p, "*");
		p = isl_printer_print_str(p, access->local_name);
		if (gpu_array_is_scalar(array))
			return p;
		p = isl_printer_print_str(p, "[");
	}

	n_index = isl_ast_expr_list_n_ast_expr(access->index);
	if (access->type == ppcg_access_global)
		for (i = 0; i + 1 < n_index; ++i)
			p = isl_printer_print_str(p, "(");

	for (i = 0; i < n_index; ++i) {
		isl_ast_expr *index;

		index = isl_ast_expr_list_get_ast_expr(access->index, i);
		if (array && i) {
			if (access->type == ppcg_access_global) {
				isl_pw_aff *bound_i;
				bound_i = isl_pw_aff_list_get_pw_aff(bound, i);
				p = isl_printer_print_str(p, ") * (");
				p = isl_printer_print_pw_aff(p, bound_i);
				p = isl_printer_print_str(p, ") + ");
				isl_pw_aff_free(bound_i);
			} else
				p = isl_printer_print_str(p, "][");
		}
		p = isl_printer_print_ast_expr(p, index);
		isl_ast_expr_free(index);
	}
	if (!array)
		p = isl_printer_print_str(p, ")");
	else
		p = isl_printer_print_str(p, "]");

	return p;
}

struct cuda_access_print_info {
	int i;
	struct ppcg_kernel_stmt *stmt;
};

/* To print the cuda accesses we walk the list of cuda accesses simultaneously
 * with the pet printer. This means that whenever the pet printer prints a
 * pet access expression we have the corresponding cuda access available and can
 * print the modified access.
 */
static __isl_give isl_printer *print_cuda_access(__isl_take isl_printer *p,
	struct pet_expr *expr, void *usr)
{
	struct cuda_access_print_info *info =
		(struct cuda_access_print_info *) usr;

	p = print_access(p, &info->stmt->u.d.access[info->i]);
	info->i++;

	return p;
}

static __isl_give isl_printer *print_stmt_body(__isl_take isl_printer *p,
	struct ppcg_kernel_stmt *stmt)
{
	struct cuda_access_print_info info;

	info.i = 0;
	info.stmt = stmt;

	p = isl_printer_start_line(p);
	p = print_pet_expr(p, stmt->u.d.stmt->body, &print_cuda_access, &info);
	p = isl_printer_print_str(p, ";");
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
		return print_copy(p, stmt);
	case ppcg_kernel_sync:
		return print_sync(p, stmt);
	case ppcg_kernel_domain:
		return print_stmt_body(p, stmt);
	}

	return p;
}

static int print_macro(enum isl_ast_op_type type, void *user)
{
	isl_printer **p = user;

	if (type == isl_ast_op_fdiv_q)
		return 0;

	*p = isl_ast_op_type_print_macro(type, *p);

	return 0;
}

/* Print the required macros for "node", including one for floord.
 * We always print a macro for floord as it may also appear in the statements.
 */
static __isl_give isl_printer *print_macros(
	__isl_keep isl_ast_node *node, __isl_take isl_printer *p)
{
	p = isl_ast_op_type_print_macro(isl_ast_op_fdiv_q, p);
	if (isl_ast_node_foreach_ast_op_type(node, &print_macro, &p) < 0)
		return isl_printer_free(p);
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
	print_kernel_vars(cuda->kernel_c, kernel);
	fprintf(cuda->kernel_c, "\n");

	print_options = isl_ast_print_options_alloc(ctx);
	print_options = isl_ast_print_options_set_print_user(print_options,
						    &print_kernel_stmt, NULL);

	p = isl_printer_to_file(ctx, cuda->kernel_c);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = isl_printer_indent(p, 4);
	p = print_macros(kernel->tree, p);
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

	p = print_macros(tree, p);
	p = isl_ast_node_print(tree, p, print_options);

	return p;
}

/* For each array that is written anywhere in the gpu_prog,
 * copy the contents back from the GPU to the host.
 *
 * Arrays that are not visible outside the corresponding scop
 * do not need to be copied back.
 */
static __isl_give isl_printer *copy_arrays_from_device(
	__isl_take isl_printer *p, struct gpu_prog *prog)
{
	int i;
	isl_union_set *write;
	write = isl_union_map_range(isl_union_map_copy(prog->write));

	for (i = 0; i < prog->n_array; ++i) {
		isl_space *dim;
		isl_set *write_i;
		int empty;

		if (prog->array[i].local)
			continue;

		dim = isl_space_copy(prog->array[i].dim);
		write_i = isl_union_set_extract_set(write, dim);
		empty = isl_set_fast_is_empty(write_i);
		isl_set_free(write_i);
		if (empty)
			continue;

		p = isl_printer_print_str(p, "cudaCheckReturn(cudaMemcpy(");
		if (gpu_array_is_scalar(&prog->array[i]))
			p = isl_printer_print_str(p, "&");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, ", ");
		p = print_array_size(p, &prog->array[i]);
		p = isl_printer_print_str(p, ", cudaMemcpyDeviceToHost));");
		p = isl_printer_end_line(p);
	}

	isl_union_set_free(write);
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
		p = isl_printer_print_str(p, "cudaCheckReturn(cudaFree(dev_");
		p = isl_printer_print_str(p, prog->array[i].name);
		p = isl_printer_print_str(p, "));");
		p = isl_printer_end_line(p);
	}

	return p;
}

int generate_cuda(isl_ctx *ctx, struct ppcg_scop *scop,
	struct ppcg_options *options, const char *input)
{
	struct cuda_info cuda;
	struct gpu_prog *prog;
	isl_ast_node *tree;
	isl_printer *p;

	if (!scop)
		return -1;

	scop->context = add_context_from_str(scop->context, options->ctx);

	prog = gpu_prog_alloc(ctx, scop);

	tree = generate_gpu(ctx, prog, options);

	cuda_open_files(&cuda, input);

	p = isl_printer_to_file(ctx, cuda.host_c);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
	p = ppcg_print_exposed_declarations(p, scop);
	p = ppcg_start_block(p);

	p = print_cuda_macros(p);

	p = declare_device_arrays(p, prog);
	p = allocate_device_arrays(p, prog);
	p = copy_arrays_to_device(p, prog);

	p = print_host_code(p, prog, tree, &cuda);
	isl_ast_node_free(tree);

	p = copy_arrays_from_device(p, prog);
	p = free_device_arrays(p, prog);

	p = ppcg_end_block(p);
	isl_printer_free(p);

	cuda_close_files(&cuda);

	gpu_prog_free(prog);

	return 0;
}
