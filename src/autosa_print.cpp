/* Helper functions in codegen */
#include <assert.h>
#include <cmath>

#include "autosa_print.h"
#include "autosa_utils.h"
#include "autosa_comm.h"
#include "print.h"

const char *vector_index[] = {"0", "1", "2", "3", "4", "5", "6", "7",
                              "8", "9", "a", "b", "c", "d", "e", "f"};

enum IO_TRANS_DIR {GLOBAL_BUF, LOCAL_BUF, FIFO};

/* Print the call of an array argument.
 */
__isl_give isl_printer *autosa_array_info_print_call_argument(
  __isl_take isl_printer *p, struct autosa_array_info *array, int n_ref, const char *prefix)
{
  if (autosa_array_is_read_only_scalar(array))
    return isl_printer_print_str(p, array->name);

  if (strlen(prefix) > 0) {
    p = isl_printer_print_str(p, prefix);
    p = isl_printer_print_str(p, "_");
  }  
  p = isl_printer_print_str(p, array->name);
  if (n_ref >= 0)
  {
    //std::pair<int, int> ref_port_map = array->local_array->group_ref_mem_port_map[n_ref];
    auto ref_port_map = array->local_array->group_ref_mem_port_map.at(n_ref);
    p = isl_printer_print_str(p, "[");
    p = isl_printer_print_int(p, ref_port_map.second);
    p = isl_printer_print_str(p, "]");
  }

  return p;
}

/* Print the array group name prefix.
 * [array_name]_[group_id](optional)_[drain](optional)
 */
__isl_give isl_printer *autosa_array_ref_group_print_prefix(
    struct autosa_array_ref_group *group, __isl_take isl_printer *p)
{
  p = isl_printer_print_str(p, group->array->name);
  if (group->group_type == AUTOSA_DRAIN_GROUP)
  {
    p = isl_printer_print_str(p, "_drain");
  }
  else
  {
    if (group->group_type == AUTOSA_IO_GROUP && group->local_array->n_io_group > 1)
    {
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_int(p, group->nr);
    }
    else if (group->group_type == AUTOSA_PE_GROUP && group->local_array->n_pe_group > 1)
    {
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_int(p, group->nr);
    }
  }

  return p;
}

/* Print the name of the local copy of a given group of array references.
 */
__isl_give isl_printer *autosa_array_ref_group_print_fifo_name(
    struct autosa_array_ref_group *group, __isl_take isl_printer *p)
{
  int global = 0;
  enum autosa_group_access_type type;

  if (group->group_type == AUTOSA_PE_GROUP)
    return p;
  
  p = isl_printer_print_str(p, "fifo_");
  p = isl_printer_print_str(p, group->array->name);
  if (group->group_type == AUTOSA_IO_GROUP) {
    if (group->local_array->n_io_group > 1)
    {
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_int(p, group->nr);
    }
  } else if (group->group_type == AUTOSA_DRAIN_GROUP)
  {
    p = isl_printer_print_str(p, "_drain");
  }

  return p;
}

/* Was the definition of "type" printed before?
 * That is, does its name appear in the list of printed types "types"?
 */
static int already_printed(struct autosa_types *types,
                           struct pet_type *type)
{
  int i;

  for (i = 0; i < types->n; ++i)
    if (!strcmp(types->name[i], type->name))
      return 1;

  return 0;
}

/* Print the definitions of all types prog->scop that have not been
 * printed before (according to "types") on "p".
 * Extend the list of printed types "types" with the newly printed types.
 */
__isl_give isl_printer *autosa_print_types(__isl_take isl_printer *p,
                                           struct autosa_types *types, struct autosa_prog *prog)
{
  int i, n;
  isl_ctx *ctx;
  char **name;

  n = prog->scop->pet->n_type;

  if (n == 0)
    return p;

  ctx = isl_printer_get_ctx(p);
  name = isl_realloc_array(ctx, types->name, char *, types->n + n);
  if (!name)
    return isl_printer_free(p);
  types->name = name;

  for (i = 0; i < n; ++i)
  {
    struct pet_type *type = prog->scop->pet->types[i];

    if (already_printed(types, type))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, type->definition);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);

    types->name[types->n++] = strdup(type->name);
  }

  return p;
}

/* Print declarations to "p" for arrays that are local to "prog"
 * but that are used on the host and therefore require a declaration.
 */
__isl_give isl_printer *autosa_print_local_declarations(
    __isl_take isl_printer *p, struct autosa_prog *prog)
{
  int i;

  if (!prog)
    return isl_printer_free(p);

  for (i = 0; i < prog->n_array; ++i)
  {
    struct autosa_array_info *array = &prog->array[i];
    isl_ast_expr *size;

    if (!array->declare_local)
      continue;
    size = array->declared_size;
    p = ppcg_print_declaration_with_size(p, array->type, size);
  }

  return p;
}

__isl_give isl_printer *print_str_new_line(__isl_take isl_printer *p, const char *str)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, str);
  p = isl_printer_end_line(p);

  return p;
}

/* Print an expression for the size of "array" in data items.
 */
__isl_give isl_printer *autosa_array_info_print_data_size(
    __isl_take isl_printer *p, struct autosa_array_info *array)
{
  int i;
  int first = 1;

  for (i = 0; i < array->n_index; ++i)
  {
    if (!first)
      p = isl_printer_print_str(p, " * ");

    isl_ast_expr *bound;

    p = isl_printer_print_str(p, "(");
    bound = isl_ast_expr_get_op_arg(array->bound_expr, 1 + i);
    p = isl_printer_print_ast_expr(p, bound);
    isl_ast_expr_free(bound);
    p = isl_printer_print_str(p, ")");
    first = 0;
  }

  if (array->local_array->is_sparse) {
    p = isl_printer_print_str(p, " / ");
    p = isl_printer_print_double(p, (double)array->local_array->eff_compress_ratio);
  }

  return p;
}

/* Print an expression for the size of "array" in bytes.
 */
__isl_give isl_printer *autosa_array_info_print_size(
    __isl_take isl_printer *p, struct autosa_array_info *array)
{
  int i;

  for (i = 0; i < array->n_index; ++i)
  {
    isl_ast_expr *bound;

    p = isl_printer_print_str(p, "(");
    bound = isl_ast_expr_get_op_arg(array->bound_expr, 1 + i);
    p = isl_printer_print_ast_expr(p, bound);
    isl_ast_expr_free(bound);
    p = isl_printer_print_str(p, ") * ");
  }
  p = isl_printer_print_str(p, "sizeof(");
  p = isl_printer_print_str(p, array->type);
  p = isl_printer_print_str(p, ")");

  return p;
}

/* Print an expression for the size of "array" in bytes.
 */
__isl_give isl_printer *autosa_array_info_print_serialize_data_size(
    __isl_take isl_printer *p, struct autosa_array_info *array)
{
  //p = isl_printer_print_str(p, "(");
  p = isl_printer_print_pw_qpolynomial(p, array->local_array->serialize_bound);
  if (array->local_array->is_sparse) {
    p = isl_printer_print_str(p, " / ");
    p = isl_printer_print_double(p, (double)array->local_array->eff_compress_ratio);
  }
  //p = isl_printer_print_str(p, ") * ");
  //p = isl_printer_print_str(p, "sizeof(");
  //p = isl_printer_print_str(p, array->type);
  //p = isl_printer_print_str(p, ")");

  return p;
}

/* Print an expression for the size of "array" in bytes.
 */
__isl_give isl_printer *autosa_array_info_print_serialize_size(
    __isl_take isl_printer *p, struct autosa_array_info *array)
{
  p = isl_printer_print_str(p, "(");
  p = isl_printer_print_pw_qpolynomial(p, array->local_array->serialize_bound);
  if (array->local_array->is_sparse) {
    p = isl_printer_print_str(p, " / ");
    p = isl_printer_print_double(p, (double)array->local_array->eff_compress_ratio);
  }
  p = isl_printer_print_str(p, ") * ");
  p = isl_printer_print_str(p, "sizeof(");
  p = isl_printer_print_str(p, array->type);
  p = isl_printer_print_str(p, ")");

  return p;
}

__isl_give isl_printer *autosa_print_array_type(__isl_take isl_printer *p,
                                                struct autosa_array_info *array)
{
  int n_lane = array->n_lane;
  if (n_lane == 1)
    p = isl_printer_print_str(p, array->type);
  else
  {
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, n_lane);
  }

  return p;
}

__isl_give isl_printer *autosa_print_array_type_with_lane(
  __isl_take isl_printer *p,
  struct autosa_array_info *array, int n_lane)
{
  //if (n_lane == 1)
  //  p = isl_printer_print_str(p, array->type);
  //else {
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, n_lane);
  //}
  return p;
}

__isl_give isl_printer *autosa_print_array_type_with_lane_sparse(
  __isl_take isl_printer *p,
  struct autosa_array_info *array, int n_lane)
{
  p = isl_printer_print_str(p, array->name);
  p = isl_printer_print_str(p, "_s_t");
  p = isl_printer_print_int(p, n_lane);

  return p;
}

__isl_give isl_printer *autosa_kernel_print_domain(__isl_take isl_printer *p,
                                                   struct autosa_kernel_stmt *stmt)
{
  return pet_stmt_print_body(stmt->u.d.stmt->stmt, p, stmt->u.d.ref2expr);
}

/* Print the declaration of a non-linearized array argument.
 */
static __isl_give isl_printer *print_non_linearized_declaration_argument(
    __isl_take isl_printer *p, struct autosa_array_info *array, int n_lane)
{
  if (n_lane == 1)
  {
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, " ");

    p = isl_printer_print_ast_expr(p, array->bound_expr);
  }
  else
  {
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, n_lane);
    p = isl_printer_print_str(p, " ");

    p = isl_printer_print_ast_expr(p, array->bound_expr);
  }

  return p;
}

/* Print the declaration of an array argument.
 * "memory_space" allows to specify a memory space prefix.
 */
__isl_give isl_printer *autosa_array_info_print_declaration_argument(
    __isl_take isl_printer *p, struct autosa_array_info *array, int n_lane,
    const char *memory_space, int n_ref, char *mem_port_map, enum platform target)
{
  int mem_port = -1;
  if (mem_port_map) {
    /* This is only for Intel HBM. We will assign the different array to different HBM channel. */
    isl_union_map *umap;

    umap = extract_sizes_from_str(isl_printer_get_ctx(p), mem_port_map);
    mem_port = read_mem_port_map(umap, array->name);
    isl_union_map_free(umap);
  }

  if (autosa_array_is_read_only_scalar(array))
  {
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, " ");
    p = isl_printer_print_str(p, array->name);
    return p;
  }

  if (memory_space)
  {
    p = isl_printer_print_str(p, memory_space);
    p = isl_printer_print_str(p, " ");
  }
  if (mem_port != -1) {
    p = isl_printer_print_str(p, "__attribute__((buffer_location(\"HBM");
    p = isl_printer_print_int(p, mem_port);
    p = isl_printer_print_str(p, "\"))) ");
  }

  if (array->n_index != 0 && !array->linearize)
    return print_non_linearized_declaration_argument(p, array, n_lane);

  //if (n_lane == 1)
  //  p = isl_printer_print_str(p, array->type);
  //else
  //{
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, n_lane);
  //}
  p = isl_printer_print_str(p, " ");
  if (target != CATAPULT_HW)
    p = isl_printer_print_str(p, "*");
  if (target == INTEL_HW)
    p = isl_printer_print_str(p, "restrict ");
  p = isl_printer_print_str(p, array->name);
  if (n_ref >= 0)
  {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_int(p, n_ref);
  }
  if (target == CATAPULT_HW) {
    if (array->local_array->host_serialize) {
      p = isl_printer_print_str(p, "[");
      p = isl_printer_print_pw_qpolynomial(p, array->local_array->serialize_bound);
      p = isl_printer_print_str(p, " / ");      
      p = isl_printer_print_int(p, n_lane);
      p = isl_printer_print_str(p, "]");      
    } else {
      throw std::runtime_error("[AutoSA] Error: Non-serialized array not supported for Catapult HLS yet.");
    }
  }

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
__isl_give isl_printer *print_kernel_arguments(__isl_take isl_printer *p,
                                               struct autosa_prog *prog, 
                                               struct autosa_kernel *kernel,
                                               int types, struct hls_info *hls)
{
  int i, n;
  int first = 1;
  unsigned nparam;
  isl_space *space;
  const char *type;

  /* Arrays */
  for (i = 0; i < kernel->n_array; ++i)
  {
    int required;
    int n_lane;

    required = autosa_kernel_requires_array_argument(kernel, i);
    if (required < 0)
      return isl_printer_free(p);
    if (!required)
      continue;

    struct autosa_local_array_info *local_array = &kernel->array[i];
    n_lane = local_array->n_lane;
    if (hls->target == INTEL_HW || hls->target == CATAPULT_HW ||
        (hls->target == XILINX_HW && local_array->n_io_group_refs == 1))
    {
      if (!first)
        p = isl_printer_print_str(p, ", ");

      if (types) {
        if (prog->scop->options->autosa->axi_stream) {
          p = autosa_fifo_print_declaration_arguments(p, local_array->io_groups[0], n_lane, NULL, hls->target);
        } else {
          p = autosa_array_info_print_declaration_argument(
                p, local_array->array, n_lane, NULL, -1, NULL, hls->target);
        }        
      } else {                
        if (prog->scop->options->autosa->axi_stream) {
          p = autosa_array_info_print_call_argument(p,
                                                    local_array->array, -1, "fifo");
        } else {
          p = autosa_array_info_print_call_argument(p,
                                                    local_array->array, 0, "buffer");
        }
      }

      first = 0;
    }
    else
    {
      for (int j = 0; j < local_array->n_io_group_refs; j++)
      {
        if (!first)
          p = isl_printer_print_str(p, ", ");

        if (types)
          p = autosa_array_info_print_declaration_argument(
                p, local_array->array, n_lane, NULL, j, NULL, hls->target);
        else
        {
          p = autosa_array_info_print_call_argument(p,
                                                    local_array->array, j, "buffer");
        }

        first = 0;
      }
    }
  }

  /* Parameters */
  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (i = 0; i < nparam; ++i)
  {
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

  /* Host loop iterators */
  n = isl_space_dim(kernel->space, isl_dim_set);
  type = isl_options_get_ast_iterator_type(prog->ctx);
  for (i = 0; i < n; ++i)
  {
    const char *name;

    if (!first)
      p = isl_printer_print_str(p, ", ");
    name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
    if (types)
    {
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
__isl_give isl_printer *print_kernel_header(
  __isl_take isl_printer *p, struct autosa_prog *prog, 
  struct autosa_kernel *kernel, struct hls_info *hls, int types)
{
  p = isl_printer_start_line(p);
  if (types)
    p = isl_printer_print_str(p, "void ");
  if (hls->hcl) 
    p = isl_printer_print_str(p, "autosa_func");
  else
    p = isl_printer_print_str(p, "kernel0");
  //p = isl_printer_print_int(p, kernel->id);
  p = isl_printer_print_str(p, "(");
  p = print_kernel_arguments(p, prog, kernel, types, hls);
  p = isl_printer_print_str(p, ")");

  return p;
}

/* This function is called for each node in a AutoSA AST.
 * In case of a user node, print the macro definitions required
 * for printing the AST expressions in the annotation, if any.
 * For other nodes, return true such that descendants are also
 * visited.
 *
 * In particular, for a kernel launch, print the macro definitions
 * needed for the grid size.
 * For a copy statement, print the macro definitions needed
 * for the two index expressions.
 * For an original user statement, print the macro definitions
 * needed for the substitutions.
 */
static isl_bool at_node(__isl_keep isl_ast_node *node, void *user)
{
  const char *name;
  isl_id *id;
  int is_kernel;
  struct autosa_kernel *kernel;
  struct autosa_kernel_stmt *stmt;
  isl_printer **p = (isl_printer **)user;

  if (isl_ast_node_get_type(node) != isl_ast_node_user)
    return isl_bool_true;

  id = isl_ast_node_get_annotation(node);
  if (!id)
    return isl_bool_false;

  name = isl_id_get_name(id);
  if (!name)
    return isl_bool_error;
  is_kernel = !strcmp(name, "kernel");
  kernel = is_kernel ? (struct autosa_kernel *)isl_id_get_user(id) : NULL;
  stmt = is_kernel ? NULL : (struct autosa_kernel_stmt *)isl_id_get_user(id);
  isl_id_free(id);

  if ((is_kernel && !kernel) || (!is_kernel && !stmt))
    return isl_bool_error;

  if (is_kernel)
  {
    *p = ppcg_ast_expr_print_macros(kernel->grid_size_expr, *p);
  }
  else if (stmt->type == AUTOSA_KERNEL_STMT_COPY)
  {
    *p = ppcg_ast_expr_print_macros(stmt->u.c.index, *p);
    *p = ppcg_ast_expr_print_macros(stmt->u.c.local_index, *p);
  }
  else if (stmt->type == AUTOSA_KERNEL_STMT_DOMAIN)
  {
    *p = ppcg_print_body_macros(*p, stmt->u.d.ref2expr);
  }
  if (!*p)
    return isl_bool_error;

  return isl_bool_false;
}

static void print_indent(FILE *dst, int indent)
{
  fprintf(dst, "%*s", indent, "");
}

/* Print a list of iterators of type "type" with names "ids" to "out".
 * Each iterator is assigned one of the instance identifiers in dims.
 */
static __isl_give isl_printer *print_iterators(
  __isl_take isl_printer *p, 
  FILE *out, const char *type,
  __isl_keep isl_id_list *ids, const char *dims[])
{
  int i, n;

  n = isl_id_list_n_id(ids);
  if (n <= 0)
    return p;
  //print_indent(out, 2);
  //fprintf(out, "%s ", type);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, type);
  p = isl_printer_print_str(p, " ");
  for (i = 0; i < n; ++i)
  {
    isl_id *id;

    if (i)
      p = isl_printer_print_str(p, ", ");
      //fprintf(out, ", ");
    id = isl_id_list_get_id(ids, i);
    //fprintf(out, "%s = %s", isl_id_get_name(id),
    //        dims[i]);
    p = isl_printer_print_str(p, isl_id_get_name(id));
    p = isl_printer_print_str(p, " = ");
    p = isl_printer_print_str(p, dims[i]);
    isl_id_free(id);
  }
  //fprintf(out, "; // module id\n");
  p = isl_printer_print_str(p, "; // module id");
  p = isl_printer_end_line(p);

  return p;
}

/* Print the required macros for the AutoSA AST "node" to "p",
 * including those needed for the user statements inside the AST.
 */
__isl_give isl_printer *autosa_print_macros(__isl_take isl_printer *p,
                                            __isl_keep isl_ast_node *node)
{
  if (isl_ast_node_foreach_descendant_top_down(node, &at_node, &p) < 0)
    return isl_printer_free(p);
  p = ppcg_print_macros(p, node);
  return p;
}

__isl_give isl_printer *print_module_iterators(
  __isl_take isl_printer *p, FILE *out, struct autosa_hw_module *module)
{
  isl_ctx *ctx;
  const char *type;
  const char *dims[] = {"idx", "idy", "idz"};

  ctx = isl_ast_node_get_ctx(module->tree);
  type = isl_options_get_ast_iterator_type(ctx);
  p = print_iterators(p, out, type, module->inst_ids, dims);

  return p;
}

__isl_give isl_printer *print_func_iterators(
  __isl_take isl_printer *p,
  FILE *out, struct autosa_drain_merge_func *func)
{
  isl_ctx *ctx;
  const char *type;
  const char *dims[] = {"idx", "idy", "idz"};

  ctx = isl_ast_node_get_ctx(func->tree);
  type = isl_options_get_ast_iterator_type(ctx);
  p = print_iterators(p, out, type, func->inst_ids, dims);
  return p;
}

__isl_give isl_printer *print_serialize_counter(
  __isl_take isl_printer *p, struct autosa_hw_module *module)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "unsigned int ");
  p = isl_printer_print_str(p, module->io_groups[0]->array->name);
  p = isl_printer_print_str(p, "_cnt = 0;");
  p = isl_printer_end_line(p);

  return p;
}

/* Print the arguments to a host serialization functioin declaration or call.
 * If "types" is set, then print a declaration (including the types of the arguments).
 * 
 * The arguments are printed in the following order:
 * - the moduler identifiers
 * - the paramters
 * - the host loop iterators
 * - the input array accessed by the module (before serialization/deserialization)
 * - the output array accessed by the module (after serialization/deserialization)
 */
__isl_give isl_printer *print_host_serialize_arguments(
  __isl_take isl_printer *p,
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group,
  struct autosa_hw_module *module,
  int types,
  int hls)
{
  int first = 1;
  int nparam;
  int n;
  isl_space *space;
  const char *type;
  struct autosa_local_array_info *local_array;

  type = isl_options_get_ast_iterator_type(kernel->ctx);
  /* module identifiers */
  const char *dims[] = {"idx", "idy", "idz"};
  n = isl_id_list_n_id(module->inst_ids);
  for (int i = 0; i < n; ++i)
  {
    if (!first)
      p = isl_printer_print_str(p, ", ");
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, dims[i]);

    first = 0;
  }

  /* params */
  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (int i = 0; i < nparam; ++i)
  {
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

  /* Host iters */
  n = isl_space_dim(kernel->space, isl_dim_set);
  for (int i = 0; i < n; ++i)
  {
    const char *name;

    if (!first)
      p = isl_printer_print_str(p, ", ");
    name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, name);

    first = 0;
  }

  /* Arrays */
  local_array = group->local_array;
  if (!first)
    p = isl_printer_print_str(p, ", ");
  if (types)    
  {
    if (hls)
    {
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *");
    }
    else 
    {
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">> &");
    }
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_to");
  }
  else 
  {    
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, local_array->array->name);
    if (!module->in) {
      p = isl_printer_print_str(p, "_unserialized");
    }    
  }
  first = 0;

  if (!first)
    p = isl_printer_print_str(p, ", ");
  if (types)
  {
    if (hls)
    {
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *");
    }
    else
    {
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">> &");
    }
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_from");
  }
  else
  {    
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, local_array->array->name);
    if (module->in) {
      p = isl_printer_print_str(p, "_unserialized");
    }    
  }
  first = 0;

  return p;  
}

/* Print out
 * "hls::stream<[type]>"
 */
__isl_give isl_printer *print_fifo_type_xilinx(__isl_take isl_printer *p,
                                               struct autosa_array_ref_group *group, int n_lane)
{
  struct autosa_array_info *array = group->array;

  p = isl_printer_print_str(p, "hls::stream<");
  if (group->local_array->is_sparse) {
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "_s_t");
    p = isl_printer_print_int(p, n_lane);
  } else {
    if (n_lane == 1) {
      p = isl_printer_print_str(p, group->array->type);
    } else {    
      p = isl_printer_print_str(p, array->name);    
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, n_lane);
    }
  }
  p = isl_printer_print_str(p, ">");

  return p;
}

/* Print out
 * "ac_channel<[type]>"
 */
__isl_give isl_printer *print_fifo_type_catapult(__isl_take isl_printer *p,
                                                 struct autosa_array_ref_group *group, int n_lane)
{
  struct autosa_array_info *array = group->array;

  p = isl_printer_print_str(p, "ac_channel<");
  if (group->local_array->is_sparse) {
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "_s_t");
    p = isl_printer_print_int(p, n_lane);
  } else {
    //if (n_lane == 1) {
    //  p = isl_printer_print_str(p, group->array->type);
    //} else {    
      p = isl_printer_print_str(p, array->name);    
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, n_lane);
    //}
  }
  p = isl_printer_print_str(p, ">");

  return p;
}

/* Print out
 * "channel [type]"
 */
__isl_give isl_printer *print_fifo_type_intel(__isl_take isl_printer *p,
                                              struct autosa_array_ref_group *group, int n_lane)
{
  p = isl_printer_print_str(p, "channel ");
  if (n_lane == 1)
    p = isl_printer_print_str(p, group->array->type);
  else
  {
    p = isl_printer_print_str(p, group->array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, n_lane);
  }

  return p;
}

/* If disable prefix is asserted, do not print "fifo" prefix. 
 */
__isl_give isl_printer *autosa_fifo_print_declaration_arguments(
    __isl_take isl_printer *p, struct autosa_array_ref_group *group, int n_lane,
    const char *suffix, enum platform target)
{
  if (target == XILINX_HW)
  {
    p = print_fifo_type_xilinx(p, group, n_lane);
    p = isl_printer_print_str(p, " &");
  } else if (target == INTEL_HW)
  {
    p = print_fifo_type_intel(p, group, n_lane);
    p = isl_printer_print_str(p, " ");
  } else if (target == CATAPULT_HW) 
  {
    p = print_fifo_type_catapult(p, group, n_lane);
    p = isl_printer_print_str(p, " &");
  }
  p = autosa_array_ref_group_print_fifo_name(group, p);
  if (suffix)
  {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, suffix);
  }

  return p;
}

__isl_give isl_printer *autosa_fifo_print_call_argument(
    __isl_take isl_printer *p, struct autosa_array_ref_group *group,
    const char *suffix, enum platform target)
{
  p = autosa_array_ref_group_print_fifo_name(group, p);
  if (suffix)
  {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, suffix);
  }

  return p;
}

/* Print the call of an array argument in the module.
 */
__isl_give isl_printer *autosa_module_array_info_print_call_argument(
  __isl_take isl_printer *p, struct autosa_array_info *array)
{
  if (autosa_array_is_read_only_scalar(array))
    return isl_printer_print_str(p, array->name);

  p = isl_printer_print_str(p, array->name);

  return p;
}

/* Print the variable initialization. */
__isl_give isl_printer *autosa_print_var_initialization(
  __isl_take isl_printer *p, struct autosa_kernel_var *var,
  enum platform target)
{  
  for (int i = 0; i < isl_vec_size(var->size); ++i) {
    isl_val *extent;

    if (target == CATAPULT_HW)
      p = print_str_new_line(p, "// hls_pipeline");    

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int c");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, " = 0; c");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, " < ");
    extent = isl_vec_get_element_val(var->size, i);
    p = isl_printer_print_val(p, extent);
    isl_val_free(extent);
    p = isl_printer_print_str(p, "; c");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, "++) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);
  }
  
  if (target == XILINX_HW)
    p = print_str_new_line(p, "// hls_pipeline");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, var->name);
  for (int i = 0; i < isl_vec_size(var->size); ++i) {
    p = isl_printer_print_str(p, "[c");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, "]");
  }
  p = isl_printer_print_str(p, " = 0;");
  p = isl_printer_end_line(p);
  for (int i = 0; i < isl_vec_size(var->size); ++i) {
    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }

  return p;  
}

/* Print the arguments to a module declaration or call. If "types" is set,
 * then print a declaration (including the types of the arguments).
 *
 * The arguments are printed in the following order
 * - the module identifiers
 * - the parameters
 * - the host loop iterators
 * - the arrays accessed by the module
 * - the fifos
 * - the enable signal
 * 
 * If module is to_mem with serialize set as 0, we will replace the arrays 
 * by a serialize fifo.
 */
__isl_give isl_printer *print_module_arguments(
    __isl_take isl_printer *p,
    struct autosa_prog *prog,
    struct autosa_kernel *kernel,
    struct autosa_hw_module *module, int types,
    enum platform target,
    int inter, int arb, int boundary, int serialize)
{
  int first = 1;
  isl_space *space;
  int nparam;
  int n;
  const char *type;

  type = isl_options_get_ast_iterator_type(prog->ctx);
  /* Module identifiers */
  const char *dims[] = {"idx", "idy", "idz"};
  n = isl_id_list_n_id(module->inst_ids);
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    for (int i = 0; i < n; ++i)
    {
      if (!first)
      {
        p = isl_printer_print_str(p, ", ");
        if (!types)
        {
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
        }
      }
      if (types)
      {
        p = isl_printer_print_str(p, type);
        p = isl_printer_print_str(p, " ");
      }
      if (!types)
      {
        p = isl_printer_print_str(p, "/* module id */ ");
      }
      p = isl_printer_print_str(p, dims[i]);
      first = 0;
    }
  }

  /* params */
  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (int i = 0; i < nparam; ++i)
  {
    const char *name;

    name = isl_space_get_dim_name(space, isl_dim_param, i);

    if (!first)
    {
      p = isl_printer_print_str(p, ", ");
      if (!types)
      {
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
      }
    }
    if (types)
      p = isl_printer_print_str(p, "int ");
    if (!types)
      p = isl_printer_print_str(p, "/* param */ ");
    p = isl_printer_print_str(p, name);

    first = 0;
  }
  isl_space_free(space);

  /* host iters */
  if (inter == -1)
    space = module->space;
  else if (inter == 0)
    space = module->intra_space;
  else if (inter == 1)
    space = module->inter_space;

  /* Skip printing the host iterators for inter/intra modules for Catapult HLS */
  if (!(inter >= 0 && target == CATAPULT_HW)) {
    n = isl_space_dim(space, isl_dim_set);
    for (int i = 0; i < n; ++i)
    {
      const char *name;

      if (!first)
      {
        p = isl_printer_print_str(p, ", ");
        if (!types)
        {
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
        }
      }
      name = isl_space_get_dim_name(space, isl_dim_set, i);
      if (types)
      {
        p = isl_printer_print_str(p, type);
        p = isl_printer_print_str(p, " ");
      }
      if (!types)
      {
        p = isl_printer_print_str(p, "/* host iter */ ");
      }
      p = isl_printer_print_str(p, name);
      if (module->double_buffer && inter != -1 && !types)
      {
        if (module->in && inter == 0)
        {
          /* intra trans */
          p = isl_printer_print_str(p, "_prev");
        }
        else if (!module->in && inter == 1)
        {
          /* inter trans */
          p = isl_printer_print_str(p, "_prev");
        }
      }

      first = 0;
    }
  }

  /* Arrays */
  if (module->type != PE_MODULE && module->to_mem)
  {
    if (!module->is_serialized || (module->is_serialized && serialize && !prog->scop->options->autosa->axi_stream)) {
      /* I/O module that accesses the external memory. */
      struct autosa_io_buffer *io_buffer =
          module->io_groups[0]->io_buffers[module->io_groups[0]->io_level - 1];
      //std::cout << io_buffer->n_lane << std::endl;
      //std::cout << module->data_pack_inter << std::endl;
      //int n_lane = (module->is_serialized)? module->data_pack_serialize : module->data_pack_inter;
      int n_lane = (module->is_serialized)? module->data_pack_serialize : io_buffer->n_lane;
      if (!first)
      {
        p = isl_printer_print_str(p, ", ");
        if (!types)
        {
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
        }
      }
      if (types)
      {
        p = autosa_array_info_print_declaration_argument(
              p, module->io_groups[0]->array, n_lane,
              target == INTEL_HW ? "__global volatile" : NULL, -1, prog->scop->options->autosa->mem_port_map, target);
      }
      else
      {
        p = isl_printer_print_str(p, "/* array */ ");
        p = autosa_module_array_info_print_call_argument(p,
                                                         module->io_groups[0]->array);
      }
      first = 0;
    } else if (module->is_serialized && serialize && prog->scop->options->autosa->axi_stream) {
      /* Print a stream fifo */
      struct autosa_io_buffer *io_buffer =
          module->io_groups[0]->io_buffers[module->io_groups[0]->io_level - 1];
      int n_lane = (module->is_serialized)? module->data_pack_serialize : io_buffer->n_lane;
      if (!first) {
        p = isl_printer_print_str(p, ", ");
        if (!types) {
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
        }
      }
      if (types) {
        p = autosa_fifo_print_declaration_arguments(p,
                                                    module->io_groups[0], n_lane, NULL, target);
      } else {
        p = isl_printer_print_str(p, "/* fifo */");
        p = autosa_fifo_print_call_argument(p,  
                                            module->io_groups[0], NULL, target);
      }
      first = 0;
    } else {
      /* Print a serialize fifo */
      int n_lane = module->data_pack_inter;
      if (!first) {
        p = isl_printer_print_str(p, ", ");
        if (!types) {
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
        }
      }
      if (types) {
        p = autosa_fifo_print_declaration_arguments(p,
                                                    module->io_groups[0], n_lane, "serialize", target);
      } else {
        p = isl_printer_print_str(p, "/* fifo */");
        p = autosa_fifo_print_call_argument(p,  
                                            module->io_groups[0], "serialize", target);
      }
      first = 0;
    }
  }
  else if (module->type == PE_MODULE)
  {
    /* Scalars */
    for (int i = 0; i < prog->n_array; i++)
    {
      int required;

      required = autosa_kernel_requires_array_argument(kernel, i);
      if (required < 0)
        return isl_printer_free(p);
      if (!required)
        continue;

      if (autosa_array_is_read_only_scalar(&prog->array[i]))
      {
        if (!first)
        {
          p = isl_printer_print_str(p, ", ");
          if (!types)
          {
            p = isl_printer_end_line(p);
            p = isl_printer_start_line(p);
          }
        }
        if (types)
          p = autosa_array_info_print_declaration_argument(
                p, &prog->array[i], 1, NULL, -1, NULL, target);
        else
        {
          p = isl_printer_print_str(p, "/* scalar */ ");
          p = autosa_array_info_print_call_argument(p,
                                                    &prog->array[i], -1, "buffer");
        }
        first = 0;
      }
    }
  }

  /* Local buffer */
  if (inter != -1)
  {
    for (int i = 0; i < module->n_var; i++)
    {
      struct autosa_kernel_var *var;

      var = (struct autosa_kernel_var *)&module->var[i];
      if (!first)
      {
        p = isl_printer_print_str(p, ", ");
        if (!types)
        {
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
        }
      }
      if (types)
      {
        if (target == CATAPULT_HW) {
          p = isl_printer_print_str(p, "ac_channel<");
          p = isl_printer_print_str(p, module->name);
          p = isl_printer_print_str(p, "_");
          p = isl_printer_print_str(p, var->name);
          p = isl_printer_print_str(p, "> &");
          p = isl_printer_print_str(p, var->name);          
        } else {
          if (module->data_pack_inter == 1 && module->io_groups[0]->local_array->is_sparse == 0) {
            p = isl_printer_print_str(p, var->array->type);
          }
          else {
            p = isl_printer_print_str(p, var->array->name);
            if (var->array->local_array->is_sparse)
              p = isl_printer_print_str(p, "_s");
            p = isl_printer_print_str(p, "_t");
            p = isl_printer_print_int(p, module->data_pack_inter);
          }
          p = isl_printer_print_str(p, " ");
          p = isl_printer_print_str(p, var->name);
          for (int j = 0; j < isl_vec_size(var->size); j++) {
            isl_val *v;
            p = isl_printer_print_str(p, "[");
            v = isl_vec_get_element_val(var->size, j);
            p = isl_printer_print_val(p, v);
            isl_val_free(v);
            p = isl_printer_print_str(p, "]");
          }
        }
      }
      else
      {
        p = isl_printer_print_str(p, "/* array */ ");
        if (target == CATAPULT_HW) {
          p = isl_printer_print_str(p, module->name);
          p = isl_printer_print_str(p, "_");
          p = isl_printer_print_str(p, var->name);
          p = isl_printer_print_str(p, "_inst");
        } else {
          if (!module->double_buffer)
          {
            p = isl_printer_print_str(p, var->name);
          }
          else
          {
            if (arb == 0)
            {
              p = isl_printer_print_str(p, var->name);
              p = isl_printer_print_str(p, inter == 0 ? "_ping" : "_pong");
            }
            else
            {
              p = isl_printer_print_str(p, var->name);
              p = isl_printer_print_str(p, inter == 0 ? "_pong" : "_ping");
            }
          }
        }
      }

      first = 0;
    }
  }

  /* fifos */
  if (module->type == PE_MODULE)
  {
    for (int i = 0; i < module->n_io_group; i++)
    {
      struct autosa_array_ref_group *group = module->io_groups[i];
      //if (!(group->copy_in || group->copy_out))
      //  continue;
      int n_lane = get_io_group_n_lane(module, NULL, group);
      if (module->io_groups[i]->pe_io_dir == IO_IN ||
          module->io_groups[i]->pe_io_dir == IO_INOUT)
      {
        if (!first)
        {
          p = isl_printer_print_str(p, ", ");
          if (!types)
          {
            p = isl_printer_end_line(p);
            p = isl_printer_start_line(p);
          }
        }
        if (types)
        {
          p = autosa_fifo_print_declaration_arguments(p,
                                                      module->io_groups[i], n_lane, "in", target);
        }
        else
        {
          p = isl_printer_print_str(p, "/* fifo */ ");
          p = autosa_fifo_print_call_argument(p,
                                              module->io_groups[i], "in", target);
        }
        first = 0;
      }
      if (module->io_groups[i]->pe_io_dir == IO_OUT ||
          module->io_groups[i]->pe_io_dir == IO_INOUT)
      {
        if (!first)
        {
          p = isl_printer_print_str(p, ", ");
          if (!types)
          {
            p = isl_printer_end_line(p);
            p = isl_printer_start_line(p);
          }
        }
        if (types)
          p = autosa_fifo_print_declaration_arguments(p,
                                                      module->io_groups[i], n_lane, "out", target);
        else
        {
          p = isl_printer_print_str(p, "/* fifo */ ");
          p = autosa_fifo_print_call_argument(p,
                                              module->io_groups[i], "out", target);
        }
        first = 0;
      }
    }
  }
  else
  {
    for (int i = 0; i < module->n_io_group; i++)
    {
      if (!module->to_mem && inter != 0)
      {
        if (!(!module->in && boundary))
        {
          if (!first)
          {
            p = isl_printer_print_str(p, ", ");
            if (!types)
            {
              p = isl_printer_end_line(p);
              p = isl_printer_start_line(p);
            }
          }
          /* in */
          if (types)
            p = autosa_fifo_print_declaration_arguments(p,
                                                        module->io_groups[i], module->data_pack_inter, "in", target);
          else
          {
            p = isl_printer_print_str(p, "/* fifo */ ");
            p = autosa_fifo_print_call_argument(p,
                                                module->io_groups[i], "in", target);
          }
          first = 0;
        }

        if (!(module->in && boundary))
        {
          /* out */
          if (!first)
          {
            p = isl_printer_print_str(p, ", ");
            if (!types)
            {
              p = isl_printer_end_line(p);
              p = isl_printer_start_line(p);
            }
          }
          if (types)
            p = autosa_fifo_print_declaration_arguments(p,
                                                        module->io_groups[i], module->data_pack_inter, "out", target);
          else
          {
            p = isl_printer_print_str(p, "/* fifo */ ");
            p = autosa_fifo_print_call_argument(p,
                                                module->io_groups[i], "out", target);
          }
          first = 0;
        }
      }

      if (inter != 1)
      {
        if (!first)
        {
          p = isl_printer_print_str(p, ", ");
          if (!types)
          {
            p = isl_printer_end_line(p);
            p = isl_printer_start_line(p);
          }
        }
        /* local */
        if (types) {
          p = autosa_fifo_print_declaration_arguments(p,
                                                      module->io_groups[i], 
                                                      (module->is_serialized && serialize)? module->data_pack_inter : module->data_pack_intra,                                                      
                                                      module->in ? "local_out" : "local_in", target);
        } else {
          p = isl_printer_print_str(p, "/* fifo */ ");
          p = autosa_fifo_print_call_argument(p,
                                              module->io_groups[i], module->in ? "local_out" : "local_in", target);
        }
        first = 0;
      }
    }
  }

  /* credit fifo */
  if (module->credit)
  {
    if (!first)
    {
      p = isl_printer_print_str(p, ", ");
      if (!types)
      {
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
      }
    }
    if (types)
    {
      if (target == XILINX_HW)
      {
        p = isl_printer_print_str(p, "hls::stream<int> &credit");
      }
      else
      {
        p = isl_printer_print_str(p, "channel int credit");
      }
    }
    else
    {
      p = isl_printer_print_str(p, "/* credit */ ");
      p = isl_printer_print_str(p, "credit");
    }

    first = 0;
  }

  /* enable signal */
  if (module->double_buffer && inter != -1 && target != CATAPULT_HW)
  {
    if (!first)
    {
      p = isl_printer_print_str(p, ", ");
      if (!types)
      {
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
      }
    }
    if (types)
    {
      p = isl_printer_print_str(p, inter == 0 ? "bool intra_trans_en" : "bool inter_trans_en");
    }
    else
    {
      p = isl_printer_print_str(p, "/* enable */ ");
      p = isl_printer_print_str(p, inter == 0 ? "intra_trans_en" : "inter_trans_en");
    }

    first = 0;
  }

  return p;
}

/* Print the arguments to a pe dummy module declaration or call. If "types" is set,
 * then print a declaration (including the types of the arguments).
 *
 * The arguments are printed in the following order
 * - the module identifiers
 * - the parameters
 * - the host loop iterators 
 * - the arrays accessed by the module
 * - the fifos
 */
__isl_give isl_printer *print_pe_dummy_module_arguments(
    __isl_take isl_printer *p,
    struct autosa_prog *prog,
    struct autosa_kernel *kernel,
    struct autosa_pe_dummy_module *pe_dummy_module,
    int types,
    enum platform target)
{
  int first = 1;
  isl_space *space;
  int nparam;
  int n;
  const char *type;
  struct autosa_hw_module *module = pe_dummy_module->module;

  type = isl_options_get_ast_iterator_type(prog->ctx);
  /* module identifiers */
  const char *dims[] = {"idx", "idy", "idz"};
  n = isl_id_list_n_id(module->inst_ids);
  for (int i = 0; i < n; ++i)
  {
    if (!first)
      p = isl_printer_print_str(p, ", ");
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, dims[i]);

    first = 0;
  }

  /* params */
  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (int i = 0; i < nparam; ++i)
  {
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

  /* host iters */
  space = module->space;

  n = isl_space_dim(space, isl_dim_set);
  for (int i = 0; i < n; ++i)
  {
    const char *name;

    if (!first)
      p = isl_printer_print_str(p, ", ");
    name = isl_space_get_dim_name(space, isl_dim_set, i);
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, name);

    first = 0;
  }

  /* Arrays */
  /* Scalars */
  for (int i = 0; i < prog->n_array; i++)
  {
    int required;

    required = autosa_kernel_requires_array_argument(kernel, i);
    if (required < 0)
      return isl_printer_free(p);
    if (!required)
      continue;

    if (autosa_array_is_read_only_scalar(&prog->array[i]))
    {
      if (!first)
      {
        p = isl_printer_print_str(p, ", ");
      }
      if (types)
        p = autosa_array_info_print_declaration_argument(
              p, &prog->array[i], 1, NULL, -1, NULL, target);
      else
        p = autosa_module_array_info_print_call_argument(p,
                                                         &prog->array[i]);
      first = 0;
    }
  }

  /* fifos */
  struct autosa_array_ref_group *group = pe_dummy_module->io_group;
  int n_lane = get_io_group_n_lane(NULL, pe_dummy_module, group);  

  if (!first)
  {
    p = isl_printer_print_str(p, ", ");
  }
  if (types)
  {
    p = autosa_fifo_print_declaration_arguments(p,
                                                group, n_lane, pe_dummy_module->in? "in" : "out", target);
  }
  else
    p = autosa_fifo_print_call_argument(p,
                                        group, pe_dummy_module->in? "in" : "out", target);
  first = 0;

  return p;
}

/* Print the arguments of the top_gen function:
 * - parameters
 * - host loop iterators
 * - file descriptor
 */
__isl_give isl_printer *print_top_gen_arguments(__isl_take isl_printer *p,
                                                struct autosa_prog *prog, struct autosa_kernel *kernel, int types)
{
  int i, n;
  int first = 1;
  unsigned nparam;
  isl_space *space;
  const char *type;

  /* Parameters */
  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (i = 0; i < nparam; ++i)
  {
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

  /* Host iterators */
  n = isl_space_dim(kernel->space, isl_dim_set);
  type = isl_options_get_ast_iterator_type(prog->ctx);
  for (i = 0; i < n; ++i)
  {
    const char *name;

    if (!first)
      p = isl_printer_print_str(p, ", ");
    name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, name);

    first = 0;
  }

  /* File description */
  if (!first)
    p = isl_printer_print_str(p, ", ");
  if (types)
  {
    p = isl_printer_print_str(p, "FILE *");
  }
  p = isl_printer_print_str(p, "f");

  first = 0;

  return p;
}

static __isl_give isl_printer *print_top_gen_header(__isl_take isl_printer *p,
                                                    struct autosa_prog *prog, struct autosa_hw_top_module *top)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "void ");
  p = isl_printer_print_str(p, "top_generate");
  p = isl_printer_print_str(p, "(");
  p = print_top_gen_arguments(p, prog, top->kernel, 1);
  p = isl_printer_print_str(p, ")");

  return p;
}

void print_top_gen_headers(
    struct autosa_prog *prog, struct autosa_hw_top_module *top, struct hls_info *hls)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->top_gen_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_top_gen_header(p, prog, top);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->top_gen_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_top_gen_header(p, prog, top);
  p = isl_printer_end_line(p);
  isl_printer_free(p);
}

/* Print out
 * "\/* [module_name] FIFO *\/"
 */
static __isl_give isl_printer *print_fifo_comment(
    __isl_take isl_printer *p, struct autosa_hw_module *module)
{
  p = isl_printer_print_str(p, "/* ");
  p = isl_printer_print_str(p, module->name);
  p = isl_printer_print_str(p, " fifo */");

  return p;
}

/* Print out
 * "_[c0 + val]"
 * Increase the "pos"th index by the value of "val"
 */
static __isl_give isl_printer *print_inst_ids_inc_suffix(
    __isl_take isl_printer *p, int n, int pos, int val)
{
  for (int i = 0; i < n; i++)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_\");");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c");
    p = isl_printer_print_int(p, i);
    if (i == pos)
    {
      if (val != 0)
      {
        p = isl_printer_print_str(p, " + ");
        p = isl_printer_print_int(p, val);
      }
    }
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);
  }

  return p;
}

/* Print out
 * "_c0_c1"
 */
static __isl_give isl_printer *print_inst_ids_suffix(
    __isl_take isl_printer *p, int n, __isl_keep isl_vec *offset)
{
  for (int i = 0; i < n; i++)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_\");");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c");
    p = isl_printer_print_int(p, i);
    if (offset)
    {
      isl_val *val = isl_vec_get_element_val(offset, i);
      if (!isl_val_is_zero(val))
      {
        p = isl_printer_print_str(p, " + ");
        p = isl_printer_print_val(p, val);
      }
      isl_val_free(val);
    }
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);
  }

  return p;
}

/* This function prints the inst ids described by "expr".
 * If the "offset" is set, it is added to the inst ids.
 */
static __isl_give isl_printer *print_pretrans_inst_ids_suffix(
    __isl_take isl_printer *p, int n_id,
    __isl_keep isl_ast_expr *expr, __isl_keep isl_vec *offset)
{
  isl_ctx *ctx = isl_ast_expr_get_ctx(expr);
  int n;

  n = isl_ast_expr_op_get_n_arg(expr);
  for (int i = 0; i < n_id; i++)
  {
    isl_ast_expr *expr_i = isl_ast_expr_get_op_arg(expr, i + 1);
    int format;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_\");");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_int(p, ");
    format = isl_printer_get_output_format(p);
    p = isl_printer_set_output_format(p, ISL_FORMAT_C);
    p = isl_printer_print_ast_expr(p, expr_i);
    p = isl_printer_set_output_format(p, format);
    if (offset)
    {
      isl_val *val = isl_vec_get_element_val(offset, i);
      if (!isl_val_is_zero(val))
      {
        p = isl_printer_print_str(p, " + ");
        p = isl_printer_print_val(p, val);
      }
      isl_val_free(val);
    }
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);

    isl_ast_expr_free(expr_i);
  }

  return p;
}

static __isl_give isl_printer *print_fifo_decl_single(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct autosa_prog *prog,
    struct hls_info *hls, int pe_inout, const char *suffix)
{
  struct autosa_hw_module *module = stmt->u.m.module;
  struct autosa_array_ref_group *group = stmt->u.m.group;
  int boundary = stmt->u.m.boundary;
  int n;
  int n_lane;
  int fifo_depth = prog->scop->options->autosa->fifo_depth;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "// Count channel number");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "fifo_cnt++;");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "// Print channel declarations of module: ");
  p = isl_printer_print_str(p, module->name);
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
  p = print_fifo_comment(p, module);
  p = isl_printer_print_str(p, " ");
  n_lane = get_io_group_n_lane(module, NULL, group);
  if (hls->target == XILINX_HW)
    p = print_fifo_type_xilinx(p, group, n_lane);
  else if (hls->target == INTEL_HW)
    p = print_fifo_type_intel(p, group, n_lane);
  else if (hls->target == CATAPULT_HW)
    p = print_fifo_type_catapult(p, group, n_lane);
  p = isl_printer_print_str(p, " ");
  p = autosa_array_ref_group_print_fifo_name(group, p);
  p = isl_printer_print_str(p, "_");
  p = isl_printer_print_str(p, module->name);
  if (pe_inout)
  {
    p = isl_printer_print_str(p, suffix);
  }
  p = isl_printer_print_str(p, "\");");
  p = isl_printer_end_line(p);

  n = isl_id_list_n_id(module->inst_ids);
  if (module->type == IO_MODULE || module->type == DRAIN_MODULE)
  {
    if (boundary)
    {
      p = print_inst_ids_inc_suffix(p, n, n - 1, 1);
    }
    else
    {
      p = print_inst_ids_suffix(p, n, NULL);
    }
  }
  else if (module->type == PE_MODULE)
  {
    if (boundary)
      p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, group->dir);
    else
      p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, NULL);
  }
  if (hls->target == INTEL_HW)
  {
    /* Print fifo attribute */
    //p = print_str_new_line(p, "p = isl_printer_print_str(p, \" __attribute__((depth(2)))\");");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \" __attribute__((depth(");
    p = isl_printer_print_int(p, fifo_depth);
    p = isl_printer_print_str(p, ")))\");");
    p = isl_printer_end_line(p);
  }  
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \";\");");  
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  if (hls->target == XILINX_HW)
  {
    /* Print fifo pragma */
    p = print_str_new_line(p, "p = isl_printer_start_line(p);");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS STREAM variable=");
    p = autosa_array_ref_group_print_fifo_name(group, p);
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, module->name);
    if (pe_inout)
    {
      p = isl_printer_print_str(p, suffix);
    }
    p = isl_printer_print_str(p, "\");");
    p = isl_printer_end_line(p);

    if (module->type == IO_MODULE || module->type == DRAIN_MODULE)
    {
      if (boundary)
      {
        p = print_inst_ids_inc_suffix(p, n, n - 1, 1);
      }
      else
      {
        p = print_inst_ids_suffix(p, n, NULL);
      }
    }
    else if (module->type == PE_MODULE)
    {
      if (boundary)
      {
        p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, group->dir);
      }
      else
      {
        p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, NULL);
      }
    }
    //p = print_str_new_line(p, "p = isl_printer_print_str(p, \" depth=2\");");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \" depth=");
    p = isl_printer_print_int(p, fifo_depth);
    p = isl_printer_print_str(p, "\");");
    p = isl_printer_end_line(p);
    
    p = print_str_new_line(p, "p = isl_printer_end_line(p);");

    /* If depth * width > 512 bits, HLS will use BRAM to implement FIFOs.
     * Instead, we will insert pragmas to use SRL instead.
     * Modified: Use SRL anytime.
     */
    /* Print fifo resource pragma. */
    //if (n_lane * group->array->size >= 32)
    {
      p = print_str_new_line(p, "p = isl_printer_start_line(p);");

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS RESOURCE variable=");
      p = autosa_array_ref_group_print_fifo_name(group, p);
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, module->name);
      if (pe_inout)
      {
        p = isl_printer_print_str(p, suffix);
      }
      p = isl_printer_print_str(p, "\");");
      p = isl_printer_end_line(p);

      if (module->type == IO_MODULE || module->type == DRAIN_MODULE)
      {
        if (boundary)
        {
          p = print_inst_ids_inc_suffix(p, n, n - 1, 1);
        }
        else
        {
          p = print_inst_ids_suffix(p, n, NULL);
        }
      }
      else if (module->type == PE_MODULE)
      {
        if (boundary)
        {
          p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, group->dir);
        }
        else
        {
          p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, NULL);
        }
      }      
      p = print_str_new_line(p, "p = isl_printer_print_str(p, \" core=FIFO_SRL\");");      
      p = print_str_new_line(p, "p = isl_printer_end_line(p);");
    }

    /* For sparse structure, we will need to perform data pack. */
    if (group->local_array->is_sparse) {
      p = print_str_new_line(p, "p = isl_printer_start_line(p);");
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS DATA_PACK variable=");
      p = autosa_array_ref_group_print_fifo_name(group, p);
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, module->name);
      if (pe_inout)
      {
        p = isl_printer_print_str(p, suffix);
      }
      p = isl_printer_print_str(p, "\");");
      p = isl_printer_end_line(p);

      if (module->type == IO_MODULE || module->type == DRAIN_MODULE)
      {
        if (boundary)
        {
          p = print_inst_ids_inc_suffix(p, n, n - 1, 1);
        }
        else
        {
          p = print_inst_ids_suffix(p, n, NULL);
        }
      }
      else if (module->type == PE_MODULE)
      {
        if (boundary)
        {
          p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, group->dir);
        }
        else
        {
          p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, NULL);
        }
      }                  
      p = print_str_new_line(p, "p = isl_printer_end_line(p);");
    }    
  }

  return p;
}

/* if module->type == PE_MODULE
 *   if boundary == 0:
 *     new_inst_id = io_trans(inst_id)
 *     print [fifo_name]_[module_name]_[new_inst_id]
 *   else if boundary == 1:
 *     new_inst_id = io_trans(inst_id)
 *     print [fifo_name]_[module_name]_[new_inst_id + dep_dir]
 * if module->type == IO_MODULE:
 *     print [fifo_name]_[module_name]_[inst_id]
 */
static __isl_give isl_printer *print_fifo_decl(__isl_take isl_printer *p,
                                               struct autosa_kernel_stmt *stmt, struct autosa_prog *prog, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.m.module;
  struct autosa_array_ref_group *group = stmt->u.m.group;
  int pe_inout;

  if (group->io_type == AUTOSA_INT_IO && module->type == PE_MODULE && group->pe_io_dir == IO_INOUT)
  {
    pe_inout = 1;
  }
  else
  {
    pe_inout = 0;
  }

  if (pe_inout)
  {
    p = print_fifo_decl_single(p, stmt, prog, hls, 1, "_in");
    p = print_fifo_decl_single(p, stmt, prog, hls, 1, "_out");
  }
  else
  {
    p = print_fifo_decl_single(p, stmt, prog, hls, 0, NULL);
  }

  return p;
}

__isl_give isl_printer *autosa_kernel_print_fifo_decl(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct autosa_prog *prog, struct hls_info *hls)
{
  p = ppcg_start_block(p);

  /* Build the fifo_decl. */
  p = print_fifo_decl(p, stmt, prog, hls);

  p = ppcg_end_block(p);

  return p;
}

static __isl_give isl_printer *print_delimiter(__isl_take isl_printer *p,
                                               int *first)
{
  if (!(*first))
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \",\");");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
    p = isl_printer_end_line(p);
  }
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
  p = isl_printer_end_line(p);

  *first = 0;

  return p;
}

static __isl_give isl_printer *print_fifo_annotation(__isl_take isl_printer *p)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* fifo */ \");");
  p = isl_printer_end_line(p);

  return p;
}

/* Print out
 * [fifo_name]_[module_name]
 */
static __isl_give isl_printer *print_fifo_prefix(__isl_take isl_printer *p,
                                                 struct autosa_hw_module *module, struct autosa_array_ref_group *group)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
  p = autosa_array_ref_group_print_fifo_name(group, p);
  p = isl_printer_print_str(p, "_");
  p = isl_printer_print_str(p, module->name);
  p = isl_printer_print_str(p, "\");");
  p = isl_printer_end_line(p);

  return p;
}

/* Print the upper body of the module call, including:
 * - module identifier
 * - parameters
 * - host loop iterators
 * - arrays
 * - inter-module fifos
 */
__isl_give isl_printer *print_module_call_upper(__isl_take isl_printer *p,
                                                struct autosa_kernel_stmt *stmt, struct autosa_prog *prog,
                                                enum platform target)
{
  struct autosa_hw_module *module = stmt->u.m.module;
  struct autosa_pe_dummy_module *pe_dummy_module = stmt->u.m.pe_dummy_module;
  int lower = stmt->u.m.lower;
  int upper = stmt->u.m.upper;
  int boundary = stmt->u.m.boundary;
  int serialize = stmt->u.m.serialize;
  int dummy = stmt->u.m.dummy;
  int first = 1;
  int n;
  char *module_name = stmt->u.m.module_name;
  isl_space *space;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "// Print calls of module: ");
  p = isl_printer_print_str(p, module_name);
  if (boundary) {
    p = isl_printer_print_str(p, "_boundary");
  }
  if (serialize) {
    p = isl_printer_print_str(p, "_serialize");
  }
  p = isl_printer_end_line(p);

  if (dummy && stmt->u.m.lower_sched_val != -1) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int c");
    p = isl_printer_print_int(p, isl_id_list_n_id(module->inst_ids) - 1);
    p = isl_printer_print_str(p, " = ");
    p = isl_printer_print_int(p, stmt->u.m.lower_sched_val);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
  p = isl_printer_print_str(p, module_name);
  if (boundary) {
    p = isl_printer_print_str(p, "_boundary");
  }
  if (serialize) {
    p = isl_printer_print_str(p, "_serialize");
  }  

  if (target == XILINX_HW) {
    if (!dummy && module->type == PE_MODULE)
      p = isl_printer_print_str(p, "_wrapper");
    else if (module->type != PE_MODULE && module->level == 1)
      p = isl_printer_print_str(p, "_wrapper");
  }
  if (target == CATAPULT_HW) {
    p = isl_printer_print_str(p, "_inst\");");
    /* Print module ids if any */
    if (isl_id_list_n_id(module->inst_ids) > 0) {
      for (int i = 0; i < isl_id_list_n_id(module->inst_ids); i++)
      {
        p = print_str_new_line(p, "p = isl_printer_print_str(p, \"_\");");
        p = isl_printer_start_line(p);        
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \".run");    
  }
  p = isl_printer_print_str(p, "\");");
  p = isl_printer_end_line(p);

  if (isl_id_list_n_id(module->inst_ids) > 0 && prog->scop->options->autosa->use_cplusplus_template) {
    p = print_str_new_line(p, "p = isl_printer_print_str(p, \"<\");");    
    if (!dummy) {
      for (int i = 0; i < isl_id_list_n_id(module->inst_ids); i++) {
        if (i > 0) {          
          p = print_str_new_line(p, "p = isl_printer_print_str(p, \", \");");
        }
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    } else {
      isl_ast_expr *expr = pe_dummy_module->io_group->io_L1_pe_expr;
      int n_arg = isl_ast_expr_op_get_n_arg(expr);
      for (int i = 0; i < isl_id_list_n_id(module->inst_ids); i++) {
        if (i > 0) {          
          p = print_str_new_line(p, "p = isl_printer_print_str(p, \", \");");
        }
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, ");
        isl_ast_expr *expr_i = isl_ast_expr_get_op_arg(expr, i + 1);
        p = isl_printer_print_ast_expr(p, expr_i);
        isl_ast_expr_free(expr_i);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
    p = print_str_new_line(p, "p = isl_printer_print_str(p, \">\");");
  }
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"(\");");  

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, 2);");
  p = isl_printer_end_line(p);

  /* module identifiers */
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    if (!dummy)
    {
      for (int i = 0; i < isl_id_list_n_id(module->inst_ids); i++)
      {
        p = print_delimiter(p, &first);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* module id */ \");");
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
    else
    {
      isl_ast_expr *expr = pe_dummy_module->io_group->io_L1_pe_expr;
      int n_arg = isl_ast_expr_op_get_n_arg(expr);
      for (int i = 0; i < isl_id_list_n_id(module->inst_ids); i++)
      {
        int format;
        p = print_delimiter(p, &first);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* module id */ \");");
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, ");
        
        isl_ast_expr *expr_i = isl_ast_expr_get_op_arg(expr, i + 1);
        p = isl_printer_print_ast_expr(p, expr_i);
        isl_ast_expr_free(expr_i);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
  }

  /* params */
  space = isl_union_set_get_space(module->kernel->arrays);
  n = isl_space_dim(space, isl_dim_param);
  for (int i = 0; i < n; i++)
  {
    p = print_delimiter(p, &first);

    const char *name = isl_space_get_dim_name(space, isl_dim_set, i);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* param */");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, "\");");
    p = isl_printer_end_line(p);
  }
  isl_space_free(space);

  /* host iterators */
  n = isl_space_dim(module->kernel->space, isl_dim_set);
  for (int i = 0; i < n; i++)
  {
    p = print_delimiter(p, &first);

    const char *name = isl_space_get_dim_name(module->kernel->space, isl_dim_set, i);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* host iter */ ");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, "\");");
    p = isl_printer_end_line(p);
  }

  /* scalar and arrays */
  if (module->type != PE_MODULE && module->to_mem && 
      ((module->is_serialized && serialize) || !module->is_serialized))
  {
    p = print_delimiter(p, &first);

    p = isl_printer_start_line(p);
    if (prog->scop->options->autosa->axi_stream) {
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* fifo */ ");    
      p = isl_printer_print_str(p, "fifo_");    
      p = isl_printer_print_str(p, module->io_groups[0]->array->name);
    } else {
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* array */ ");    
      p = isl_printer_print_str(p, module->io_groups[0]->array->name);
    }
    if (module->io_groups[0]->local_array->n_io_group_refs > 1)
    {
      if (module->io_groups[0]->n_mem_ports == 1)
      {
        /* Print A_[module_n_array_ref] */
        p = isl_printer_print_str(p, "_");
        p = isl_printer_print_int(p, module->n_array_ref);
        p = isl_printer_print_str(p, "\");");
        p = isl_printer_end_line(p);
      }
      else
      {
        /* Print A_[module_n_array_ref + c0] */
        p = isl_printer_print_str(p, "_\");");
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c0 + ");
        p = isl_printer_print_int(p, module->n_array_ref);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
    else
    {
      p = isl_printer_print_str(p, "\");");
      p = isl_printer_end_line(p);
    }
  }
  else if (module->type == PE_MODULE)
  {
    for (int i = 0; i < prog->n_array; i++)
    {
      int required;

      required = autosa_kernel_requires_array_argument(module->kernel, i);
      if (required < 0)
        return isl_printer_free(p);
      if (!required)
        continue;

      if (autosa_array_is_read_only_scalar(&prog->array[i]))
      {
        p = print_delimiter(p, &first);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* scalar */ ");
        p = isl_printer_print_str(p, module->io_groups[0]->array->name);
        p = isl_printer_print_str(p, "\");");
        p = isl_printer_end_line(p);
      }
    }
  }

  /* FIFO */
  n = isl_id_list_n_id(module->inst_ids);
  if (module->type == PE_MODULE)
  {
    if (dummy)
    {
      struct autosa_array_ref_group *group = pe_dummy_module->io_group;
      p = print_delimiter(p, &first);
      p = print_fifo_annotation(p);
      p = print_fifo_prefix(p, module, group);
      if (isl_vec_is_zero(group->dir))
      {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_in\")");
        p = isl_printer_end_line(p);
      }
      if (pe_dummy_module->in)
        p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, group->dir);
      else
        p = print_pretrans_inst_ids_suffix(p, n, group->io_L1_pe_expr, NULL);
    }
    else
    {
      for (int i = 0; i < module->n_io_group; i++)
      {
        struct autosa_array_ref_group *group = module->io_groups[i];
        if (group->pe_io_dir == IO_NULL)
          continue;
        if (group->pe_io_dir == IO_INOUT)
        {
          p = print_delimiter(p, &first);
          p = print_fifo_annotation(p);
          p = print_fifo_prefix(p, module, group);          
          if (group->io_type == AUTOSA_INT_IO)
          {
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_in\");");
            p = isl_printer_end_line(p);
          }
          p = print_inst_ids_suffix(p, n, NULL);

          p = print_delimiter(p, &first);
          p = print_fifo_annotation(p);
          p = print_fifo_prefix(p, module, group);          
          if (group->io_type == AUTOSA_INT_IO)
          {
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_out\");");
            p = isl_printer_end_line(p);
          }          
          if (group->io_type == AUTOSA_INT_IO)
          {
            p = print_inst_ids_suffix(p, n, NULL);
          }
          else
          {
            p = print_inst_ids_suffix(p, n, group->dir);
          }
        }
        else
        {
          p = print_delimiter(p, &first);
          p = print_fifo_annotation(p);
          p = print_fifo_prefix(p, module, group);
          p = print_inst_ids_suffix(p, n, NULL);
        }
      }
    }
  }
  else
  {
    if (!module->to_mem)
    {
      for (int i = 0; i < module->n_io_group; i++)
      {
        struct autosa_array_ref_group *group = module->io_groups[i];
        if (module->in)
        {
          p = print_delimiter(p, &first);
          p = print_fifo_annotation(p);
          p = print_fifo_prefix(p, module, group);
          p = print_inst_ids_suffix(p, n, NULL);

          if (!boundary)
          {
            p = print_delimiter(p, &first);
            p = print_fifo_annotation(p);
            p = print_fifo_prefix(p, module, group);
            p = print_inst_ids_inc_suffix(p, n, n - 1, 1);
          }
        }
        else
        {
          if (!boundary)
          {
            p = print_delimiter(p, &first);
            p = print_fifo_annotation(p);
            p = print_fifo_prefix(p, module, group);
            p = print_inst_ids_inc_suffix(p, n, n - 1, 1);
          }

          p = print_delimiter(p, &first);
          p = print_fifo_annotation(p);
          p = print_fifo_prefix(p, module, group);
          p = print_inst_ids_suffix(p, n, NULL);
        }
      }
    } else {
      if (module->is_serialized && !serialize) {
        struct autosa_array_ref_group *group = module->io_groups[0];
        p = print_delimiter(p, &first);
        p = print_fifo_annotation(p);
        p = print_fifo_prefix(p, module, group);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_serialize\");");
        p = isl_printer_end_line(p);
      }
    }
  }

  return p;
}

/* Build the lower-level module name to the current "module".
 */
static char *build_io_module_lower_name(struct autosa_hw_module *module)
{
  struct autosa_array_ref_group *group = module->io_groups[0];

  isl_printer *p = isl_printer_to_str(module->kernel->ctx);
  p = isl_printer_print_str(p, group->array->name);
  if (group->group_type == AUTOSA_IO_GROUP)
  {
    if (group->local_array->n_io_group > 1)
    {
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_int(p, group->nr);
    }
  }
  else if (group->group_type == AUTOSA_DRAIN_GROUP)
  {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, "drain");
  }
  p = isl_printer_print_str(p, "_IO_L");
  p = isl_printer_print_int(p, module->level - 1);
  if (module->in)
    p = isl_printer_print_str(p, "_in");
  else
    p = isl_printer_print_str(p, "_out");

  char *name = isl_printer_get_str(p);
  isl_printer_free(p);

  return name;
}

/* Print the prefix of fifos to the lower-level modules. 
 */
static __isl_give isl_printer *print_fifo_prefix_lower(
    __isl_take isl_printer *p,
    struct autosa_hw_module *module, struct autosa_array_ref_group *group)
{
  int lower_is_PE;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
  p = autosa_array_ref_group_print_fifo_name(group, p);
  p = isl_printer_print_str(p, "_");
  assert(module->type != PE_MODULE);

  if (module->to_pe)
    lower_is_PE = 1;
  else
    lower_is_PE = 0;

  if (!lower_is_PE)
  {
    char *name = build_io_module_lower_name(module);
    p = isl_printer_print_str(p, name);
    free(name);
  }
  else
  {
    p = isl_printer_print_str(p, "PE");
  }
  p = isl_printer_print_str(p, "\");");
  p = isl_printer_end_line(p);

  return p;
}

/* Print the lower body of the module call, including the 
 * fifos to the lower-level modules.
 */
static __isl_give isl_printer *print_module_call_lower(__isl_take isl_printer *p,
                                                       struct autosa_kernel_stmt *stmt, struct autosa_prog *prog)
{
  struct autosa_hw_module *module = stmt->u.m.module;
  int lower = stmt->u.m.lower;
  int first = 0;
  int n = isl_id_list_n_id(module->inst_ids);
  int lower_is_PE;
  int boundary = stmt->u.m.boundary;
  int serialize = stmt->u.m.serialize;

  if (lower)
  {
    struct autosa_array_ref_group *group = module->io_groups[0];

    p = print_delimiter(p, &first);
    p = print_fifo_annotation(p);
    if (serialize) {
      p = print_fifo_prefix(p, module, group);
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_serialize\");");      
      p = isl_printer_end_line(p);
    } else {
      p = print_fifo_prefix_lower(p, module, group);
  
      if (module->to_pe)
        lower_is_PE = 1;
      else
        lower_is_PE = 0;
  
      if (group->io_type == AUTOSA_INT_IO && lower_is_PE && group->pe_io_dir == IO_INOUT)
      {
        /* Add in/out suffix. */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
        p = isl_printer_print_str(p, module->in ? "_in" : "_out");
        p = isl_printer_print_str(p, "\");");
        p = isl_printer_end_line(p);
      }
  
      if (lower_is_PE) {
        p = print_pretrans_inst_ids_suffix(p, module->kernel->n_sa_dim,
                                           boundary ? group->io_pe_expr_boundary : group->io_pe_expr, 
                                           module->in || group->pe_io_dir != IO_INOUT? NULL : group->dir
                                           );
      } else {
        if (stmt->u.m.lower_sched_val != -1) {
          p = print_inst_ids_suffix(p, n, NULL);
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"_");
          p = isl_printer_print_int(p, stmt->u.m.lower_sched_val);
          p = isl_printer_print_str(p, "\");");
          p = isl_printer_end_line(p);        
        } else {
          p = print_inst_ids_suffix(p, n + 1, NULL);
        }
      }
    }
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, -2);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \");\");");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
  p = isl_printer_end_line(p);

  return p;
}

/* Print out the module call instantionation in the private class fields for 
 * Catapult HLS.
 */
__isl_give isl_printer *autosa_kernel_print_module_call_inst(
  __isl_take isl_printer *p,
  struct autosa_kernel_stmt *stmt, struct autosa_prog *prog,
  enum platform target)
{
  int upper = stmt->u.m.upper;
  int lower = stmt->u.m.lower;
  int complete = (upper == 0 && lower == 0);
  int dummy = stmt->u.m.dummy;
  int boundary = stmt->u.m.boundary;
  int serialize = stmt->u.m.serialize;
  char *module_name = stmt->u.m.module_name;
  struct autosa_hw_module *module = stmt->u.m.module;

  if (dummy)
    return p;

  p = ppcg_start_block(p);

  if (complete || upper) {
    p = print_str_new_line(p, "p = isl_printer_start_line(p);");
    
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
    p = isl_printer_print_str(p, module->name);
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");    
    if (serialize)
      p = isl_printer_print_str(p, "_serialize");    
    p = isl_printer_print_str(p, "\");");
    p = isl_printer_end_line(p);

    //p = print_str_new_line(p, "p = isl_printer_end_line(p);");

    //p = print_str_new_line(p, "p = isl_printer_start_line(p);");
    p = print_str_new_line(p, "p = isl_printer_print_str(p, \" \");");
    
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"");
    p = isl_printer_print_str(p, module->name);
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");    
    if (serialize)
      p = isl_printer_print_str(p, "_serialize");    
    p = isl_printer_print_str(p, "_inst");
    p = isl_printer_print_str(p, "\");");
    p = isl_printer_end_line(p);    

    /* Print the module ids if any */
    if (!dummy)
    {
      for (int i = 0; i < isl_id_list_n_id(module->inst_ids); i++)
      {                 
        p = print_str_new_line(p, "p = isl_printer_print_str(p, \"_\");");
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_int(p, c");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, ");");        
        p = isl_printer_end_line(p);
      }
    }
    
    p = print_str_new_line(p, "p = isl_printer_print_str(p, \";\");");
    p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  } 

  p = ppcg_end_block(p);

  return p;
}

/* Print out the module calls:
 * - module_call_upper
 * - module_call_lower
 */
__isl_give isl_printer *autosa_kernel_print_module_call(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct autosa_prog *prog,
    enum platform target)
{
  int upper = stmt->u.m.upper;
  int lower = stmt->u.m.lower;
  int complete = (upper == 0 && lower == 0);
  int dummy = stmt->u.m.dummy;
  int boundary = stmt->u.m.boundary;
  int serialize = stmt->u.m.serialize;
  char *module_name = stmt->u.m.module_name;
  struct autosa_hw_module *module = stmt->u.m.module;
  p = ppcg_start_block(p);

  /* Build the module name. */
  if (complete)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "// Count module number");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module_name);
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");
    p = isl_printer_print_str(p, "_cnt++;");
    p = isl_printer_end_line(p);
    if (module->is_filter && module->is_buffer)
    {
      /* Print counter for inter_trans and intra_trans module. */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, module_name);
      p = isl_printer_print_str(p, "_intra_trans_cnt++;");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, module_name);
      if (boundary)
        p = isl_printer_print_str(p, "_inter_trans_boundary_cnt++;");
      else
        p = isl_printer_print_str(p, "_inter_trans_cnt++;");
      p = isl_printer_end_line(p);
    }

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* Module Call */\");");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
    p = isl_printer_end_line(p);

    p = print_module_call_upper(p, stmt, prog, target);
    p = print_module_call_lower(p, stmt, prog);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* Module Call */\");");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
    p = isl_printer_end_line(p);
  }
  else
  {
    if (upper)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "// Count module number");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, module_name);
      if (boundary)
        p = isl_printer_print_str(p, "_boundary");
      if (serialize)        
        p = isl_printer_print_str(p, "_serialize");
      p = isl_printer_print_str(p, "_cnt++;");
      p = isl_printer_end_line(p);
      if (module->is_filter && module->is_buffer && !serialize)
      {
        /* Print counter for inter_trans and intra_trans module */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, module_name);
        p = isl_printer_print_str(p, "_intra_trans_cnt++;");
        p = isl_printer_end_line(p);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, module_name);
        if (boundary)
          p = isl_printer_print_str(p, "_inter_trans_boundary_cnt++;");
        else
          p = isl_printer_print_str(p, "_inter_trans_cnt++;");
        p = isl_printer_end_line(p);
      }

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* Module Call */\");");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
      p = isl_printer_end_line(p);

      p = print_module_call_upper(p, stmt, prog, target);
    }
    else
    {
      p = print_module_call_lower(p, stmt, prog);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* Module Call */\");");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
      p = isl_printer_end_line(p);
    }
  }

  p = ppcg_end_block(p);

  return p;
}

/* If read, print:
 *   "[fifo_name].read()"
 * else, print:
 *   "[fifo_name].write("
 */
__isl_give isl_printer *print_fifo_rw_xilinx(__isl_take isl_printer *p,
                                             const char *fifo_name, int read)
{
  if (read)
  {
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ".read()");
  }
  else
  {
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ".write(");
  }
  return p;
}

__isl_give isl_printer *print_fifo_rw_catapult(
  __isl_take isl_printer *p, const char *fifo_name, int read)
{
  if (read) {
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ".read()");
  } else {
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ".write(");
  }
  return p;
}

/* If read, print:
 *   "read_channel_intel([fifo_name])"
 * else, print:
 *   "write_channel_intel([fifo_name])"
 */
__isl_give isl_printer *print_fifo_rw_intel(__isl_take isl_printer *p,
                                            const char *fifo_name, int read)
{
  if (read)
  {
    p = isl_printer_print_str(p, "read_channel_intel(");
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ")");
  }
  else
  {
    p = isl_printer_print_str(p, "write_channel_intel(");
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ", ");
  }
  return p;
}

/* Print an I/O statement.
 *
 * An in I/O statement is printed as
 *
 *  local[] = fifo.read(); 
 *
 * while an out I/O statement is printed as
 *
 *  fifo.write(local);
 */
__isl_give isl_printer *autosa_kernel_print_io(__isl_take isl_printer *p,
                                               struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.i.module;
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_kernel *kernel = module->kernel;
  char *fifo_name;
  isl_ctx *ctx = isl_printer_get_ctx(p);
  int is_dummy = stmt->u.i.dummy;
  fifo_name = concat(ctx, stmt->u.i.in_fifo_name, stmt->u.i.in == 1 ? "in" : "out");
  int data_pack = stmt->u.i.data_pack;  

  /* Extract the sparse data. */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  if (is_dummy)  
  {
    if (stmt->u.i.in) {
      /* [type] fifo_data; */
      p = isl_printer_start_line(p);
      if (is_sparse) {
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, "_s_t");
        p = isl_printer_print_int(p, data_pack);
      } else {
        //if (data_pack == 1)
        //{
        //  p = isl_printer_print_str(p, group->array->type);
        //}
        //else
        //{
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, data_pack);
        //}
      }
      p = isl_printer_print_str(p, " fifo_data;");
      p = isl_printer_end_line(p);

      /* fifo_data = fifo.read(); */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 1);
      else if (hls->target == CATAPULT_HW)  
        p = print_fifo_rw_catapult(p, fifo_name, 1);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      free(fifo_name);
      return p;
    } else {
      /* Send zeros by default, might be buggy. */      
      /* [type] fifo_data = 0; */
      p = isl_printer_start_line(p);
      //if (data_pack == 1) {
      //  p = isl_printer_print_str(p, group->array->type);
      //} else {
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, "_t");
        p = isl_printer_print_int(p, data_pack);
      //}
      p = isl_printer_print_str(p, " fifo_data = 0;");
      p = isl_printer_end_line(p);
      
      /* fifo.write(fifo_data); */
      p = isl_printer_start_line(p);
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 0);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 0);
      p = isl_printer_print_str(p, "fifo_data);");
      p = isl_printer_end_line(p);

      free(fifo_name);
      return p;      
    }
  }

  int nxt_data_pack = stmt->u.i.nxt_data_pack;
  isl_ast_expr *local_index_packed;
  isl_ast_expr *arg, *div;
  int n_arg;
  local_index_packed = isl_ast_expr_copy(stmt->u.i.local_index);
  /* Modify the local index. */
  if (data_pack > 1)
  {
    n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
    arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
    div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, data_pack));
    arg = isl_ast_expr_div(arg, div);
    local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
  }

  if (data_pack == nxt_data_pack && !group->local_array->is_sparse)
  {
    // TODO: modify the sparse

    /* local[] = fifo.read() */
    p = isl_printer_start_line(p);
    if (stmt->u.i.in)
    {
      p = isl_printer_print_ast_expr(p, local_index_packed);
      p = isl_printer_print_str(p, " = ");
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 1);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 1);
    }
    else
    {
      /* fifo.write(local[]) */
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 0);
      else if (hls->target == CATAPULT_HW)  
        p = print_fifo_rw_catapult(p, fifo_name, 0);
      p = isl_printer_print_ast_expr(p, local_index_packed);
      p = isl_printer_print_str(p, ")");
    }
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  } 
  else
  {
    p = ppcg_start_block(p);
    if (!kernel->sparse) {
      /* [type] fifo_data; */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, group->array->name);    
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, data_pack);
      p = isl_printer_print_str(p, " fifo_data;");
      p = isl_printer_end_line(p);
    }    

    if (kernel->sparse && is_sparse == 0 && stmt->u.i.in) {
      /* [type] tmp_X[]; */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, group->array->type);      
      p = isl_printer_print_str(p, " ");
      p = isl_printer_print_str(p, group->array->name);
      p = isl_printer_print_str(p, "_tmp[1][");
      p = isl_printer_print_int(p, group->n_lane);
      p = isl_printer_print_str(p, "];");
      p = isl_printer_end_line(p);

      if (hls->target == XILINX_HW) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=");
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, "_tmp dim=0 complete");
        p = isl_printer_end_line(p);
      }
    }

    if (stmt->u.i.in)
    {
      /* fifo_data = fifo.read(); */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data");
      if (kernel->sparse) {
        p = isl_printer_print_str(p, "_");
        p = isl_printer_print_str(p, group->array->name);    
      }
      p = isl_printer_print_str(p, " = ");
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 1);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 1);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
      if (kernel->sparse) {        
        /* [type] fifo_data = fifo_data_X; */
        if (is_sparse) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_s_t");
          p = isl_printer_print_int(p, group->n_lane);
          p = isl_printer_print_str(p, " fifo_data = fifo_data_");
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);          
        } else {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, group->n_lane);
          p = isl_printer_print_str(p, " fifo_data = fifo_data_");
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);          
        }
      }

      if (hls->target == XILINX_HW)
      {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int n = 0; n < ");
        if (is_sparse)
          p = isl_printer_print_int(p, group->n_lane * n_nzero);  
        else
          p = isl_printer_print_int(p, data_pack / nxt_data_pack);
        p = isl_printer_print_str(p, "; n++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "#pragma HLS UNROLL");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);
        isl_ast_expr *op;
        isl_ast_expr *expr = stmt->u.i.local_index;
        int n_arg = isl_ast_expr_op_get_n_arg(expr);
        /* Union */
        if (nxt_data_pack == 1)
        {
          /* union {unsigned int ui; float ut;} u; */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "union {unsigned int ui; ");
          p = isl_printer_print_str(p, group->array->type);
          p = isl_printer_print_str(p, " ut;} u;");
          p = isl_printer_end_line(p);
          /* u.ui = (unsigned int)fifo_data(32*next_data_pack - 1, 0); */
          p = isl_printer_start_line(p);
          if (kernel->sparse) {
            if (is_sparse) 
              p = isl_printer_print_str(p, "u.ui = (unsigned int)fifo_data.d(");
            else
              p = isl_printer_print_str(p, "u.ui = (unsigned int)fifo_data(");
          } else
            p = isl_printer_print_str(p, "u.ui = (unsigned int)fifo_data(");
          p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack - 1);
          p = isl_printer_print_str(p, ", 0);");
          p = isl_printer_end_line(p);
        }
        /* local[][n] = u.ut; or 
         * local[][n] = fifo_data(32*nxt_data_pack - 1, 0);
         */
        p = isl_printer_start_line(p);
        op = isl_ast_expr_op_get_arg(expr, 0);        
        if (kernel->sparse && group->local_array->is_sparse == 0 && group->local_array->array_type == AUTOSA_EXT_ARRAY) {
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_tmp");
        } else {
          p = isl_printer_print_ast_expr(p, op); // array_name
        }

        isl_ast_expr_free(op);
        for (int i = 0; i < n_arg - 1; i++)
        {
          op = isl_ast_expr_op_get_arg(expr, 1 + i);
          p = isl_printer_print_str(p, "[");
          if (i == n_arg - 2)
          {
            if (stmt->u.i.simd_depth != -1) {
              //DBGASTEXPR(stdout, op, ctx);
              p = isl_printer_print_ast_expr(p, op);
              p = isl_printer_print_str(p, " + n");
            } else {
              p = isl_printer_print_str(p, "n");
            }
          }
          else
          {
            p = isl_printer_print_ast_expr(p, op);
          }
          p = isl_printer_print_str(p, "]");
          isl_ast_expr_free(op);
        }
        p = isl_printer_print_str(p, " = ");
        if (nxt_data_pack == 1)
        {
          p = isl_printer_print_str(p, "u.ut;");
          p = isl_printer_end_line(p);
        }
        else
        {
          p = isl_printer_print_str(p, "fifo_data(");
          p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack - 1);
          p = isl_printer_print_str(p, ", 0)");
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }
        /* fifo_data = fifo_data >> 32*nxt_data_pack; */
        p = isl_printer_start_line(p);
        if (is_sparse)
          p = isl_printer_print_str(p, "fifo_data.d = fifo_data.d >> ");
        else
          p = isl_printer_print_str(p, "fifo_data = fifo_data >> ");            
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack);
        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, -2);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "}");
        p = isl_printer_end_line(p);
      }
      else if (hls->target == INTEL_HW)
      {
        isl_ast_expr *op;
        isl_ast_expr *expr = stmt->u.i.local_index;
        int n_arg = isl_ast_expr_op_get_n_arg(expr);
        for (int i = 0; i < data_pack / nxt_data_pack; i++)
        {
          /* local[][n] = fifo_data.sxxxx; */
          p = isl_printer_start_line(p);
          op = isl_ast_expr_op_get_arg(expr, 0);
          p = isl_printer_print_ast_expr(p, op); // array_name
          isl_ast_expr_free(op);
          for (int j = 0; j < n_arg - 1; j++)
          {
            op = isl_ast_expr_op_get_arg(expr, 1 + j);
            p = isl_printer_print_str(p, "[");
            if (j == n_arg - 2)
            {
              p = isl_printer_print_int(p, i);
            }
            else
            {
              p = isl_printer_print_ast_expr(p, op);
            }
            p = isl_printer_print_str(p, "]");
            isl_ast_expr_free(op);
          }
          if (nxt_data_pack > 1)
            p = isl_printer_print_str(p, ".data");
          p = isl_printer_print_str(p, " = fifo_data.data.s");
          for (int j = 0; j < nxt_data_pack; j++)
          {
            p = isl_printer_print_str(p, vector_index[j + i * nxt_data_pack]);
          }
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }
      } else if (hls->target == CATAPULT_HW) {
        p = print_str_new_line(p, "#pragma unroll yes");
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int n = 0; n < ");
        if (is_sparse)
          p = isl_printer_print_int(p, group->n_lane * n_nzero);  
        else
          p = isl_printer_print_int(p, data_pack / nxt_data_pack);
        p = isl_printer_print_str(p, "; n++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);
        isl_ast_expr *op;
        isl_ast_expr *expr = stmt->u.i.local_index;
        int n_arg = isl_ast_expr_op_get_n_arg(expr);
        /* local[][n] = fifo_data.slc(); */
        p = isl_printer_start_line(p);
        op = isl_ast_expr_op_get_arg(expr, 0);        
        if (kernel->sparse && group->local_array->is_sparse == 0 && group->local_array->array_type == AUTOSA_EXT_ARRAY) {
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_tmp");
        } else {
          p = isl_printer_print_ast_expr(p, op); // array_name
        }
        isl_ast_expr_free(op);
        for (int i = 0; i < n_arg - 1; i++) {
          op = isl_ast_expr_op_get_arg(expr, 1 + i);
          p = isl_printer_print_str(p, "[");
          if (i == n_arg - 2)
          {
            if (stmt->u.i.simd_depth != -1) {
              //DBGASTEXPR(stdout, op, ctx);
              p = isl_printer_print_ast_expr(p, op);
              p = isl_printer_print_str(p, " + n");
            } else {
              p = isl_printer_print_str(p, "n");
            }
          }
          else
          {
            p = isl_printer_print_ast_expr(p, op);
          }
          p = isl_printer_print_str(p, "]");
          isl_ast_expr_free(op);
        }
        p = isl_printer_print_str(p, " = (");
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, "_t");
        p = isl_printer_print_int(p, nxt_data_pack);
        p = isl_printer_print_str(p, ")fifo_data.slc<");
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack);
        p = isl_printer_print_str(p, ">(0);");        
        p = isl_printer_end_line(p);

        /* fifo_data = fifo_data >> xx * nxt_data_pack; */
        p = isl_printer_start_line(p);
        if (is_sparse)
          p = isl_printer_print_str(p, "fifo_data.d = fifo_data.d >> ");
        else
          p = isl_printer_print_str(p, "fifo_data = fifo_data >> ");      
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack);
        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, -2);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "}");
        p = isl_printer_end_line(p);  
      }

      if (kernel->sparse && group->local_array->is_sparse == 0) {
        /* Print the extra data selection code. */        
        int index_s, index_w;
        int pos_w;

        p = isl_printer_start_line(p);
        index_w = (int)log2f((float)group->n_lane);
        if (hls->target == XILINX_HW) {
          p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_int(p, index_w);
        } else if (hls->target == CATAPULT_HW) {
          p = isl_printer_print_str(p, "ac_int<");
          p = isl_printer_print_int(p, index_w);
          p = isl_printer_print_str(p, ", false");
        }
        p = isl_printer_print_str(p, "> index[");
        index_s = group->n_lane / kernel->vec_len * kernel->n_nzero;
        p = isl_printer_print_int(p, index_s);
        p = isl_printer_print_str(p, "];");
        p = isl_printer_end_line(p);

        if (hls->target == XILINX_HW) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=index dim=0 complete");
          p = isl_printer_end_line(p);
        }

        //p = print_str_new_line(p, "unsigned char index = 0;");
        
        p = isl_printer_start_line(p);
        struct autosa_local_array_info *sparse_array;
        for (int i = 0; i < kernel->n_array; i++) {
          sparse_array = &kernel->array[i];
          if (sparse_array->is_sparse)
            break;
        }
        p = isl_printer_print_str(p, sparse_array->array->name);
        p = isl_printer_print_str(p, "_s_t");
        p = isl_printer_print_int(p, group->n_lane / kernel->vec_len);
        p = isl_printer_print_str(p, " ");        
        p = isl_printer_print_str(p, "s_tmp = fifo_data_");
        p = isl_printer_print_str(p, sparse_array->array->name);
        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);

        pos_w = (int)log2f((float)index_s);
        p = isl_printer_start_line(p);
        if (hls->target == XILINX_HW) {
          p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_int(p, pos_w);
        } else if (hls->target == CATAPULT_HW) {
          p = isl_printer_print_str(p, "ac_int<");
          p = isl_printer_print_int(p, pos_w);          
          p = isl_printer_print_str(p, ", false");
        }
        p = isl_printer_print_str(p, "> pos = 0;");
        p = isl_printer_end_line(p);

        if (hls->target == CATAPULT_HW) {
          p = print_str_new_line(p, "#pragma unroll yes");
        }

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int n = 0; n < ");
        p = isl_printer_print_int(p, group->n_lane / kernel->vec_len);
        p = isl_printer_print_str(p, "; n++) {");
        p = isl_printer_end_line(p);

        if (hls->target == XILINX_HW) {
          p = print_str_new_line(p, "#pragma HLS UNROLL");
        }

        p = isl_printer_indent(p, 2);        
        p = print_str_new_line(p, "unsigned char offset = s_tmp.i(7, 0);");
        p = print_str_new_line(p, "s_tmp.i = s_tmp.i >> 8;");
        
        if (hls->target == CATAPULT_HW) {
          p = print_str_new_line(p, "#pragma unroll yes");
        }

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int m = 0; m < ");        
        p = isl_printer_print_int(p, kernel->vec_len);
        p = isl_printer_print_str(p, "; m++) {");
        p = isl_printer_end_line(p);

        //p = isl_printer_print_str(p, " * n; m < ");
        //p = isl_printer_print_int(p, kernel->vec_len);
        //p = isl_printer_print_str(p, " * n + ");
        //p = isl_printer_print_int(p, kernel->vec_len);
        //p = isl_printer_print_str(p, "; m++) {");
        
        if (hls->target == XILINX_HW) {
          p = print_str_new_line(p, "#pragma HLS UNROLL");
        }
        
        p = isl_printer_indent(p, 2);
        if (hls->target == XILINX_HW) {
          p = print_str_new_line(p, "if ((ap_uint<1>)(offset & 1) == (ap_uint<1>)1) {");
        } else if (hls->target == CATAPULT_HW) {
          p = print_str_new_line(p, "if ((ac_int<1, false>)(offset & 1) == (ac_int<1, false>)1) {");
        }
        p = isl_printer_indent(p, 2);
        
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "index[pos] = n * ");
        p = isl_printer_print_int(p, kernel->vec_len);
        p = isl_printer_print_str(p, " + m;");        
        p = isl_printer_end_line(p);

        p = print_str_new_line(p, "pos++;");

        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
        p = print_str_new_line(p, "offset = offset >> 1;");
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");

        if (hls->target == CATAPULT_HW) {
          p = print_str_new_line(p, "#pragma unroll yes");
        }

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int n = 0; n < ");
        p = isl_printer_print_int(p, group->n_lane / kernel->vec_len * kernel->n_nzero);
        p = isl_printer_print_str(p, "; n++) {");
        p = isl_printer_end_line(p);

        if (hls->target == XILINX_HW) {
          p = print_str_new_line(p, "#pragma HLS UNROLL");
        }

        p = isl_printer_indent(p, 2);
        p = isl_printer_start_line(p);
        isl_ast_expr *op;
        isl_ast_expr *expr = stmt->u.i.local_index;
        int n_arg = isl_ast_expr_op_get_n_arg(expr);
        op = isl_ast_expr_op_get_arg(expr, 0);
        p = isl_printer_print_ast_expr(p, op); // array_name;
        isl_ast_expr_free(op);
        for (int i = 0; i < n_arg - 1; i++) {
          op = isl_ast_expr_op_get_arg(expr, 1 + i);
          p = isl_printer_print_str(p, "[");
          if (i == n_arg - 2) {
            if (stmt->u.i.simd_depth != -1) {
              p = isl_printer_print_ast_expr(p, op);
              p = isl_printer_print_str(p, " + n");
            } else {
              p = isl_printer_print_str(p, "n");
            }
          } else {
            p = isl_printer_print_ast_expr(p, op);
          }
          p = isl_printer_print_str(p, "]");
          isl_ast_expr_free(op);
        }
        p = isl_printer_print_str(p, " = ");
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, "_tmp[0][index[n]];");
        p = isl_printer_end_line(p);
        
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
      }
    }
    else
    {
      if (hls->target == XILINX_HW)
      {
        if (kernel->sparse) {
          p = isl_printer_start_line(p);
          p = print_fifo_rw_xilinx(p, fifo_name, 0);
          p = isl_printer_print_str(p, "fifo_data_");
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, ");");
          p = isl_printer_end_line(p);
        } else {
          if (nxt_data_pack == 1)
          {
            /* union {unsigned int ui; float ut;} u1, u0; */
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "union {unsigned int ui; ");
            p = isl_printer_print_str(p, group->array->type);
            p = isl_printer_print_str(p, " ut;} ");
            int first = 1;
            for (int i = data_pack / nxt_data_pack - 1; i >= 0; i--)
            {
              if (!first)
                p = isl_printer_print_str(p, ", ");
              p = isl_printer_print_str(p, "u");
              p = isl_printer_print_int(p, i);
              first = 0;
            }
            p = isl_printer_print_str(p, ";");
            p = isl_printer_end_line(p);
            /* u1 = local[][1];
             * u0 = local[][0];
             */
            for (int i = data_pack / nxt_data_pack - 1; i >= 0; i--)
            {
              isl_ast_expr *expr = stmt->u.i.local_index;
              isl_ast_expr *op;
              int n_arg = isl_ast_expr_op_get_n_arg(expr);
              p = isl_printer_start_line(p);
              p = isl_printer_print_str(p, "u");
              p = isl_printer_print_int(p, i);
              p = isl_printer_print_str(p, ".ut = ");
              op = isl_ast_expr_op_get_arg(expr, 0);
              p = isl_printer_print_ast_expr(p, op);
              isl_ast_expr_free(op);
              for (int j = 0; j < n_arg - 1; j++)
              {
                op = isl_ast_expr_op_get_arg(expr, 1 + j);
                p = isl_printer_print_str(p, "[");
                if (j == n_arg - 2)
                {
                  if (stmt->u.i.simd_depth != -1) {
                    p = isl_printer_print_ast_expr(p, op);
                    p = isl_printer_print_str(p, " + ");
                  }
                  p = isl_printer_print_int(p, i);
                }
                else
                {
                  p = isl_printer_print_ast_expr(p, op);
                }
                p = isl_printer_print_str(p, "]");
                isl_ast_expr_free(op);
              }
              p = isl_printer_print_str(p, ";");
              p = isl_printer_end_line(p);
            }
          }
          /* fifo_data = (ap_uint<32*nxt_data_pack>(u1.ui), 
           *              ap_uint<32*nxt_data_pack>(u0.ui)); */
          int first = 1;
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "fifo_data = (");
          for (int i = data_pack / nxt_data_pack - 1; i >= 0; i--)
          {
            isl_ast_expr *expr = stmt->u.i.local_index;
            isl_ast_expr *op;
            int n_arg = isl_ast_expr_op_get_n_arg(expr);
            if (!first)
              p = isl_printer_print_str(p, ", ");
            if (nxt_data_pack == 1)
            {
              p = isl_printer_print_str(p, "ap_uint<");
              p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack);
              p = isl_printer_print_str(p, ">(u");
              p = isl_printer_print_int(p, i);
              p = isl_printer_print_str(p, ".ui)");
            }
            else
            {
              op = isl_ast_expr_op_get_arg(expr, 0);
              p = isl_printer_print_ast_expr(p, op);
              isl_ast_expr_free(op);
              for (int j = 0; j < n_arg - 1; j++)
              {
                op = isl_ast_expr_op_get_arg(expr, 1 + j);
                p = isl_printer_print_str(p, "[");
                if (j == n_arg - 2)
                {
                  p = isl_printer_print_int(p, i);
                }
                else
                {
                  p = isl_printer_print_ast_expr(p, op);
                }
                p = isl_printer_print_str(p, "]");
                isl_ast_expr_free(op);
              }
            }
            first = 0;
          }
          p = isl_printer_print_str(p, ");");
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
          p = print_fifo_rw_xilinx(p, fifo_name, 0);
          p = isl_printer_print_str(p, "fifo_data);");
          p = isl_printer_end_line(p);
        }
      }
      else if (hls->target == INTEL_HW)
      {
        /* fifo_data = (float4)((float2)local[][1], (float2)local[][0]); */
        int first = 1;
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "fifo_data.data = (");
        if (data_pack == 1)
        {
          p = isl_printer_print_str(p, group->array->type);
        }
        else
        {
          //p = isl_printer_print_str(p, group->array->name);
          //p = isl_printer_print_str(p, "_t");
          //p = isl_printer_print_int(p, data_pack);
          p = isl_printer_print_str(p, group->array->type);
          p = isl_printer_print_int(p, data_pack);
        }
        p = isl_printer_print_str(p, ")(");
        //for (int i = data_pack / nxt_data_pack - 1; i >= 0; i--)
        for (int i = 0; i < data_pack / nxt_data_pack; i++)
        {
          isl_ast_expr *expr = stmt->u.i.local_index;
          isl_ast_expr *op;
          int n_arg = isl_ast_expr_op_get_n_arg(expr);
          if (!first)
            p = isl_printer_print_str(p, ", ");
          p = isl_printer_print_str(p, "(");
          if (nxt_data_pack == 1)
          {
            p = isl_printer_print_str(p, group->array->type);
          }
          else
          {
            p = isl_printer_print_str(p, group->array->name);
            p = isl_printer_print_str(p, "_t");
            p = isl_printer_print_int(p, nxt_data_pack);
          }
          p = isl_printer_print_str(p, ")");
          op = isl_ast_expr_op_get_arg(expr, 0);
          p = isl_printer_print_ast_expr(p, op);
          isl_ast_expr_free(op);
          for (int j = 0; j < n_arg - 1; j++)
          {
            op = isl_ast_expr_op_get_arg(expr, 1 + j);
            p = isl_printer_print_str(p, "[");
            if (j == n_arg - 2)
            {
              p = isl_printer_print_int(p, i);
            }
            else
            {
              p = isl_printer_print_ast_expr(p, op);
            }
            p = isl_printer_print_str(p, "]");
            isl_ast_expr_free(op);
            if (nxt_data_pack > 1)
              p = isl_printer_print_str(p, ".data");
          }
          first = 0;
        }
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
        /* write_channel_intel(fifo, fifo_data); */
        p = isl_printer_start_line(p);
        p = print_fifo_rw_intel(p, fifo_name, 0);
        p = isl_printer_print_str(p, "fifo_data);");
        p = isl_printer_end_line(p);
      } else if (hls->target == CATAPULT_HW) {
        if (kernel->sparse) {
          p = isl_printer_start_line(p);
          p = print_fifo_rw_catapult(p, fifo_name, 0);          
          p = isl_printer_print_str(p, "fifo_data_");
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, ");");
          p = isl_printer_end_line(p);
        } else {          
          for (int i = data_pack / nxt_data_pack - 1; i >= 0; i--) {
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "fifo_data.set_slc(");
            p = isl_printer_print_int(p, group->array->size * 8 * nxt_data_pack * i);
            p = isl_printer_print_str(p, ", ");

            isl_ast_expr *expr = stmt->u.i.local_index;
            isl_ast_expr *op;
            int n_arg = isl_ast_expr_op_get_n_arg(expr);
            op = isl_ast_expr_op_get_arg(expr, 0);
            p = isl_printer_print_ast_expr(p, op);
            isl_ast_expr_free(op);
            for (int j = 0; j < n_arg - 1; j++)
            {
              op = isl_ast_expr_op_get_arg(expr, 1 + j);
              p = isl_printer_print_str(p, "[");
              if (j == n_arg - 2)
              {
                p = isl_printer_print_int(p, i);
              }
              else
              {
                p = isl_printer_print_ast_expr(p, op);
              }
              p = isl_printer_print_str(p, "]");
              isl_ast_expr_free(op);
            }
            p = isl_printer_print_str(p, ");");
            p = isl_printer_end_line(p);
          }

          p = isl_printer_start_line(p);
          p = print_fifo_rw_catapult(p, fifo_name, 0);
          p = isl_printer_print_str(p, "fifo_data);");
          p = isl_printer_end_line(p);
        }
      }
    }
    p = ppcg_end_block(p);
  }
  
  free(fifo_name);
  isl_ast_expr_free(local_index_packed);  
  return p;
}

__isl_give isl_printer *autosa_print_reduce_data_pack(
  __isl_take isl_printer *p,
  struct autosa_kernel_stmt *stmt,
  int data_pack_in,
  int data_pack_out,
  struct autosa_array_ref_group *group,
  enum platform target
  )
{  
  p = print_str_new_line(p, "/* Local Reduction */");

  if (target == XILINX_HW) {
    /* union {unsigned int ui; data_t uf;} uin_0, uin_1, ... uout_0, uout_1, ...; */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "union {unsigned int ui; ");
    p = isl_printer_print_str(p, group->array->type);
    p = isl_printer_print_str(p, " ut;} ");
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_print_str(p, "uin_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ", ");
    }
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);
      if (i == data_pack_in - 1) {
        p = isl_printer_print_str(p, ";");
      } else {
        p = isl_printer_print_str(p, ", ");
      }
    }
    p = isl_printer_end_line(p);

    /* assign the fifo_data and buf_data_split[split_i] to union vars. */
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "uin_");
      p = isl_printer_print_int(p, i);
      if (data_pack_in == 1) {
        p = isl_printer_print_str(p, ".ut = in_data;");
      } else {
        p = isl_printer_print_str(p, ".ui = (unsigned int)in_data(");
        p = isl_printer_print_int(p, group->array->size * 8 * (i + 1) - 1);
        p = isl_printer_print_str(p, ", ");
        p = isl_printer_print_int(p, group->array->size * 8 * i);
        p = isl_printer_print_str(p, ");");
      }
      p = isl_printer_end_line(p);
    }
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);    
      p = isl_printer_print_str(p, ".ui = (unsigned int)data_split[split_idx](");
      p = isl_printer_print_int(p, group->array->size * 8 * (i + 1) - 1);
      p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_int(p, group->array->size * 8 * i);
      p = isl_printer_print_str(p, ");");    
      p = isl_printer_end_line(p);
    }

    /* perform reduction. */
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ".ut ");
      p = isl_printer_print_str(p, stmt->u.i.reduce_op);
      p = isl_printer_print_str(p, "= ");
      p = isl_printer_print_str(p, "uin_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ".ut;");
      p = isl_printer_end_line(p);
    }

    /* re-assign the reduced values to the buf_data_split[i]. */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "data_split[split_idx] = ");
    p = isl_printer_print_str(p, "(");
    for (int i = data_pack_in - 1; i >= 0; i--) {    
      if (i != data_pack_in - 1)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "(ap_uint<");
      p = isl_printer_print_int(p, group->array->size * 8);
      p = isl_printer_print_str(p, ">)");
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ".ui");
    }
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);
  } else if (target == CATAPULT_HW) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, group->array->name);
    p = isl_printer_print_str(p, "_t1 ");
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_print_str(p, "uin_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ", ");
    }
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);
      if (i == data_pack_in - 1) {
        p = isl_printer_print_str(p, ";");
      } else {
        p = isl_printer_print_str(p, ", ");
      }
    }
    p = isl_printer_end_line(p);

    /* assign the fifo_data and buf_data_split[split_i] to vars. */
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "uin_");
      p = isl_printer_print_int(p, i);      
      if (data_pack_in == 1) {
        p = isl_printer_print_str(p, " = in_data");      
      } else {
        p = isl_printer_print_str(p, " = in_data.slc<");
        p = isl_printer_print_int(p, group->array->size * 8);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, group->array->size * 8 * i);
        p = isl_printer_print_str(p, ");");
      }
      p = isl_printer_end_line(p);
    }
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, " = data_split[split_idx].slc<");
      p = isl_printer_print_int(p, group->array->size * 8);
      p = isl_printer_print_str(p, ">(");
      p = isl_printer_print_int(p, group->array->size * 8 * i);
      p = isl_printer_print_str(p, ");");
      p = isl_printer_end_line(p);
    }

    /* perform reduction */
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "uout_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, " ");
      p = isl_printer_print_str(p, stmt->u.i.reduce_op);
      p = isl_printer_print_str(p, "= ");
      p = isl_printer_print_str(p, "uin_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }

    /* re-assign the reduced values to the buf_data_split[i]. */
    for (int i = 0; i < data_pack_in; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "data_split[split_idx].set_slc(");
      p = isl_printer_print_int(p, group->array->size * 8 * i);
      p = isl_printer_print_str(p, ", uout_");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, ")");
    }    
  }

  p = print_str_new_line(p, "/* Local Reduction */");

  return p;
}

__isl_give isl_printer *autosa_print_reduce_default(
  __isl_take isl_printer *p,
  struct autosa_kernel_stmt *stmt,
  int data_pack,
  isl_ast_expr *index,
  struct autosa_array_ref_group *group)
{
  p = print_str_new_line(p, "/* Local Reduction */");

  /* union {unsigned int ui; data_t ut;} u... */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "union {unsigned int ui; ");
  p = isl_printer_print_str(p, group->array->type);
  p = isl_printer_print_str(p, " ut;} ");
  for (int i = 0; i < data_pack; i++) {
    p = isl_printer_print_str(p, "uin_");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, ", ");
  }
  for (int i = 0; i < data_pack; i++) {
    p = isl_printer_print_str(p, "uout_");
    p = isl_printer_print_int(p, i);
    if (i == data_pack - 1) {
      p = isl_printer_print_str(p, ";");
    } else {
      p = isl_printer_print_str(p, ", ");
    }
  }
  p = isl_printer_end_line(p);

  /* assign fifo_data to uxx, assign local_data to uxx. */
  for (int i = 0; i < data_pack; i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "uin_");
    p = isl_printer_print_int(p, i);
    if (data_pack == 1) {
      p = isl_printer_print_str(p, ".ut = in_data;");
    } else {
      p = isl_printer_print_str(p, ".ui = (unsigned int)in_data(");
      p = isl_printer_print_int(p, group->array->size * 8 * (i + 1) - 1);
      p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_int(p, group->array->size * 8 * i);
      p = isl_printer_print_str(p, ");");
    }
    p = isl_printer_end_line(p);
  }
  for (int i = 0; i < data_pack; i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "uout_");
    p = isl_printer_print_int(p, i);    
    if (data_pack == 1) {
      p = isl_printer_print_str(p, ".ut = ");
      if (stmt->u.i.module->double_buffer &&
          stmt->u.i.module->options->autosa->double_buffer_style == 0)
        throw std::runtime_error("[AutoSA] Error: Local reduce for double buffer style 0 is not supported!");
      else {        
        p = isl_printer_print_ast_expr(p, index);
      }
      p = isl_printer_print_str(p, ";");      
    } else {
      p = isl_printer_print_str(p, ".ui = (unsigned int)");
      p = isl_printer_print_ast_expr(p, index);
      p = isl_printer_print_str(p, "(");
      p = isl_printer_print_int(p, group->array->size * 8 * (i + 1) - 1);
      p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_int(p, group->array->size * 8 * i);
      p = isl_printer_print_str(p, ");");
    }
    p = isl_printer_end_line(p);
  }

  /* perform reduction. */
  for (int i = 0; i < data_pack; i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "uout_");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, ".ut ");
    p = isl_printer_print_str(p, stmt->u.i.reduce_op);
    p = isl_printer_print_str(p, "= ");
    p = isl_printer_print_str(p, "uin_");
    p = isl_printer_print_int(p, i);
    p = isl_printer_print_str(p, ".ut;");
    p = isl_printer_end_line(p);
  }

  /* reassign uxx to local[][] */
  p = isl_printer_start_line(p);
  //p = isl_printer_print_ast_expr(p, index);
  p = isl_printer_print_str(p, "out_data");
  p = isl_printer_print_str(p, " = ");
  if (data_pack == 1) {
    p = isl_printer_print_str(p, "uout_0.ut;");    
  } else {
    p = isl_printer_print_str(p, "(");
    int is_first = 1;
    for (int i = data_pack - 1; i >= 0; i--) {
      if (!is_first)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "(ap_uint<");
      p = isl_printer_print_int(p, group->array->size * 8);
      p = isl_printer_print_str(p, ">)uout_");
      p = isl_printer_print_int(p, i);   
      p = isl_printer_print_str(p, ".ui");
      is_first = 0;
    }
    p = isl_printer_print_str(p, ");");
  }
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "/* Local Reduction */");

  return p;
}

/* Print an I/O transfer statement.
 *
 * An in I/O statement is printed as
 *
 *  [type] fifo_data;
 *  fifo_data = fifo.read();
 *  if (filter_condition) {
 *    local[] = fifo_data; // if buf == 1
 *    fifo_local.write(fifo_data); // if buf == 0
 *  } else {
 *    fifo.write(fifo_data);
 *  }
 *
 * if filter_depth < 0
 *
 *  [type] fifo_data;
 *  fifo_data = fifo.read();
 *  local = fifo_data; // if buf == 1
 *  fifo_local.write(fifo_data); // if buf == 0
 *
 * An out I/O statement is printed as 
 *
 *  [type] fifo_data;
 *  fifo_data = fifo.read();
 *  if (filter_condition) {
 *    fifo_data = local[]; // if buf == 1
 *    fifo_data = fifo_local.read(); // if buf == 0
 *  } else {
 *    fifo_data = fifo.read();
 *  }
 *  fifo.write(fifo_data);
 */
static __isl_give isl_printer *autosa_kernel_print_io_transfer_default(
    __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,
    struct autosa_array_ref_group *group, int n_lane, struct hls_info *hls,
    const char *iterator_prefix)
{
  isl_ctx *ctx;
  char *fifo_name;
  ctx = isl_printer_get_ctx(p);
  int boundary = stmt->u.i.boundary;
  /* If the statement is a boundary statement, 
   * then ignore the filter condition by setting filter_sched_depth as -1
   */
  if (boundary)
    stmt->u.i.filter_sched_depth = -1;

  isl_ast_expr *local_index_packed;
  isl_ast_expr *arg, *div;
  local_index_packed = isl_ast_expr_copy(stmt->u.i.local_index);
  int n_arg;
  /* Extract the sparse data */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  /* Modify the local index. */
  if (is_sparse) {
    n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
    arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
    div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, vec_len * n_lane));
    arg = isl_ast_expr_div(arg, div);
    local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
  } else {
    if (n_lane > 1)
    {
      n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
      arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
      div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, n_lane));
      arg = isl_ast_expr_div(arg, div);
      local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
    }
  }

  /* Declare the fifo data variable. */
  p = isl_printer_start_line(p);
  if (is_sparse) {
    p = autosa_print_array_type_with_lane_sparse(p, group->array, n_lane);
  } else {
    //if (n_lane == 1) {
    //  p = isl_printer_print_str(p, stmt->u.i.array->type);
    //} else {
      p = isl_printer_print_str(p, stmt->u.i.array->name);
      if (group->local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, n_lane);
    //}
  }
  p = isl_printer_print_str(p, " fifo_data;");
  p = isl_printer_end_line(p);

  if (stmt->u.i.in)
  {            
    fifo_name = concat(ctx, stmt->u.i.in_fifo_name, "in");
    /* fifo_data = fifo.read(); */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "fifo_data");
    p = isl_printer_print_str(p, " = ");
    if (hls->target == XILINX_HW)
      p = print_fifo_rw_xilinx(p, fifo_name, 1);
    else if (hls->target == INTEL_HW)
      p = print_fifo_rw_intel(p, fifo_name, 1);
    else if (hls->target == CATAPULT_HW)
      p = print_fifo_rw_catapult(p, fifo_name, 1);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
    free(fifo_name);

    if (stmt->u.i.buf)
    {
      /* local[][] = fifo_data; */
      if (stmt->u.i.reduce) {
        p = autosa_print_reduce_default(p, stmt, n_lane, local_index_packed, group);
      } else {
        p = isl_printer_start_line(p);
        //p = isl_printer_print_ast_expr(p, local_index_packed);
        if (stmt->u.i.module->double_buffer && 
            stmt->u.i.module->options->autosa->double_buffer_style == 0)
        {
          isl_ast_expr *op;
          op = isl_ast_expr_op_get_arg(local_index_packed, 0);
          p = isl_printer_print_ast_expr(p, op);
          isl_ast_expr_free(op);
          p = isl_printer_print_str(p, "[arb]");
          for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
            op = isl_ast_expr_op_get_arg(local_index_packed, n);
            p = isl_printer_print_str(p, "[");
            p = isl_printer_print_ast_expr(p, op);
            p = isl_printer_print_str(p, "]");
            isl_ast_expr_free(op);
          }
        } 
        else 
        {
          if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
            isl_ast_expr *op;
            op = isl_ast_expr_op_get_arg(local_index_packed, 0);
            p = isl_printer_print_ast_expr(p, op);    
            isl_ast_expr_free(op);
            p = isl_printer_print_str(p, "_tmp.data");
            for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
              op = isl_ast_expr_op_get_arg(local_index_packed, n);
              p = isl_printer_print_str(p, "[");
              p = isl_printer_print_ast_expr(p, op);
              p = isl_printer_print_str(p, "]");
              isl_ast_expr_free(op);
            }
          } else {
            p = isl_printer_print_ast_expr(p, local_index_packed);
          }
        }
        p = isl_printer_print_str(p, " ");
        if (stmt->u.i.reduce) {        
          p = isl_printer_print_str(p, stmt->u.i.reduce_op);
          // TODO: what if the data pack factor is greater than 1?
        }         
        p = isl_printer_print_str(p, "= fifo_data;");
        p = isl_printer_end_line(p);
      }
    }
    else
    {
      /* fifo.write(fifo_data); */          
      fifo_name = concat(ctx, stmt->u.i.out_fifo_name, "out");      
      p = isl_printer_start_line(p);
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 0);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 0);
      p = isl_printer_print_str(p, "fifo_data);");
      p = isl_printer_end_line(p);
      free(fifo_name);
    }
  }
  else
  {    
    if (stmt->u.i.buf)
    {
      /* fifo_data = local[][]; */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      if (stmt->u.i.module->double_buffer && 
          stmt->u.i.module->options->autosa->double_buffer_style == 0) {      
        isl_ast_expr *op;
        op = isl_ast_expr_op_get_arg(local_index_packed, 0);
        p = isl_printer_print_ast_expr(p, op);
        isl_ast_expr_free(op);
        p = isl_printer_print_str(p, "[!arb]");
        for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
          op = isl_ast_expr_op_get_arg(local_index_packed, n);
          p = isl_printer_print_str(p, "[");
          p = isl_printer_print_ast_expr(p, op);
          p = isl_printer_print_str(p, "]");
          isl_ast_expr_free(op);
        }
      } else {
        if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
          isl_ast_expr *op;
          op = isl_ast_expr_op_get_arg(local_index_packed, 0);
          p = isl_printer_print_ast_expr(p, op);    
          isl_ast_expr_free(op);
          p = isl_printer_print_str(p, "_tmp.data");
          for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
            op = isl_ast_expr_op_get_arg(local_index_packed, n);
            p = isl_printer_print_str(p, "[");
            p = isl_printer_print_ast_expr(p, op);
            p = isl_printer_print_str(p, "]");
            isl_ast_expr_free(op);
          }
        } else {
          p = isl_printer_print_ast_expr(p, local_index_packed);
        }
      }
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }
    else
    {
      /* fifo_data = fifo.read(); */            
      fifo_name = concat(ctx, stmt->u.i.in_fifo_name, "in");      
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 1);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 1);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
      free(fifo_name);
    }

    /* fifo.write(fifo_data); */
    fifo_name = concat(ctx, stmt->u.i.out_fifo_name, "out");
    p = isl_printer_start_line(p);
    if (hls->target == XILINX_HW)
      p = print_fifo_rw_xilinx(p, fifo_name, 0);
    else if (hls->target == INTEL_HW)
      p = print_fifo_rw_intel(p, fifo_name, 0);
    else if (hls->target == CATAPULT_HW)
      p = print_fifo_rw_catapult(p, fifo_name, 0);
    p = isl_printer_print_str(p, "fifo_data);");
    p = isl_printer_end_line(p);
    free(fifo_name);
  }

  isl_ast_expr_free(local_index_packed);

  return p;
}

/* Print an access to the element in the global memory copy
 * described by "stmt".  The index of the copy is recorded in
 * stmt->index as an access to the array.
 * If "serialize" is set, we will simply print array[i++];
 */
static __isl_give isl_printer *io_stmt_print_global_index(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt, int serialize)
{
  struct autosa_array_info *array = stmt->u.i.array;
  isl_ast_expr *index;

  if (autosa_array_is_scalar(array))
  {
    if (!autosa_array_is_read_only_scalar(array))
      p = isl_printer_print_str(p, "*");
    p = isl_printer_print_str(p, array->name);
    return p;
  }

  index = isl_ast_expr_copy(stmt->u.i.index);
  if (!serialize) {    
    p = isl_printer_print_ast_expr(p, index);
  } else {    
    isl_ast_expr *array_name;
    array_name = isl_ast_expr_op_get_arg(index, 0);
    p = isl_printer_print_ast_expr(p, array_name);
    p = isl_printer_print_str(p, "[i]");    
    isl_ast_expr_free(array_name);
  }
  isl_ast_expr_free(index);

  return p;
}

static __isl_give isl_printer *io_stmt_print_index_last_dim(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt, 
  int serialize, int global, int n_lane, int nxt_n_lane, int is_sparse, int vec_len)
{
  struct autosa_array_info *array = stmt->u.i.array;
  isl_ast_expr *index;

  if (autosa_array_is_scalar(array))
  {
    if (!autosa_array_is_read_only_scalar(array))
      p = isl_printer_print_str(p, "0");    
    return p;
  }

  if (global)
    index = isl_ast_expr_copy(stmt->u.i.index);
  else 
    index = isl_ast_expr_copy(stmt->u.i.local_index);

  if (!serialize) {    
    isl_ast_expr *op;
    int n_arg, r;
    isl_val *val;
    isl_ctx *ctx = isl_printer_get_ctx(p);

    n_arg = isl_ast_expr_op_get_n_arg(index);
    op = isl_ast_expr_op_get_arg(index, n_arg - 1);
    r = n_lane / nxt_n_lane;    
    if (is_sparse) 
      val = isl_val_int_from_si(ctx, vec_len * nxt_n_lane);
    else
      val = isl_val_int_from_si(ctx, nxt_n_lane);        
    op = isl_ast_expr_div(op, isl_ast_expr_from_val(val));        
    if (global) {
      op = isl_ast_expr_mul(op, isl_ast_expr_from_val(isl_val_int_from_si(ctx, n_lane)));
    }
    p = isl_printer_print_ast_expr(p, op);

    isl_ast_expr_free(op);    
  } else {        
    p = isl_printer_print_str(p, "i");        
  }
  isl_ast_expr_free(index);

  return p;  
}

/* A list of helper functions for autosa_kernel_print_io_transfer */
/* update_data_split: data_split[split_i] = in_data; */
static __isl_give isl_printer *io_transfer_update_data_split(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix)
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;

  if (hls->target == XILINX_HW || hls->target == CATAPULT_HW || 
    (hls->target == INTEL_HW && nxt_n_lane > 1)) {
    if (stmt->u.i.reduce) {
      //if (n_lane == nxt_n_lane)
      //  p = autosa_print_reduce_default(p, stmt, n_lane, local_index_packed, group);
      //else
      p = autosa_print_reduce_data_pack(p, stmt, nxt_n_lane, n_lane, group, hls->target); // TODO
    } else {
      if (hls->target == XILINX_HW) {
        if (nxt_n_lane == 1) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "union {unsigned int ui; ");
          p = isl_printer_print_str(p, group->array->type);
          p = isl_printer_print_str(p, " ut;} u;");
          p = isl_printer_end_line(p);

          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "u.ut = in_data;");
          p = isl_printer_end_line(p);
        }
      }

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "data_split[split_idx] ");
      if (stmt->u.i.reduce) {
        p = isl_printer_print_str(p, stmt->u.i.reduce_op);
      }
      p = isl_printer_print_str(p, "= ");

      if (hls->target == XILINX_HW) {
        if (nxt_n_lane == 1) {
          p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_int(p, group->array->size * 8);
          p = isl_printer_print_str(p, ">(u.ui);");
        } else {
          p = isl_printer_print_str(p, "in_data;");
        }
      } else {
        p = isl_printer_print_str(p, "in_data;");
      }
      p = isl_printer_end_line(p);
    }
  }

  return p;
}

static __isl_give isl_printer *io_transfer_pack_out_data(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix) 
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;

  p = isl_printer_start_line(p);
  if (hls->target == XILINX_HW) {
    int first = 1;
    p = isl_printer_print_str(p, "out_data = (");
    for (int i = n_lane / nxt_n_lane - 1; i >= 0; i--) {
      if (!first)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "data_split[");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, "]");
        first = 0;
    }
    p = isl_printer_print_str(p, ");");
  } else if (hls->target == INTEL_HW) {
    if (nxt_n_lane == 1) {
      p = isl_printer_print_str(p, "out_data.data[split_idx] = in_data;");
    } else {
      int first = 1;
      p = isl_printer_print_str(p, "out_data.data = ");
      p = isl_printer_print_str(p, "(");
      p = isl_printer_print_str(p, group->array->type);
      p = isl_printer_print_int(p, n_lane);
      p = isl_printer_print_str(p, ")(");
      for (int i = 0; i < n_lane / nxt_n_lane; i++) {
        if (!first)
          p = isl_printer_print_str(p, ", ");
        if (nxt_n_lane > 1) {
          p = isl_printer_print_str(p, "(");
          p = isl_printer_print_str(p, group->array->type);
          p = isl_printer_print_int(p, nxt_n_lane);
          p = isl_printer_print_str(p, ")");
        }
        p = isl_printer_print_str(p, "data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "]");
        if (nxt_n_lane > 1) {
          p = isl_printer_print_str(p, ".data");
        }
        first = 0;
      }
      p = isl_printer_print_str(p, ");");
    }
  } else if (hls->target == CATAPULT_HW) {
    for (int i = 0; i < n_lane / nxt_n_lane; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "out_data.set_slc(");
      p = isl_printer_print_int(p, i * group->array->size * 8 * nxt_n_lane);
      p = isl_printer_print_str(p, ", data_split[");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, "]);");
      p = isl_printer_end_line(p);  
    }
  }
  p = isl_printer_end_line(p);

  return p;
}

static __isl_give isl_printer *io_transfer_read_local_buf(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix, isl_ast_expr *local_index_packed) 
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "out_data = ");
  if (stmt->u.i.module->double_buffer && 
    stmt->u.i.module->options->autosa->double_buffer_style == 0) {
    isl_ast_expr *op;
    op = isl_ast_expr_op_get_arg(local_index_packed, 0);
    p = isl_printer_print_ast_expr(p, op);    
    isl_ast_expr_free(op);
    p = isl_printer_print_str(p, stmt->u.i.in? "[arb]" : "[!arb]");
    for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
      op = isl_ast_expr_op_get_arg(local_index_packed, n);
      p = isl_printer_print_str(p, "[");
      p = isl_printer_print_ast_expr(p, op);
      p = isl_printer_print_str(p, "]");
      isl_ast_expr_free(op);
    }
  } else {
    if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
      isl_ast_expr *op;
      op = isl_ast_expr_op_get_arg(local_index_packed, 0);
      p = isl_printer_print_ast_expr(p, op);    
      isl_ast_expr_free(op);
      p = isl_printer_print_str(p, "_tmp.data");
      for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
        op = isl_ast_expr_op_get_arg(local_index_packed, n);
        p = isl_printer_print_str(p, "[");
        p = isl_printer_print_ast_expr(p, op);
        p = isl_printer_print_str(p, "]");
        isl_ast_expr_free(op);
      }
    } else {
      p = isl_printer_print_ast_expr(p, local_index_packed);
    }
  }
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);

  return p;  
}

static __isl_give isl_printer *io_transfer_parse_sparse_data(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix) 
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;

  /* Extract the sparse data. */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  /* [type_n_lane] buf_data_d = buf_data.d; */
  p = isl_printer_start_line(p);
  p = autosa_print_array_type_with_lane(p, group->array, n_lane * n_nzero);
  p = isl_printer_print_str(p, " out_data_d = out_data.d;");
  p = isl_printer_end_line(p);

  /* [type_n_lane] buf_data_i = buf_data.i; */
  p = isl_printer_start_line(p);
  if (hls->target == XILINX_HW) {
    p = isl_printer_print_str(p, "ap_uint<");
    p = isl_printer_print_int(p, 8 * n_lane);
  } else if (hls->target == CATAPULT_HW) {
    p = isl_printer_print_str(p, "ac_int<");
    p = isl_printer_print_int(p, 8 * n_lane);
    p = isl_printer_print_str(p, ", false");
  }
  p = isl_printer_print_str(p, "> out_data_i = out_data.i;");
  p = isl_printer_end_line(p); 

  return p;
}

static __isl_give isl_printer *io_transfer_write_data_split(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix, const char *data_str) 
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;

  /* Extract the sparse data. */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  if (hls->target == XILINX_HW) {    
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int n = 0; n < ");
    p = isl_printer_print_int(p, n_lane / nxt_n_lane);
    p = isl_printer_print_str(p, "; n++) {");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma HLS UNROLL");
    p = isl_printer_end_line(p);    
    p = isl_printer_indent(p, 2);

    if (is_sparse) {
      /* data_split[n] = {out_data_d(), ...} */    
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "data_split[n] = (");
      p = isl_printer_print_str(p, group->array->name);
      p = isl_printer_print_str(p, "_s_t");
      p = isl_printer_print_int(p, nxt_n_lane);
      p = isl_printer_print_str(p, "){");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "_d(");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane * n_nzero - 1);
      p = isl_printer_print_str(p, ", 0), ");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "_i(");
      p = isl_printer_print_int(p, 8 * nxt_n_lane - 1);
      p = isl_printer_print_str(p, ", 0)};");
      p = isl_printer_end_line(p);      

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "_d = ");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "_d >> ");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane * n_nzero);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "_i = ");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "_i >> ");
      p = isl_printer_print_int(p, 8 * nxt_n_lane);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    } else {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "data_split[n] = ");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, "(");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane - 1);
      p = isl_printer_print_str(p, ", 0);");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, " = ");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, " >> ");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }

    p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "}");
    p = isl_printer_end_line(p);
  }
  else if (hls->target == INTEL_HW && nxt_n_lane > 1) {    
    for (int i = 0; i < n_lane / nxt_n_lane; i++) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "data_split[");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, "]");
      if (nxt_n_lane > 1)
        p = isl_printer_print_str(p, ".data");
      p = isl_printer_print_str(p, " = ");
      p = isl_printer_print_str(p, data_str);
      p = isl_printer_print_str(p, ".data.s");
      for (int j = 0; j < nxt_n_lane; j++) {
        p = isl_printer_print_str(p, vector_index[j + i * nxt_n_lane]);
      }
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }    
  }
  else if (hls->target == CATAPULT_HW) {
    for (int i = 0; i < n_lane / nxt_n_lane; i++) {
      if (is_sparse) {
        /* data_split[].set_slc(0, out_data_i.slc<>()); */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "].set_slc(0, ");
        p = isl_printer_print_str(p, data_str);
        p = isl_printer_print_str(p, "_i.slc<");
        p = isl_printer_print_int(p, 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, i * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, "));");
        p = isl_printer_end_line(p);

        /* data_split[].set_slc(xx, out_data_d.slc<>()); */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "].set_slc(");
        p = isl_printer_print_int(p, 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ", ");
        p = isl_printer_print_str(p, data_str);
        p = isl_printer_print_str(p, "_d.slc<");        
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane * n_nzero);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, i * group->array->size * 8 * nxt_n_lane * n_nzero);
        p = isl_printer_print_str(p, "));");
        p = isl_printer_end_line(p);
      } else {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "] = ");
        p = isl_printer_print_str(p, data_str);
        p = isl_printer_print_str(p, ".slc<");        
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, i * group->array->size * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
  }      

  return p;
}

static __isl_give isl_printer *io_transfer_read_data_split(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix) 
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;

  /* Extract the sparse data. */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  if (is_sparse) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "out_data = data_split[split_idx];");
    p = isl_printer_end_line(p);
  } else {
    if (hls->target == XILINX_HW) {
      if (nxt_n_lane == 1) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "union {unsigned int ui; ");
        p = isl_printer_print_str(p, group->array->type);
        p = isl_printer_print_str(p, " ut;} u;");
        p = isl_printer_end_line(p);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "u.ui = (unsigned int)data_split[split_idx];");
        p = isl_printer_end_line(p);
      }
    }

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "out_data = ");
    if (hls->target == XILINX_HW) {
      if (nxt_n_lane == 1) {
        p = isl_printer_print_str(p, "u.ut");
      } else {
        p = isl_printer_print_str(p, "data_split[split_idx]");
      }
    } else if (hls->target == INTEL_HW) {
      if (nxt_n_lane > 1)
        p = isl_printer_print_str(p, "data_split[split_idx]");
      else      
        p = isl_printer_print_str(p, "data.data[split_idx]");
    } else if (hls->target == CATAPULT_HW) {
      p = isl_printer_print_str(p, "data_split[split_idx]");
    }
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);    
  }

  return p;
}

static __isl_give isl_printer *autosa_kernel_print_io_transfer(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix, 
  char *in_fifo_suffix, char *out_fifo_suffix,
  enum IO_TRANS_DIR in, enum IO_TRANS_DIR out) 
{
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;  
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;
  isl_ctx *ctx = isl_printer_get_ctx(p);
  isl_ast_expr *local_index_packed = isl_ast_expr_copy(stmt->u.i.local_index);  
  int n_arg;
  int boundary = stmt->u.i.boundary;
  /* If the statement is a boundary statement, 
   * then ignore the filter condition by setting filter_sched_depth as -1
   */
  if (boundary)
    stmt->u.i.filter_sched_depth = -1;

  /* Extract the sparse data. */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  /* Pre-process the local index. */
  if (group->local_array->is_sparse) {
    isl_ast_expr *arg, *div;
    n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
    arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
    div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, vec_len * n_lane));
    arg = isl_ast_expr_div(arg, div);
    local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
  } else {
    if (n_lane > 1)
    {
      isl_ast_expr *arg, *div;
      n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
      arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
      div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, n_lane));
      arg = isl_ast_expr_div(arg, div);
      local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
    }
  }

  p = ppcg_start_block(p);  

  /* Declare some common variables here. */  
  int in_n_lane, out_n_lane;
  if (module->in) {    
    in_n_lane = n_lane;
    out_n_lane = nxt_n_lane;    
  } else {
    in_n_lane = nxt_n_lane;
    out_n_lane = n_lane;
  }

  /* [type_in] in_data; */
  p = isl_printer_start_line(p);
  if (group->local_array->is_sparse) {
    p = autosa_print_array_type_with_lane_sparse(p, group->array, in_n_lane);
  } else {    
    p = isl_printer_print_str(p, stmt->u.i.array->name);    
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, in_n_lane);
  } 
  p = isl_printer_print_str(p, " in_data;");
  p = isl_printer_end_line(p);
  
  /* [type_out] out_data; */
  p = isl_printer_start_line(p);
  if (group->local_array->is_sparse) {
    p = autosa_print_array_type_with_lane_sparse(p, group->array, out_n_lane);
  } else {    
    p = isl_printer_print_str(p, stmt->u.i.array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, out_n_lane);    
  }
  p = isl_printer_print_str(p, " out_data;");
  p = isl_printer_end_line(p);  

  if (n_lane != nxt_n_lane) {
    /* [type_nxt_n_lane] data_split[]; */
    if (hls->target == XILINX_HW || hls->target == CATAPULT_HW ||
      (hls->target == INTEL_HW && nxt_n_lane > 1)) {
      p = isl_printer_start_line(p);
      if (is_sparse) {
        p = autosa_print_array_type_with_lane_sparse(p, group->array, nxt_n_lane);
      } else {
        if (nxt_n_lane == 1) {
          if (hls->target == XILINX_HW) {
            p = isl_printer_print_str(p, "ap_uint<");
            p = isl_printer_print_int(p, group->array->size * 8);
            p = isl_printer_print_str(p, ">");
          } else if (hls->target == INTEL_HW) {
            p = isl_printer_print_str(p, group->array->type);
          } else if (hls->target == CATAPULT_HW) {
            p = isl_printer_print_str(p, group->array->name);
            p = isl_printer_print_str(p, "_t");
            p = isl_printer_print_int(p, nxt_n_lane);
          }       
        } else {
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, nxt_n_lane);
        }
      }
      p = isl_printer_print_str(p, " data_split[");
      p = isl_printer_print_int(p, n_lane / nxt_n_lane);
      p = isl_printer_print_str(p, "];");
      p = isl_printer_end_line(p);

      if (hls->target == XILINX_HW)
      {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=data_split complete");
        p = isl_printer_end_line(p);
      }
    }     
  }
  
  if ((in == GLOBAL_BUF || in == LOCAL_BUF) && (n_lane != nxt_n_lane)) {
    /* Insert guards. */
    /* if (cx % xx == 0) { */
    if (stmt->u.i.coalesce_depth >= 0) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "if (");
      if (iterator_prefix != NULL) {
        p = isl_printer_print_str(p, iterator_prefix);
      } else {
        p = isl_printer_print_str(p, "c");
      }    
      p = isl_printer_print_int(p, stmt->u.i.coalesce_depth);
      p = isl_printer_print_str(p, " % ");
      p = isl_printer_print_int(p, n_lane / nxt_n_lane);
      p = isl_printer_print_str(p, " == 0) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);
    }
  }

  /* Read in data */
  if (in == GLOBAL_BUF) {
    /* in_data = global_buf[]; */
    p = isl_printer_start_line(p);        
    p = isl_printer_print_str(p, "in_data = ");
    p = io_stmt_print_global_index(p, stmt, stmt->u.i.serialize);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  } else if (in == LOCAL_BUF) {
    /* in_data = local_buf[]; */
    p = isl_printer_start_line(p);   
    p = isl_printer_print_str(p, "in_data = ");

    if (stmt->u.i.module->double_buffer && 
          stmt->u.i.module->options->autosa->double_buffer_style == 0) {  
      isl_ast_expr *op;

      op = isl_ast_expr_op_get_arg(local_index_packed, 0);
      p = isl_printer_print_ast_expr(p, op);
      isl_ast_expr_free(op);
      p = isl_printer_print_str(p, stmt->u.i.in? "[arb]" : "[!arb]");
      for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
        op = isl_ast_expr_op_get_arg(local_index_packed, n);
        p = isl_printer_print_str(p, "[");
        p = isl_printer_print_ast_expr(p, op);
        p = isl_printer_print_str(p, "]");
        isl_ast_expr_free(op);
      }
    } else if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
      isl_ast_expr *op;

      op = isl_ast_expr_op_get_arg(local_index_packed, 0);
      p = isl_printer_print_ast_expr(p, op);    
      isl_ast_expr_free(op);
      p = isl_printer_print_str(p, "_tmp.data");
      for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
        op = isl_ast_expr_op_get_arg(local_index_packed, n);
        p = isl_printer_print_str(p, "[");
        p = isl_printer_print_ast_expr(p, op);
        p = isl_printer_print_str(p, "]");
        isl_ast_expr_free(op);
      }      
    } else {
      p = isl_printer_print_ast_expr(p, local_index_packed);
    }
    
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);    
  } else if (in == FIFO) {
    char *fifo_in_name;
    fifo_in_name = concat(ctx, stmt->u.i.in_fifo_name, in_fifo_suffix);    
    //isl_printer *p_str;    
    //p_str = isl_printer_to_str(ctx);
    //p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
    //p_str = isl_printer_print_str(p_str, "_");
    //p_str = isl_printer_print_str(p_str, in_fifo_suffix);
    //fifo_in_name = isl_printer_get_str(p_str);
    //isl_printer_free(p_str);

    /* in_data = fifo_in.read(); */
    p = isl_printer_start_line(p);  
    p = isl_printer_print_str(p, "in_data = ");
    if (hls->target == XILINX_HW)
      p = print_fifo_rw_xilinx(p, fifo_in_name, 1);
    else if (hls->target == INTEL_HW)
      p = print_fifo_rw_intel(p, fifo_in_name, 1);      
    else if (hls->target == CATAPULT_HW)
      p = print_fifo_rw_catapult(p, fifo_in_name, 1);  
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);  

    free(fifo_in_name);
  }

  /* Re-pack data in the middle. */
  if (n_lane == nxt_n_lane) {
    if (stmt->u.i.reduce) {
      p = autosa_print_reduce_default(p, stmt, n_lane, local_index_packed, group);
    } else {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "out_data = in_data;");
      p = isl_printer_end_line(p);
    }
  } else {
    if (out == FIFO) {
      /* write_data_split: data_split[] = in_data... */
      p = io_transfer_write_data_split(p, stmt, hls, iterator_prefix, "in_data");
    }    

    if ((in == GLOBAL_BUF || in == LOCAL_BUF) && (n_lane != nxt_n_lane)) {
      /* Insert guards. */
      /* if (cx % xx == 0) { */
      if (stmt->u.i.coalesce_depth >= 0) {
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
      }
    }

    /* calculate_split_idx: split_idx = ... */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int split_idx = (");    
    p = io_stmt_print_index_last_dim(
          p, stmt, stmt->u.i.serialize, ((in == GLOBAL_BUF) || (out == GLOBAL_BUF))? 1 : 0,
          n_lane, nxt_n_lane, is_sparse, vec_len);
    p = isl_printer_print_str(p, ") % ");
    p = isl_printer_print_int(p, n_lane / nxt_n_lane);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);

    if (out == GLOBAL_BUF) {
      /* update_data_split: data_split[split_i] = in_data; */
      p = io_transfer_update_data_split(p, stmt, hls, iterator_prefix);

      /* pack_out_data: out_data = (data_split[], ...); */
      p = io_transfer_pack_out_data(p, stmt, hls, iterator_prefix);
    } else if (out == LOCAL_BUF) {
      /* read_local_buf: out_data = local_buf[...]; */
      p = io_transfer_read_local_buf(p, stmt, hls, iterator_prefix, local_index_packed);

      /* parse_sparse_data */
      if (is_sparse) {
        p = io_transfer_parse_sparse_data(p, stmt, hls, iterator_prefix);
      }

      /* write_data_split: data_split[] = out_data... */
      p = io_transfer_write_data_split(p, stmt, hls, iterator_prefix, "out_data");

      /* update_data_split: data_split[split_i] = in_data; */
      p = io_transfer_update_data_split(p, stmt, hls, iterator_prefix);

      /* pack_out_data: out_data = (data_split[], ...) */
      p = io_transfer_pack_out_data(p, stmt, hls, iterator_prefix);
    } else if (out == FIFO) {
      /* read_data_split: out_data = data_split[split_i]; */
      p = io_transfer_read_data_split(p, stmt, hls, iterator_prefix);
    }
  }

  if ((out == GLOBAL_BUF || out == LOCAL_BUF) && (n_lane != nxt_n_lane)) {
    if (stmt->u.i.coalesce_depth >= 0) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "if (");
      if (iterator_prefix != NULL) {
        p = isl_printer_print_str(p, iterator_prefix);
      } else {
        p = isl_printer_print_str(p, "c");
      }            
      p = isl_printer_print_int(p, stmt->u.i.coalesce_depth);
      p = isl_printer_print_str(p, " % ");
      p = isl_printer_print_int(p, n_lane / nxt_n_lane);
      p = isl_printer_print_str(p, " == ");
      p = isl_printer_print_int(p, n_lane / nxt_n_lane);
      p = isl_printer_print_str(p, " - 1 || c");
      p = isl_printer_print_int(p, stmt->u.i.coalesce_depth);
      p = isl_printer_print_str(p, " == ");
      p = isl_printer_print_int(p, stmt->u.i.coalesce_bound - 1);
      p = isl_printer_print_str(p, ") {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);
    }
  }

  /* Write out data. */
  if (out == GLOBAL_BUF) {
    /* global_buf[] = in_data; */
    p = isl_printer_start_line(p);   
    p = io_stmt_print_global_index(p, stmt, stmt->u.i.serialize);
    p = isl_printer_print_str(p, " = out_data;");
    p = isl_printer_end_line(p);
  } else if (out == LOCAL_BUF) {      
    /* local_buf[] = fifo_data; */
    //if (stmt->u.i.reduce) {
    //  p = autosa_print_reduce_default(p, stmt, n_lane, local_index_packed, group);
    //} else {
      p = isl_printer_start_line(p);

      if (stmt->u.i.module->double_buffer && 
            stmt->u.i.module->options->autosa->double_buffer_style == 0) {
        isl_ast_expr *op;
              
        op = isl_ast_expr_op_get_arg(local_index_packed, 0);
        p = isl_printer_print_ast_expr(p, op);
        isl_ast_expr_free(op);
        p = isl_printer_print_str(p, stmt->u.i.in? "[arb]" : "[!arb]");
        for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
            op = isl_ast_expr_op_get_arg(local_index_packed, n);
            p = isl_printer_print_str(p, "[");
            p = isl_printer_print_ast_expr(p, op);
            p = isl_printer_print_str(p, "]");
            isl_ast_expr_free(op);
        }        
      } else if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
        isl_ast_expr *op;

        op = isl_ast_expr_op_get_arg(local_index_packed, 0);
        p = isl_printer_print_ast_expr(p, op);    
        isl_ast_expr_free(op);
        p = isl_printer_print_str(p, "_tmp.data");
        for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
          op = isl_ast_expr_op_get_arg(local_index_packed, n);
          p = isl_printer_print_str(p, "[");
          p = isl_printer_print_ast_expr(p, op);
          p = isl_printer_print_str(p, "]");
          isl_ast_expr_free(op);
        }        
      } else {        
        p = isl_printer_print_ast_expr(p, local_index_packed);        
      }

      p = isl_printer_print_str(p, " ");
      //if (stmt->u.i.reduce) {        
      //  p = isl_printer_print_str(p, stmt->u.i.reduce_op);        
      //}               
      p = isl_printer_print_str(p, "= out_data;");
      p = isl_printer_end_line(p);
    //}    
  } else if (out == FIFO) {      
    char *fifo_out_name;
    fifo_out_name = concat(ctx, stmt->u.i.out_fifo_name, out_fifo_suffix);      

    /* fifo_out.write(fifo_data); */          
    p = isl_printer_start_line(p);
    if (hls->target == XILINX_HW)
      p = print_fifo_rw_xilinx(p, fifo_out_name, 0);
    else if (hls->target == INTEL_HW)
      p = print_fifo_rw_intel(p, fifo_out_name, 0);
    else if (hls->target == CATAPULT_HW)
      p = print_fifo_rw_catapult(p, fifo_out_name, 0);
    p = isl_printer_print_str(p, "out_data);");
    p = isl_printer_end_line(p);
   
    free(fifo_out_name);    
  }

  if ((out == GLOBAL_BUF || out == LOCAL_BUF) && (n_lane != nxt_n_lane)) {
    if (stmt->u.i.coalesce_depth >= 0) {
      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");
    }
  }

  p = ppcg_end_block(p);

  isl_ast_expr_free(local_index_packed);

  return p;
}

/* This function extracts the necessary information for generating I/O transfer statements and 
 * calls the final function to generate the statements.
 */
static __isl_give isl_printer *autosa_kernel_print_io_transfer_wrapper(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,  
  struct hls_info *hls, const char *iterator_prefix
) {
  int n_lane, nxt_n_lane;
  enum IO_TRANS_DIR in, out;
  char in_fifo_suffix[100], out_fifo_suffix[100];

  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  if (stmt->type == AUTOSA_KERNEL_STMT_IO_DRAM) {
    if (stmt->u.i.in) {
      if (module->is_serialized) {
        in = FIFO;
        sprintf(in_fifo_suffix, "serialize");
      } else {
        in = GLOBAL_BUF;
      }

      if (stmt->u.i.buf) {
        out = LOCAL_BUF;
      } else {
        out = FIFO;
        sprintf(out_fifo_suffix, "out");
      }      
    } else {
      if (stmt->u.i.buf) {
        in = LOCAL_BUF;
      } else {
        in = FIFO;
        sprintf(in_fifo_suffix, "in");
      }

      if (module->is_serialized) {
        out = FIFO;
        sprintf(out_fifo_suffix, "serialize");
      } else {
        out = GLOBAL_BUF;
      }
    }
  } else if (stmt->type == AUTOSA_KERNEL_STMT_IO_TRANSFER) {
    if (stmt->u.i.in) {
      in = FIFO;
      sprintf(in_fifo_suffix, "in");

      if (stmt->u.i.buf) {
        out = LOCAL_BUF;
      } else {
        out = FIFO;
        sprintf(out_fifo_suffix, "out");
      }
    } else {
      if (stmt->u.i.buf) {
        in = LOCAL_BUF;
      } else {
        in = FIFO;
        sprintf(in_fifo_suffix, "in");
      }

      out = FIFO;
      sprintf(out_fifo_suffix, "out");
    }    
  }

  p = autosa_kernel_print_io_transfer(
    p, stmt, hls, iterator_prefix, in_fifo_suffix, out_fifo_suffix, in, out);

  return p;
}

/* Print an I/O transfer statement.
 * is_filter = 0
 * is_buf = 1
 * An in I/O statement is printed as
 *
 *  [type] fifo_data;
 *  [type2] buf_data;
 *  [type] buf_data_split[];
 *  buf_data = local_buf[...];
 *  fifo_data = fifo.read();
 *  for (int n = 0; n < n_lane / nxt_n_lane; n++) {
 *    buf_data_split[n] = buf_data();
 *    buf_data = buf_data >> DW;
 *  }
 *  buf_data_split[...] = Reinterpret<>(fifo_data);
 *  buf_data = (buf_data_split[1], ...);
 *  local_buf[...] = buf_data;
 *
 * An out I/O staement is printed as 
 *
 *  [type] fifo_data;
 *  [type2] buf_data;
 *  [type] buf_data_split[];
 *  buf_data = local_buf[...];
 *  for (int n = 0; n < n_lane / nxt_n_lane; n++) {
 *    buf_data_split[n] = buf_data();
 *    buf_data = buf_data >> DW;
 *  }
 *  fifo_data = Reinterpret<>(buf_data_split[...]);
 *  fifo.write(fifo_data);
 */
static __isl_give isl_printer *autosa_kernel_print_io_transfer_data_pack(
  __isl_take isl_printer *p, struct autosa_kernel_stmt *stmt,
  struct autosa_array_ref_group *group, int n_lane, int nxt_n_lane,
  struct hls_info *hls, const char *iterator_prefix, int global, int buffer)
{
  isl_ctx *ctx;
  ctx = isl_printer_get_ctx(p);
  int boundary = stmt->u.i.boundary;

  char *fifo_name;
  isl_ast_expr *expr, *op;
  int n_arg;
  int r;
  isl_val *val;
  isl_ast_expr *local_index_packed;
  isl_ast_expr *arg, *div;
  local_index_packed = isl_ast_expr_copy(stmt->u.i.local_index);
  /* Extract the sparse data */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  /* Modify the local index. */
  if (is_sparse) {
    n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
    arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
    div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, vec_len * n_lane));
    arg = isl_ast_expr_div(arg, div);
    local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
  } else {
    if (n_lane > 1)
    {
      n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
      arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
      div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, n_lane));
      arg = isl_ast_expr_div(arg, div);
      local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
    }
  }

  /* [type] fifo_data; */
  p = isl_printer_start_line(p);
  if (is_sparse) 
    p = autosa_print_array_type_with_lane_sparse(p, group->array, nxt_n_lane);
  else
    p = autosa_print_array_type_with_lane(p, group->array, nxt_n_lane);  
  p = isl_printer_print_str(p, " ");
  p = isl_printer_print_str(p, "fifo_data;");
  p = isl_printer_end_line(p);

  /* [type2] buf_data; */
  p = isl_printer_start_line(p);
  if (is_sparse) {
    p = autosa_print_array_type_with_lane_sparse(p, group->array, n_lane);
  } else {
    p = isl_printer_print_str(p, group->array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, n_lane);
  }
  p = isl_printer_print_str(p, " ");
  p = isl_printer_print_str(p, "buf_data;");
  p = isl_printer_end_line(p);

  /* [type] buf_data_split[]; */  
  if (hls->target == XILINX_HW || hls->target == CATAPULT_HW ||
      (hls->target == INTEL_HW && nxt_n_lane > 1)) {
    p = isl_printer_start_line(p);
    if (is_sparse) {
      p = autosa_print_array_type_with_lane_sparse(p, group->array, nxt_n_lane);
    } else {
      if (nxt_n_lane == 1)
      {
        if (hls->target == XILINX_HW)
        {
          p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_int(p, group->array->size * 8);
          p = isl_printer_print_str(p, ">");
        }
        else if (hls->target == INTEL_HW)
        {
          p = isl_printer_print_str(p, group->array->type);
        }
        else if (hls->target == CATAPULT_HW)
        {
          p = isl_printer_print_str(p, group->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, nxt_n_lane);
        }
      }
      else
      {
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, "_t");
        p = isl_printer_print_int(p, nxt_n_lane);
      }
    }
    p = isl_printer_print_str(p, " buf_data_split[");
    p = isl_printer_print_int(p, n_lane / nxt_n_lane);
    p = isl_printer_print_str(p, "];");
    p = isl_printer_end_line(p);
    if (hls->target == XILINX_HW)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=buf_data_split complete");
      p = isl_printer_end_line(p);
    }
  }
  
  if (stmt->u.i.in && stmt->u.i.coalesce_depth >= 0)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (");
    if (iterator_prefix != NULL) {
      p = isl_printer_print_str(p, iterator_prefix);
    } else {
      p = isl_printer_print_str(p, "c");
    }    
    p = isl_printer_print_int(p, stmt->u.i.coalesce_depth);
    p = isl_printer_print_str(p, " % ");
    p = isl_printer_print_int(p, n_lane / nxt_n_lane);
    p = isl_printer_print_str(p, " == 0) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);
  }
  /* buf_data = local[]; */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "buf_data = ");
  if (stmt->u.i.module->double_buffer && 
      stmt->u.i.module->options->autosa->double_buffer_style == 0) {
    isl_ast_expr *op;
    op = isl_ast_expr_op_get_arg(local_index_packed, 0);
    p = isl_printer_print_ast_expr(p, op);    
    isl_ast_expr_free(op);
    p = isl_printer_print_str(p, stmt->u.i.in? "[arb]" : "[!arb]");
    for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
      op = isl_ast_expr_op_get_arg(local_index_packed, n);
      p = isl_printer_print_str(p, "[");
      p = isl_printer_print_ast_expr(p, op);
      p = isl_printer_print_str(p, "]");
      isl_ast_expr_free(op);
    }
  } else {
    if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
      isl_ast_expr *op;
      op = isl_ast_expr_op_get_arg(local_index_packed, 0);
      p = isl_printer_print_ast_expr(p, op);    
      isl_ast_expr_free(op);
      p = isl_printer_print_str(p, "_tmp.data");
      for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
        op = isl_ast_expr_op_get_arg(local_index_packed, n);
        p = isl_printer_print_str(p, "[");
        p = isl_printer_print_ast_expr(p, op);
        p = isl_printer_print_str(p, "]");
        isl_ast_expr_free(op);
      }
    } else {
      p = isl_printer_print_ast_expr(p, local_index_packed);
    }
  }

  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);

  if (is_sparse) {
    /* [type] buf_data_d = buf_data.d; */
    p = isl_printer_start_line(p);
    p = autosa_print_array_type_with_lane(p, group->array, n_lane * n_nzero);
    p = isl_printer_print_str(p, " buf_data_d = buf_data.d;");
    p = isl_printer_end_line(p);

    /* [type] buf_data_i = buf_data.i; */
    p = isl_printer_start_line(p);
    if (hls->target == XILINX_HW) {
      p = isl_printer_print_str(p, "ap_uint<");
      p = isl_printer_print_int(p, 8 * n_lane);
    } else if (hls->target == CATAPULT_HW) {
      p = isl_printer_print_str(p, "ac_int<");
      p = isl_printer_print_int(p, 8 * n_lane);
      p = isl_printer_print_str(p, ", false");
    }
    p = isl_printer_print_str(p, "> buf_data_i = buf_data.i;");
    p = isl_printer_end_line(p);      
  }

  if (hls->target == XILINX_HW)
  {    
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int n = 0; n < ");
    p = isl_printer_print_int(p, n_lane / nxt_n_lane);
    p = isl_printer_print_str(p, "; n++) {");
    p = isl_printer_end_line(p);
        
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma HLS UNROLL");
    p = isl_printer_end_line(p);    
    p = isl_printer_indent(p, 2);

    if (is_sparse) {
      /* buf_data_split[n] = {buf_data_d(), ...} */    
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "buf_data_split[n] = (");
      p = isl_printer_print_str(p, group->array->name);
      p = isl_printer_print_str(p, "_s_t");
      p = isl_printer_print_int(p, nxt_n_lane);
      p = isl_printer_print_str(p, "){buf_data_d(");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane * n_nzero - 1);
      p = isl_printer_print_str(p, ", 0), buf_data_i(");
      p = isl_printer_print_int(p, 8 * nxt_n_lane - 1);
      p = isl_printer_print_str(p, ", 0)};");
      p = isl_printer_end_line(p);      

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "buf_data_d = buf_data_d >> ");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane * n_nzero);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "buf_data_i = buf_data_i >> ");
      p = isl_printer_print_int(p, 8 * nxt_n_lane);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    } else {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "buf_data_split[n] = buf_data(");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane - 1);
      p = isl_printer_print_str(p, ", 0);");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "buf_data = buf_data >> ");
      p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }

    p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "}");
    p = isl_printer_end_line(p);
  }
  else if (hls->target == INTEL_HW && nxt_n_lane > 1) 
  {    
    for (int i = 0; i < n_lane / nxt_n_lane; i++)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "buf_data_split[");
      p = isl_printer_print_int(p, i);
      p = isl_printer_print_str(p, "]");
      if (nxt_n_lane > 1)
        p = isl_printer_print_str(p, ".data");
      p = isl_printer_print_str(p, " = buf_data.data.s");
      for (int j = 0; j < nxt_n_lane; j++)
      {
        p = isl_printer_print_str(p, vector_index[j + i * nxt_n_lane]);
      }
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }    
  }
  else if (hls->target == CATAPULT_HW) {
    for (int i = 0; i < n_lane / nxt_n_lane; i++) {
      if (is_sparse) {
        /* buf_data_split[].set_slc(0, buf_data_i.slc<>()); */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "buf_data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "].set_slc(0, ");
        p = isl_printer_print_str(p, "buf_data_i.slc<");
        p = isl_printer_print_int(p, 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, i * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, "));");
        p = isl_printer_end_line(p);

        /* buf_data_split[].set_slc(xx, buf_data_d.slc<>()); */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "buf_data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "].set_slc(");
        p = isl_printer_print_int(p, 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ", buf_data_d.slc<");;
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane * n_nzero);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, i * group->array->size * 8 * nxt_n_lane * n_nzero);
        p = isl_printer_print_str(p, "));");
        p = isl_printer_end_line(p);
      } else {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "buf_data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "] = buf_data.slc<");
        p = isl_printer_print_int(p, group->array->size * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ">(");
        p = isl_printer_print_int(p, i * group->array->size * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
  }

  if (stmt->u.i.in && stmt->u.i.coalesce_depth >= 0)
  {
    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }

  /* split_i = ... */
  expr = isl_ast_expr_copy(stmt->u.i.local_index);
  n_arg = isl_ast_expr_op_get_n_arg(expr);
  op = isl_ast_expr_op_get_arg(expr, n_arg - 1);
  r = n_lane / nxt_n_lane;
  if (is_sparse) 
    val = isl_val_int_from_si(ctx, vec_len * nxt_n_lane);
  else
    val = isl_val_int_from_si(ctx, nxt_n_lane);
  op = isl_ast_expr_div(op, isl_ast_expr_from_val(val));
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "int split_i = (");
  p = isl_printer_print_ast_expr(p, op);
  p = isl_printer_print_str(p, ") % ");
  p = isl_printer_print_int(p, r);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_ast_expr_free(op);
  isl_ast_expr_free(expr);
  if (stmt->u.i.in)
  {
    fifo_name = concat(ctx, stmt->u.i.in_fifo_name, "in");
    /* fifo_data = fifo.read(); */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "fifo_data = ");
    if (hls->target == XILINX_HW)
      p = print_fifo_rw_xilinx(p, fifo_name, 1);
    else if (hls->target == INTEL_HW)
      p = print_fifo_rw_intel(p, fifo_name, 1);
    else if (hls->target == CATAPULT_HW)
      p = print_fifo_rw_catapult(p, fifo_name, 1);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
      /* buf_data_split[...] = Reinterpret<>(fifo_data); */
    if (hls->target == XILINX_HW || hls->target == CATAPULT_HW || 
        (hls->target == INTEL_HW && nxt_n_lane > 1)) {
      if (stmt->u.i.reduce) {
        p = autosa_print_reduce_data_pack(p, stmt, nxt_n_lane, n_lane, group, hls->target); // TODO
      } else {      
        if (hls->target == XILINX_HW)
        {
          if (nxt_n_lane == 1)
          {
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "union {unsigned int ui; ");
            p = isl_printer_print_str(p, group->array->type);
            p = isl_printer_print_str(p, " ut;} u;");
            p = isl_printer_end_line(p);
  
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "u.ut = fifo_data;");
            p = isl_printer_end_line(p);
          }
        }
  
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "buf_data_split[split_i] ");
        if (stmt->u.i.reduce) {
          p = isl_printer_print_str(p, stmt->u.i.reduce_op);
        }
        p = isl_printer_print_str(p, "= ");
  
        if (hls->target == XILINX_HW)
        {
          if (nxt_n_lane == 1)
          {
            p = isl_printer_print_str(p, "ap_uint<");
            p = isl_printer_print_int(p, group->array->size * 8);
            p = isl_printer_print_str(p, ">(u.ui);");
          }
          else
          {
            p = isl_printer_print_str(p, "fifo_data;");
          }
        }
        else 
        {
          p = isl_printer_print_str(p, "fifo_data;");
        }
        p = isl_printer_end_line(p);      
      }
  
      if (stmt->u.i.coalesce_depth >= 0)
      {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "if (");
        if (iterator_prefix != NULL) {
          p = isl_printer_print_str(p, iterator_prefix);
        } else {
          p = isl_printer_print_str(p, "c");
        }            
        p = isl_printer_print_int(p, stmt->u.i.coalesce_depth);
        p = isl_printer_print_str(p, " % ");
        p = isl_printer_print_int(p, n_lane / nxt_n_lane);
        p = isl_printer_print_str(p, " == ");
        p = isl_printer_print_int(p, n_lane / nxt_n_lane);
        p = isl_printer_print_str(p, " - 1 || c");
        p = isl_printer_print_int(p, stmt->u.i.coalesce_depth);
        p = isl_printer_print_str(p, " == ");
        p = isl_printer_print_int(p, stmt->u.i.coalesce_bound - 1);
        p = isl_printer_print_str(p, ") {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);
      }
    }
    /* buf_data = (buf_data_split[1], ...); */
    p = isl_printer_start_line(p);
    if (hls->target == XILINX_HW)
    {
      int first = 1;
      p = isl_printer_print_str(p, "buf_data = (");
      for (int i = n_lane / nxt_n_lane - 1; i >= 0; i--)
      {
        if (!first)
          p = isl_printer_print_str(p, ", ");
        p = isl_printer_print_str(p, "buf_data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "]");
          first = 0;
      }
      p = isl_printer_print_str(p, ");");
    } else if (hls->target == INTEL_HW)
    {
      if (nxt_n_lane == 1) {
        p = isl_printer_print_str(p, "buf_data.data[split_i] = fifo_data;");
      } else {
        int first = 1;
        p = isl_printer_print_str(p, "buf_data.data = ");
        p = isl_printer_print_str(p, "(");
        p = isl_printer_print_str(p, group->array->type);
        p = isl_printer_print_int(p, n_lane);
        p = isl_printer_print_str(p, ")(");
          for (int i = 0; i < n_lane / nxt_n_lane; i++)
        {
          if (!first)
            p = isl_printer_print_str(p, ", ");
            if (nxt_n_lane > 1)
          {
            p = isl_printer_print_str(p, "(");
            p = isl_printer_print_str(p, group->array->type);
            p = isl_printer_print_int(p, nxt_n_lane);
            p = isl_printer_print_str(p, ")");
          }
          p = isl_printer_print_str(p, "buf_data_split[");
          p = isl_printer_print_int(p, i);
          p = isl_printer_print_str(p, "]");
          if (nxt_n_lane > 1)
          {
            p = isl_printer_print_str(p, ".data");
          }
            first = 0;
        }
        p = isl_printer_print_str(p, ");");
      }
    } else if (hls->target == CATAPULT_HW) {
      for (int i = 0; i < n_lane / nxt_n_lane; i++) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "buf_data.set_slc(");
        p = isl_printer_print_int(p, i * group->array->size * 8 * nxt_n_lane);
        p = isl_printer_print_str(p, ", buf_data_split[");
        p = isl_printer_print_int(p, i);
        p = isl_printer_print_str(p, "]);");
        p = isl_printer_end_line(p);  
      }
    }
      p = isl_printer_end_line(p);
      /* local_buf[...] = buf_data; */
    p = isl_printer_start_line(p);    
    if (stmt->u.i.module->double_buffer && 
        stmt->u.i.module->options->autosa->double_buffer_style == 0) {
      isl_ast_expr *op;
      op = isl_ast_expr_op_get_arg(local_index_packed, 0);
      p = isl_printer_print_ast_expr(p, op);
      isl_ast_expr_free(op);
      p = isl_printer_print_str(p, "[arb]");
      for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
        op = isl_ast_expr_op_get_arg(local_index_packed, n);
        p = isl_printer_print_str(p, "[");
        p = isl_printer_print_ast_expr(p, op);
        p = isl_printer_print_str(p, "]");
        isl_ast_expr_free(op);
      }
    } else {
      if (hls->target == CATAPULT_HW && stmt->u.i.module->is_filter) {
        isl_ast_expr *op;
        op = isl_ast_expr_op_get_arg(local_index_packed, 0);
        p = isl_printer_print_ast_expr(p, op);    
        isl_ast_expr_free(op);
        p = isl_printer_print_str(p, "_tmp.data");
        for (int n = 1; n < isl_ast_expr_op_get_n_arg(local_index_packed); n++) {
          op = isl_ast_expr_op_get_arg(local_index_packed, n);
          p = isl_printer_print_str(p, "[");
          p = isl_printer_print_ast_expr(p, op);
          p = isl_printer_print_str(p, "]");
          isl_ast_expr_free(op);
        }        
      } else {
        p = isl_printer_print_ast_expr(p, local_index_packed);
      }
    }
    p = isl_printer_print_str(p, " = buf_data;");
    p = isl_printer_end_line(p);
      if (stmt->u.i.coalesce_depth >= 0)
    {
      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");
    }
      free(fifo_name);
  } else {
    if (is_sparse) {
      /* fifo_data = buf_data_split[...]; */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = buf_data_split[split_i];");
      p = isl_printer_end_line(p);
      /* fifo.write(fifo_data); */
      fifo_name = concat(ctx, stmt->u.i.out_fifo_name, "out");
      p = isl_printer_start_line(p);
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 0);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 0);
      p = isl_printer_print_str(p, "fifo_data);");
      p = isl_printer_end_line(p);
      free(fifo_name);
    } else {
      fifo_name = concat(ctx, stmt->u.i.out_fifo_name, "out");
      if (hls->target == XILINX_HW)
      {
        if (nxt_n_lane == 1)
        {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "union {unsigned int ui; ");
          p = isl_printer_print_str(p, group->array->type);
          p = isl_printer_print_str(p, " ut;} u;");
          p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "u.ui = (unsigned int)buf_data_split[split_i];");
          p = isl_printer_end_line(p);
        }
      }
      /* fifo_data = Reinterpret<>(buf_data_split[...]); */    
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      if (hls->target == XILINX_HW)
      {
        if (nxt_n_lane == 1)
        {
          p = isl_printer_print_str(p, "u.ut");
        }
        else
        {
          p = isl_printer_print_str(p, "buf_data_split[split_i]");
        }
      }
      else if (hls->target == INTEL_HW)
      {
        if (nxt_n_lane > 1)
          p = isl_printer_print_str(p, "buf_data_split[split_i]");
        else      
          p = isl_printer_print_str(p, "buf_data.data[split_i]");
      }
      else if (hls->target == CATAPULT_HW) 
      {
        p = isl_printer_print_str(p, "buf_data_split[split_i]");
      }
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);    
        /* fifo.write(fifo_data); */
      p = isl_printer_start_line(p);
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 0);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 0);
      p = isl_printer_print_str(p, "fifo_data);");
      p = isl_printer_end_line(p);
        free(fifo_name);
    }
  }

  isl_ast_expr_free(local_index_packed);

  return p;
}

///* Print an I/O transfer statement.
// */
//__isl_give isl_printer *autosa_kernel_print_io_transfer(
//    __isl_take isl_printer *p,
//    struct autosa_kernel_stmt *stmt, struct hls_info *hls, const char *iterator_prefix)
//{
//  struct autosa_hw_module *module = stmt->u.i.module;
//  struct autosa_array_ref_group *group = stmt->u.i.group;
//  int n_lane = stmt->u.i.data_pack;
//  int nxt_n_lane = stmt->u.i.nxt_data_pack;
//  //int is_filter = stmt->u.i.filter;
//  int is_buf = stmt->u.i.buf;
//  isl_ctx *ctx = isl_printer_get_ctx(p);
//
//  //  p = ppcg_start_block(p);
//  if (n_lane == nxt_n_lane) {    
//    p = autosa_kernel_print_io_transfer_default(p, stmt, group, n_lane, hls, iterator_prefix);
//  } else {    
//    p = autosa_kernel_print_io_transfer_data_pack(
//          p, stmt, group, n_lane, nxt_n_lane, hls, iterator_prefix, 0, 1);
//  }
//  //  p = ppcg_end_block(p);
//
//  return p;
//}

/* Print a serialization/deserialization statement.
 * Serialization:
 * X_to[X_cnt++] = X_from[...]
 * Deserizalition:
 * X_to[...] = X_from[X_cnt++]
 */
__isl_give isl_printer *autosa_kernel_print_host_serialize(
  __isl_take isl_printer *p,
  struct autosa_kernel_stmt *stmt,
  struct hls_info *hls)
{
  isl_ast_expr *index, *arg;
  isl_id *id;
  const char *array_name;

  index = stmt->u.s.index;
  p = isl_printer_start_line(p);
  arg = isl_ast_expr_get_op_arg(index, 0);
  id = isl_ast_expr_id_get_id(arg);
  array_name = isl_id_get_name(id);
  isl_id_free(id);
  isl_ast_expr_free(arg);

  arg = isl_ast_expr_get_op_arg(index, 1);

  if (stmt->u.s.in) {
    p = isl_printer_print_str(p, array_name);
    p = isl_printer_print_str(p, "_to[cnt++] = ");    
    p = isl_printer_print_str(p, array_name);
    p = isl_printer_print_str(p, "_from[");
    if (stmt->u.s.group->local_array->is_sparse)
      p = isl_printer_print_str(p, "(");
    p = isl_printer_print_ast_expr(p, arg);
    if (stmt->u.s.group->local_array->is_sparse)
      p = isl_printer_print_str(p, ") / EFF_COMPRESS_RATIO");
    p = isl_printer_print_str(p, "];");
  } else {
    p = isl_printer_print_str(p, array_name);
    p = isl_printer_print_str(p, "_to[");
    p = isl_printer_print_ast_expr(p, arg);
    p = isl_printer_print_str(p, "] = ");
    p = isl_printer_print_str(p, array_name);
    p = isl_printer_print_str(p, "_from[cnt++];");    
  }
  p = isl_printer_end_line(p);
  isl_ast_expr_free(arg);

  return p;
}

/* Print a drain merge statement.
 *
 * [group_array_prefix]_to[...] = [group_array_prefix]_from[...]
 */
__isl_give isl_printer *autosa_kernel_print_drain_merge(__isl_take isl_printer *p,
                                                        struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  isl_ast_expr *index_to, *index_from, *arg;
  isl_ctx *ctx = hls->ctx;
  struct autosa_drain_merge_func *func = stmt->u.dm.func;
  isl_ast_expr *index = stmt->u.dm.index;
  int n_arg;
  isl_id *id;
  const char *array_name;
  char *new_array_name;
  isl_printer *p_str;

  p = isl_printer_start_line(p);
  // TODO
  n_arg = isl_ast_expr_get_op_n_arg(index);
  /* Modify the index. */
  arg = isl_ast_expr_get_op_arg(index, 0);
  id = isl_ast_expr_id_get_id(arg);
  array_name = isl_id_get_name(id);
  isl_id_free(id);
  isl_ast_expr_free(arg);
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, array_name);
  p_str = isl_printer_print_str(p_str, "_to");
  new_array_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  id = isl_id_alloc(ctx, new_array_name, NULL);
  arg = isl_ast_expr_from_id(id);
  free(new_array_name);
  index_to = isl_ast_expr_set_op_arg(isl_ast_expr_copy(index), 0, arg);

  arg = isl_ast_expr_get_op_arg(index, 0);
  id = isl_ast_expr_id_get_id(arg);
  array_name = isl_id_get_name(id);
  isl_id_free(id);
  isl_ast_expr_free(arg);
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, array_name);
  p_str = isl_printer_print_str(p_str, "_from");
  new_array_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  id = isl_id_alloc(ctx, new_array_name, NULL);
  arg = isl_ast_expr_from_id(id);
  free(new_array_name);
  index_from = isl_ast_expr_set_op_arg(isl_ast_expr_copy(index), 0, arg);

  p = isl_printer_print_ast_expr(p, index_to);
  p = isl_printer_print_str(p, " = ");
  p = isl_printer_print_ast_expr(p, index_from);
  p = isl_printer_print_str(p, ";");

  isl_ast_expr_free(index_to);
  isl_ast_expr_free(index_from);

  p = isl_printer_end_line(p);

  return p;
}

/* Print an I/O dram statement.
 *
 * An in I/O statement is printed as 
 *
 *  [type] fifo_data;
 *  fifo_data = global;
 *  or 
 *  fifo_data = fifo_[arr].read() // when serialize is enabled
 *  fifo.write(fifo_data);
 *
 * while an out I/O statement is printed as
 *
 *  [type] fifo_data;
 *  fifo_data = fifo.read();
 *  global = fifo_data;
 *  or 
 *  fifo_[arr].write(fifo_data); // when serialize is enabled
 */
__isl_give isl_printer *autosa_kernel_print_io_dram(
  __isl_take isl_printer *p,
  struct autosa_kernel_stmt *stmt, struct hls_info *hls,
  const char *iterator_prefix)
{
  // TODO: add when data packing factors are different.
  struct autosa_array_ref_group *group = stmt->u.i.group;
  struct autosa_hw_module *module = stmt->u.i.module;
  char *fifo_name;
  int n_lane = stmt->u.i.data_pack;
  int nxt_n_lane = stmt->u.i.nxt_data_pack;
  isl_ctx *ctx = isl_printer_get_ctx(p);
  int buf = stmt->u.i.buf;
  isl_ast_expr *local_index_packed;  
  int n_arg;  
  /* Extract the sparse data. */
  int is_sparse = group->local_array->is_sparse;
  int vec_len = stmt->u.i.local_array->vec_len;
  int n_nzero = stmt->u.i.local_array->n_nzero;
  float compress_ratio = stmt->u.i.local_array->compress_ratio;
  int n_meta_data = stmt->u.i.local_array->n_meta_data;
  float eff_compress_ratio = stmt->u.i.local_array->eff_compress_ratio;

  local_index_packed = isl_ast_expr_copy(stmt->u.i.local_index);
  /* Modify the local index; */
  if (group->local_array->is_sparse) {
    isl_ast_expr *arg, *div;
    n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
    arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
    div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, vec_len * n_lane));
    arg = isl_ast_expr_div(arg, div);
    local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
  } else {
    if (n_lane > 1)
    {
      isl_ast_expr *arg, *div;
      n_arg = isl_ast_expr_get_op_n_arg(local_index_packed);
      arg = isl_ast_expr_get_op_arg(local_index_packed, n_arg - 1);
      div = isl_ast_expr_from_val(isl_val_int_from_si(ctx, n_lane));
      arg = isl_ast_expr_div(arg, div);
      local_index_packed = isl_ast_expr_set_op_arg(local_index_packed, n_arg - 1, arg);
    }
  }

  p = isl_printer_indent(p, -2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "{");
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, 2);

  /* Declare the fifo data variable. */
  p = isl_printer_start_line(p);
  if (group->local_array->is_sparse) {
    p = autosa_print_array_type_with_lane_sparse(p, group->array, nxt_n_lane);
  } else {    
    p = isl_printer_print_str(p, stmt->u.i.array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, nxt_n_lane);    
  }
  p = isl_printer_print_str(p, " fifo_data;");
  p = isl_printer_end_line(p);

  if (stmt->u.i.in)
  {
    /* Generate the serialize fifo name */
    isl_printer *p_str;
    char *serialize_fifo_name;
    p_str = isl_printer_to_str(ctx);
    p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
    p_str = isl_printer_print_str(p_str, "_serialize");
    serialize_fifo_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);

    p = isl_printer_start_line(p);    
    p = isl_printer_print_str(p, "fifo_data = ");        
    if (module->is_serialized) {
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, serialize_fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, serialize_fifo_name, 1);      
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, serialize_fifo_name, 1);      
    } else {
      p = io_stmt_print_global_index(p, stmt, stmt->u.i.serialize);    
    }
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);

    free(serialize_fifo_name);

    if (!buf) {            
      fifo_name = concat(ctx, stmt->u.i.out_fifo_name, "out");      
      p = isl_printer_start_line(p);
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 0);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 0);
      p = isl_printer_print_str(p, "fifo_data);");
      p = isl_printer_end_line(p);
      free(fifo_name);      
    }
    else
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_ast_expr(p, local_index_packed);
      p = isl_printer_print_str(p, " = fifo_data;");
      p = isl_printer_end_line(p);
    }
  }
  else
  {
    if (!buf)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");      
      fifo_name = concat(ctx, stmt->u.i.in_fifo_name, "in");      
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, fifo_name, 1);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, fifo_name, 1);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
      free(fifo_name);
    }
    else
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      p = isl_printer_print_ast_expr(p, local_index_packed);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }

    p = isl_printer_start_line(p);    
    if (module->is_serialized) {
      /* Generate serialize fifo name */
      isl_printer *p_str;
      char *serialize_fifo_name;
      p_str = isl_printer_to_str(ctx);
      p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
      p_str = isl_printer_print_str(p_str, "_serialize");
      serialize_fifo_name = isl_printer_get_str(p_str);
      isl_printer_free(p_str);

      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, serialize_fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, serialize_fifo_name, 0);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, serialize_fifo_name, 0);
      p = isl_printer_print_str(p, "fifo_data);");

      free(serialize_fifo_name);
    } else {
      p = io_stmt_print_global_index(p, stmt, stmt->u.i.serialize);
      p = isl_printer_print_str(p, " = fifo_data;");
    }
    p = isl_printer_end_line(p);
  }

  p = isl_printer_indent(p, -2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "}");
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, 2);

  isl_ast_expr_free(local_index_packed);

  return p;
}

static __isl_give isl_printer *print_inter_trans_module_call(
    __isl_take isl_printer *p,
    struct autosa_hw_module *module, struct autosa_prog *prog,
    struct autosa_kernel *kernel, struct hls_info *hls, int arb, int boundary)
{
  int n = isl_id_list_n_id(module->inst_ids);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, module->name);
  p = isl_printer_print_str(p, "_inter_trans");
  if (boundary)
    p = isl_printer_print_str(p, "_boundary");
  if (prog->scop->options->autosa->use_cplusplus_template) {
    p = isl_printer_print_str(p, "<");
    for (int i = 0; i < n; i++) {
      if (i > 0) 
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "p");
      p = isl_printer_print_int(p, i);
    }
    p = isl_printer_print_str(p, ">");
  }
  if (hls->target == CATAPULT_HW) {
    p = isl_printer_print_str(p, "_inst.run");
  }
  p = isl_printer_print_str(p, "(");
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, 2);
  p = isl_printer_start_line(p);
  p = print_module_arguments(p, prog, kernel, module, 0,
                             hls->target, 1, arb, boundary, 0);
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, -2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, ");");
  p = isl_printer_end_line(p);

  return p;
}

/* Print the function call for inter_transfer module. */
__isl_give isl_printer *autosa_kernel_print_inter_trans(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.f.module;
  struct autosa_kernel *kernel = module->kernel;
  struct autosa_prog *prog = kernel->prog;
  int boundary = stmt->u.f.boundary;

  if (hls->target == CATAPULT_HW) {    
    p = print_inter_trans_module_call(p, module, prog, kernel, hls, 0, boundary);
  } else {
    if (module->double_buffer)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "if (arb == 0) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);
    }

    p = print_inter_trans_module_call(p, module, prog, kernel, hls, 0, boundary);

    if (module->double_buffer)
    {
      p = isl_printer_indent(p, -2);
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "} else {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = print_inter_trans_module_call(p, module, prog, kernel, hls, 1, boundary);

      p = isl_printer_indent(p, -2);
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "}");
      p = isl_printer_end_line(p);
    }
  }

  return p;
}

static __isl_give isl_printer *print_intra_trans_module_call(
    __isl_take isl_printer *p,
    struct autosa_hw_module *module, struct autosa_prog *prog,
    struct autosa_kernel *kernel,
    struct hls_info *hls, int arb)
{
  int n = isl_id_list_n_id(module->inst_ids);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, module->name);
  p = isl_printer_print_str(p, "_intra_trans");
  if (prog->scop->options->autosa->use_cplusplus_template) {
    p = isl_printer_print_str(p, "<");
    for (int i = 0; i < n; i++) {
      if (i > 0) 
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "p");
      p = isl_printer_print_int(p, i);
    }
    p = isl_printer_print_str(p, ">");
  }
  if (hls->target == CATAPULT_HW) {
    p = isl_printer_print_str(p, "_inst.run");
  }
  p = isl_printer_print_str(p, "(");
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, 2);
  p = isl_printer_start_line(p);
  p = print_module_arguments(p, prog, kernel, module, 0, 
                             hls->target, 0, arb, 0, 0);
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, -2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, ");");
  p = isl_printer_end_line(p);

  return p;
}

/* Print the function call for intra_transfer module. */
__isl_give isl_printer *autosa_kernel_print_intra_trans(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.f.module;
  struct autosa_kernel *kernel = module->kernel;
  struct autosa_prog *prog = kernel->prog;

  if (hls->target == CATAPULT_HW) {
    p = print_intra_trans_module_call(p, module, prog, kernel, hls, 1);
  } else {
    if (module->double_buffer)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "if (arb == 0) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);
    }

    p = print_intra_trans_module_call(p, module, prog, kernel, hls, 0);

    if (module->double_buffer)
    {
      p = isl_printer_indent(p, -2);
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "} else {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = print_intra_trans_module_call(p, module, prog, kernel, hls, 1);

      p = isl_printer_indent(p, -2);
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "}");
      p = isl_printer_end_line(p);
    }
  }

  return p;
}

/* Print the function calls for inter_transfer and intra_tranfer modules. */
__isl_give isl_printer *autosa_kernel_print_inter_intra(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.f.module;
  struct autosa_kernel *kernel = module->kernel;
  struct autosa_prog *prog = kernel->prog;
  int boundary = stmt->u.f.boundary;

  if (module->double_buffer && hls->target != CATAPULT_HW)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (arb == 0) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);
  }

  /* inter_trans */
  p = print_inter_trans_module_call(p, module, prog, kernel, hls, 0, boundary);
  /* intra_trans */
  p = print_intra_trans_module_call(p, module, prog, kernel, hls, 0);

  if (module->double_buffer && hls->target != CATAPULT_HW)
  {
    p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "} else {");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, 2);

    /* inter_trans */
    p = print_inter_trans_module_call(p, module, prog, kernel, hls, 1, boundary);
    /* intra_trans */
    p = print_intra_trans_module_call(p, module, prog, kernel, hls, 1);

    p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "}");
    p = isl_printer_end_line(p);
  }

  return p;
}

/* Print the function calls for intra_transfer and inter_tranfer modules. */
__isl_give isl_printer *autosa_kernel_print_intra_inter(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.f.module;
  struct autosa_kernel *kernel = module->kernel;
  struct autosa_prog *prog = kernel->prog;
  int boundary = stmt->u.f.boundary;

  if (module->double_buffer && hls->target != CATAPULT_HW)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (arb == 0) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);
  }

  /* intra_trans */
  p = print_intra_trans_module_call(p, module, prog, kernel, hls, 0);
  /* inter_trans */
  p = print_inter_trans_module_call(p, module, prog, kernel, hls, 0, boundary);

  if (module->double_buffer && hls->target != CATAPULT_HW)
  {
    p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "} else {");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, 2);

    /* intra_trans */
    p = print_intra_trans_module_call(p, module, prog, kernel, hls, 1);
    /* inter_trans */
    p = print_inter_trans_module_call(p, module, prog, kernel, hls, 1, boundary);

    p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "}");
    p = isl_printer_end_line(p);
  }

  return p;
}

/* Print the state transfer for double buffers. */
__isl_give isl_printer *autosa_kernel_print_state_handle(
    __isl_take isl_printer *p,
    struct autosa_kernel_stmt *stmt, struct hls_info *hls)
{
  struct autosa_hw_module *module = stmt->u.f.module;
  isl_space *space;
  int n;

  if (hls->target == CATAPULT_HW)
    return p;

  if (module->in)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "intra_trans_en = 1;");
    p = isl_printer_end_line(p);
  }
  else
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "inter_trans_en = 1;");
    p = isl_printer_end_line(p);
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "arb = !arb;");
  p = isl_printer_end_line(p);

  if (module->in)
  {
    /* intra trans */
    space = module->intra_space;
  }
  else
  {
    /* inter trans */
    space = module->inter_space;
  }
  n = isl_space_dim(space, isl_dim_set);
  for (int i = 0; i < n; i++)
  {
    const char *name;
    name = isl_space_get_dim_name(space, isl_dim_set, i);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, "_prev = ");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  }

  return p;
}

/* Print the body for a module that connects to the DRAM with serialized data. 
 */
__isl_give isl_printer *print_module_serialize_body(
  __isl_take isl_printer *p, struct autosa_hw_module *module, struct hls_info *hls)
{
  isl_pw_qpolynomial *total_bound_pwq = module->io_groups[0]->array->local_array->serialize_bound;
  long int total_bound = -1;  
  int ele_size = module->io_groups[0]->array->size; // bytes
  total_bound = convert_pwqpoly_to_int(total_bound_pwq);
  int data_pack_in = module->data_pack_serialize;
  int data_pack_out = module->data_pack_inter;  
  char *fifo_name;
  isl_printer *p_str;
  isl_ctx *ctx = isl_printer_get_ctx(p);
  /* Extract the sparse information */
  int is_sparse = module->io_groups[0]->local_array->is_sparse;
  int vec_len = module->io_groups[0]->local_array->vec_len;
  int n_nzero = module->io_groups[0]->local_array->n_nzero;
  float compress_ratio = module->io_groups[0]->local_array->compress_ratio;
  int n_meta_data = module->io_groups[0]->local_array->n_meta_data;
  float eff_compress_ratio = module->io_groups[0]->local_array->eff_compress_ratio;

  int axi_stream = module->options->autosa->axi_stream;

  p_str = isl_printer_to_str(ctx);
  p_str = autosa_array_ref_group_print_fifo_name(module->io_groups[0], p_str);  
  fifo_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  
  if (data_pack_in == data_pack_out) {    
    if (module->in) { 
      char *new_fifo_name;

      if (hls->target == INTEL_HW)
        p = print_str_new_line(p, "#pragma loop_coalesce");
      else if (hls->target == CATAPULT_HW)
        p = print_str_new_line(p, "#pragma hls_pipeline_init_interval 1");

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");      
      if (is_sparse)
        p = isl_printer_print_int(p, total_bound / eff_compress_ratio / data_pack_in);
      else
        p = isl_printer_print_int(p, total_bound / data_pack_out);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
          
      if (hls->target == XILINX_HW)
        p = print_str_new_line(p, "#pragma HLS PIPELINE II=1");

      p = isl_printer_indent(p, 2);
      p = isl_printer_start_line(p);
      p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_out);
      p = isl_printer_print_str(p, " fifo_data;");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      if (axi_stream) {
        //char *fifo_name;
        //isl_printer *p_str;
        //p_str = isl_printer_to_str(ctx);
        //p_str = isl_printer_print_str(p_str,"fifo_");
        //p_str = isl_printer_print_str(p_str, module->io_groups[0]->array->name);
        //fifo_name = isl_printer_get_str(p_str);
        //isl_printer_free(p_str);

        if (hls->target == XILINX_HW)
          p = print_fifo_rw_xilinx(p, fifo_name, 1);
        else if (hls->target == INTEL_HW)
          p = print_fifo_rw_intel(p, fifo_name, 1);
        else if (hls->target == CATAPULT_HW)
          p = print_fifo_rw_catapult(p, fifo_name, 1);
        p = isl_printer_print_str(p, ";");

        //free(fifo_name);
      } else {
        p = isl_printer_print_str(p, module->io_groups[0]->array->name);
        p = isl_printer_print_str(p, "[i];");
      }
      p = isl_printer_end_line(p);

      new_fifo_name = concat(ctx, fifo_name, "local_out");
      p = isl_printer_start_line(p);
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, new_fifo_name, 0);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, new_fifo_name, 0);          
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, new_fifo_name, 0);          

      p = isl_printer_print_str(p, "fifo_data);");      
      p = isl_printer_end_line(p);
      free(new_fifo_name);

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");            
    } else {
      char *new_fifo_name;

      if (hls->target == INTEL_HW)
        p = print_str_new_line(p, "#pragma loop_coalesce");
      else if (hls->target == CATAPULT_HW)
        p = print_str_new_line(p, "#pragma hls_pipeline_init_interval 1");

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, total_bound / data_pack_out);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);

      if (hls->target == XILINX_HW)
        p = print_str_new_line(p, "#pragma HLS PIPELINE II=1");

      p = isl_printer_indent(p, 2);
      p = isl_printer_start_line(p);
      p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_in);      
      p = isl_printer_print_str(p, " fifo_data;");
      p = isl_printer_end_line(p);

      new_fifo_name = concat(ctx, fifo_name, "local_in");
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fifo_data = ");
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, new_fifo_name, 1);
      else if (hls->target == INTEL_HW)
        p = print_fifo_rw_intel(p, new_fifo_name, 1);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, new_fifo_name, 1);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      if (axi_stream) {
        //char *fifo_name;
        //isl_printer *p_str;
        //p_str = isl_printer_to_str(ctx);
        //p_str = isl_printer_print_str(p_str,"fifo_");
        //p_str = isl_printer_print_str(p_str, module->io_groups[0]->array->name);
        //fifo_name = isl_printer_get_str(p_str);
        //isl_printer_free(p_str);
        
        if (hls->target == XILINX_HW)
          p = print_fifo_rw_xilinx(p, fifo_name, 0);
        else if (hls->target == INTEL_HW)
          p = print_fifo_rw_intel(p, fifo_name, 0);
        else if (hls->target == CATAPULT_HW)
          p = print_fifo_rw_catapult(p, fifo_name, 0);
        p = isl_printer_print_str(p, "fifo_data);");
        p = isl_printer_print_str(p, ";");

        //free(fifo_name);        
      } else {
        p = isl_printer_print_str(p, module->io_groups[0]->array->name);
        p = isl_printer_print_str(p, "[i] = fifo_data;");
      }
      p = isl_printer_end_line(p);

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");

      free(new_fifo_name);
    }
  } else {    
    if (module->in) {
      char *new_fifo_name;

      /* [type] fifo_data; */
      p = isl_printer_start_line(p);      
      if (is_sparse)
        p = autosa_print_array_type_with_lane_sparse(p, module->io_groups[0]->array, data_pack_out);
      else
        p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_out);
      p = isl_printer_print_str(p, " fifo_data;");
      p = isl_printer_end_line(p);

      /* [type2] mem_data; */
      p = isl_printer_start_line(p);
      p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_in);
      p = isl_printer_print_str(p, " mem_data;");
      p = isl_printer_end_line(p);
      
      if (hls->target == XILINX_HW) {        
        if (data_pack_out == 1 && !is_sparse) {
          /* union {unsigned int ui; [type] ut;} u; */
          p = isl_printer_start_line(p);        
          p = isl_printer_print_str(p, "union {unsigned int ui; ");
          p = isl_printer_print_str(p, module->io_groups[0]->array->type);
          p = isl_printer_print_str(p, " ut;} u;");        
          p = isl_printer_end_line(p);
        }        
          
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int i = 0; i < ");
        if (is_sparse)
          p = isl_printer_print_int(p, total_bound / eff_compress_ratio / data_pack_in);
        else
          p = isl_printer_print_int(p, total_bound / data_pack_in);
        p = isl_printer_print_str(p, "; i++) {");
        p = isl_printer_end_line(p);
            
        p = print_str_new_line(p, "#pragma HLS PIPELINE II=1");            
        p = isl_printer_indent(p, 2);
  
        /* mem_data = array[]; */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "mem_data = ");
        if (axi_stream) {
          //char *fifo_name;
          //isl_printer *p_str;
          //p_str = isl_printer_to_str(ctx);
          //p_str = isl_printer_print_str(p_str,"fifo_");
          //p_str = isl_printer_print_str(p_str, module->io_groups[0]->array->name);
          //fifo_name = isl_printer_get_str(p_str);
          //isl_printer_free(p_str);

          if (hls->target == XILINX_HW)
            p = print_fifo_rw_xilinx(p, fifo_name, 1);
          else if (hls->target == INTEL_HW)
            p = print_fifo_rw_intel(p, fifo_name, 1);
          else if (hls->target == CATAPULT_HW)
            p = print_fifo_rw_catapult(p, fifo_name, 1);
          p = isl_printer_print_str(p, ";");

          //free(fifo_name);
        } else {
          p = isl_printer_print_str(p, module->io_groups[0]->array->name);
          p = isl_printer_print_str(p, "[i];");
        }
        p = isl_printer_end_line(p);
  
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int p = 0; p < ");
        if (is_sparse)
          p = isl_printer_print_int(p, data_pack_in / (n_nzero + n_meta_data) / data_pack_out);
        else
          p = isl_printer_print_int(p, data_pack_in / data_pack_out);
        p = isl_printer_print_str(p, "; p++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);
  
        if (is_sparse) {
          /* ap_uint<...> mem_data_tmp = mem_data(...); */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_int(p, ele_size * (n_nzero + n_meta_data) * 8 * data_pack_out);
          p = isl_printer_print_str(p, "> mem_data_tmp = mem_data(");
          p = isl_printer_print_int(p, ele_size * (n_nzero + n_meta_data) * 8 * data_pack_out - 1);
          p = isl_printer_print_str(p, ", 0);");
          p = isl_printer_end_line(p);

          /* mem_data = mem_data >> ...; */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data = mem_data >> ");
          p = isl_printer_print_int(p, ele_size * (n_nzero + n_meta_data) * 8 * data_pack_out);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);

          /* fifo_data.d = ... */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "fifo_data.d = (");
          for (int n = data_pack_out - 1; n >= 0; n--) {
            p = isl_printer_print_str(p, "(ap_uint<");
            p = isl_printer_print_int(p, ele_size * 8 * n_nzero);
            p = isl_printer_print_str(p, ">)");
            p = isl_printer_print_str(p, "mem_data_tmp(");
            p = isl_printer_print_int(p, n * ele_size * 8 * (n_nzero + n_meta_data) + ele_size * 8 * n_nzero - 1);
            p = isl_printer_print_str(p, ", ");
            p = isl_printer_print_int(p, n * ele_size * 8 * (n_nzero + n_meta_data));
            p = isl_printer_print_str(p, ")");
            if (n > 0) 
              p = isl_printer_print_str(p, ", ");
          }
          p = isl_printer_print_str(p, ");");
          p = isl_printer_end_line(p);

          /* fifo_data.i = ... */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "fifo_data.i = (");
          for (int n = data_pack_out - 1; n >= 0; n--) {
            p = isl_printer_print_str(p, "(ap_uint<8>)mem_data_tmp(");
            p = isl_printer_print_int(p, n * ele_size * 8 * (n_nzero + n_meta_data) + ele_size * 8 * n_nzero + 8 - 1);
            p = isl_printer_print_str(p, ", ");
            p = isl_printer_print_int(p, n * ele_size * 8 * (n_nzero + n_meta_data) + ele_size * 8 * n_nzero);
            p = isl_printer_print_str(p, ")");
            if (n > 0) 
              p = isl_printer_print_str(p, ", ");
          }
          p = isl_printer_print_str(p, ");");
          p = isl_printer_end_line(p);
        } else {
          /* fifo_data = mem_data(..,..); */
          p = isl_printer_start_line(p);
          if (data_pack_out == 1) {
            p = isl_printer_print_str(p, "u.ui = (unsigned int)mem_data(");
            p = isl_printer_print_int(p, ele_size * data_pack_out * 8 - 1);
            p = isl_printer_print_str(p, ", 0);");
            p = isl_printer_end_line(p);

            p = print_str_new_line(p, "fifo_data = u.ut;");
          } else {
            p = isl_printer_print_str(p, "fifo_data = mem_data(");
            p = isl_printer_print_int(p, ele_size * data_pack_out * 8 - 1);
            p = isl_printer_print_str(p, ", 0);");
          }
          p = isl_printer_end_line(p);

          /* mem_data = mem_data >> .. */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data = mem_data >> ");
          p = isl_printer_print_int(p, ele_size * data_pack_out * 8);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }
  
        new_fifo_name = concat(ctx, fifo_name, "local_out");
        p = isl_printer_start_line(p);        
        p = print_fifo_rw_xilinx(p, new_fifo_name, 0);
        p = isl_printer_print_str(p, "fifo_data);");
        p = isl_printer_end_line(p);
  
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
  
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");

        free(new_fifo_name);
      } else if (hls->target == INTEL_HW) {                  
        p = print_str_new_line(p, "#pragma loop_coalesce");

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int i = 0; i < ");
        p = isl_printer_print_int(p, total_bound / data_pack_in);
        p = isl_printer_print_str(p, "; i++) {");
        p = isl_printer_end_line(p);
                  
        p = isl_printer_indent(p, 2);
  
        /* mem_data = array[]; */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "mem_data = __burst_coalesced_load(&");
        p = isl_printer_print_str(p, module->io_groups[0]->array->name);
        p = isl_printer_print_str(p, "[i]);");
        p = isl_printer_end_line(p);
          
        /* [type] mem_data_split[n] */
        p = isl_printer_start_line(p);
        p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_out);
        p = isl_printer_print_str(p, " mem_data_split[");
        p = isl_printer_print_int(p, data_pack_in / data_pack_out);
        p = isl_printer_print_str(p, "];");
        p = isl_printer_end_line(p);

        for (int i = 0; i < data_pack_in / data_pack_out; i++) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data_split[");
          p = isl_printer_print_int(p, i);
          p = isl_printer_print_str(p, "].data = mem_data.data.s");
          for (int j = i * data_pack_out; j < i * data_pack_out + data_pack_out; j++) {
            p = isl_printer_print_str(p, vector_index[j]);
          }
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int p = 0; p < ");
        p = isl_printer_print_int(p, data_pack_in / data_pack_out);
        p = isl_printer_print_str(p, "; p++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);
  
        /* fifo_data = mem_data(..,..); */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "fifo_data = mem_data_split[p];");                
        p = isl_printer_end_line(p);
          
        new_fifo_name = concat(ctx, fifo_name, "local_out");
        p = isl_printer_start_line(p);
        p = print_fifo_rw_intel(p, new_fifo_name, 0);
        p = isl_printer_print_str(p, "fifo_data);");
        p = isl_printer_end_line(p);
  
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
  
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");

        free(new_fifo_name);        
      } else if (hls->target == CATAPULT_HW) {
        p = print_str_new_line(p, "#pragma hls_pipeline_init_interval 1");

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int i = 0; i < ");
        if (is_sparse)
          p = isl_printer_print_int(p, total_bound / eff_compress_ratio / data_pack_in);
        else
          p = isl_printer_print_int(p, total_bound / data_pack_in);
        p = isl_printer_print_str(p, "; i++) {");
        p = isl_printer_end_line(p);

        p = isl_printer_indent(p, 2);

        /* mem_data = array[]; */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "mem_data = ");
        p = isl_printer_print_str(p, module->io_groups[0]->array->name);
        p = isl_printer_print_str(p, "[i];");
        p = isl_printer_end_line(p);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int p = 0; p < ");
        if (is_sparse)
          p = isl_printer_print_int(p, data_pack_in / (n_nzero + n_meta_data) / data_pack_out);
        else
          p = isl_printer_print_int(p, data_pack_in / data_pack_out);
        p = isl_printer_print_str(p, "; p++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);

        if (is_sparse) {
          /* ap_uint<...> mem_data_tmp = mem_data(...); */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "ac_int<");
          p = isl_printer_print_int(p, ele_size * (n_nzero + n_meta_data) * 8 * data_pack_out);
          p = isl_printer_print_str(p, ", false> mem_data_tmp = mem_data.slc<");
          p = isl_printer_print_int(p, ele_size * (n_nzero + n_meta_data) * 8 * data_pack_out - 1);
          p = isl_printer_print_str(p, ">(0);");
          p = isl_printer_end_line(p);

          /* mem_data = mem_data >> ...; */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data = mem_data >> ");
          p = isl_printer_print_int(p, ele_size * (n_nzero + n_meta_data) * 8 * data_pack_out);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);

          /* fifo_data.d = ... */
          for (int n = 0; n < data_pack_out; n++) {
            p = isl_printer_start_line(p);

            p = isl_printer_print_str(p, "fifo_data.d.set_slc(");
            p = isl_printer_print_int(p, n * ele_size * 8 * n_nzero);
            p = isl_printer_print_str(p, ", ");

            p = isl_printer_print_str(p, "mem_data_tmp.slc<");
            p = isl_printer_print_int(p, n * ele_size * 8 * n_nzero);
            p = isl_printer_print_str(p, ">(");
            p = isl_printer_print_int(p, n * ele_size * 8 * (n_nzero + n_meta_data));
            p = isl_printer_print_str(p, "));");

            p = isl_printer_end_line(p);
          }          

          /* fifo_data.i = ... */
          for (int n = 0; n < data_pack_out; n++) {
            p = isl_printer_start_line(p);
            
            p = isl_printer_print_str(p, "fifo_data.i.set_slc(");
            p = isl_printer_print_int(p, 8 * n);
            p = isl_printer_print_str(p, ", ");

            p = isl_printer_print_str(p, "mem_data_tmp.slc<8>(");
            p = isl_printer_print_int(p, n * ele_size * 8 * (n_nzero + n_meta_data) + ele_size * 8 * n_nzero);
            p = isl_printer_print_str(p, "));");

            p = isl_printer_end_line(p);
          }          
        } else {
          /* fifo_data = mem_data(..,..); */
          //p = isl_printer_start_line(p);
          //if (data_pack_out == 1) {
          //  p = isl_printer_print_str(p, "u.ui = (unsigned int)mem_data(");
          //  p = isl_printer_print_int(p, ele_size * data_pack_out * 8 - 1);
          //  p = isl_printer_print_str(p, ", 0);");
          //  p = isl_printer_end_line(p);

          //  p = print_str_new_line(p, "fifo_data = u.ut;");
          //} else {
          //  p = isl_printer_print_str(p, "fifo_data = mem_data(");
          //  p = isl_printer_print_int(p, ele_size * data_pack_out * 8 - 1);
          //  p = isl_printer_print_str(p, ", 0);");
          //}
          //p = isl_printer_end_line(p);
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "fifo_data = mem_data.slc<");
          p = isl_printer_print_int(p, ele_size * data_pack_out * 8);
          p = isl_printer_print_str(p, ">(0);");
          p = isl_printer_end_line(p);

          /* mem_data = mem_data >> .. */
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data = mem_data >> ");
          p = isl_printer_print_int(p, ele_size * data_pack_out * 8);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }
  
        new_fifo_name = concat(ctx, fifo_name, "local_out");
        p = isl_printer_start_line(p);        
        p = print_fifo_rw_catapult(p, new_fifo_name, 0);
        p = isl_printer_print_str(p, "fifo_data);");
        p = isl_printer_end_line(p);
  
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
  
        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");

        free(new_fifo_name);
      }
    } else {
      char *new_fifo_name;
      if (hls->target == INTEL_HW)
        p = print_str_new_line(p, "#pragma loop_coalesce");
      else if (hls->target == CATAPULT_HW)
        p = print_str_new_line(p, "#pragma hls_pipeline_init_interval 1");

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, total_bound / data_pack_in);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
          
      if (hls->target == XILINX_HW)
        p = print_str_new_line(p, "#pragma HLS PIPELINE II=1");
      p = isl_printer_indent(p, 2);

      /* [type] fifo_data; */
      p = isl_printer_start_line(p);
      //if (data_pack_out == 1) {
      //  p = isl_printer_print_str(p, module->io_groups[0]->array->type);
      //} else {
      p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_out);
      //}
      p = isl_printer_print_str(p, " fifo_data;");
      p = isl_printer_end_line(p);      

      /* [type2] mem_data; */
      p = isl_printer_start_line(p);
      p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_in);      
      p = isl_printer_print_str(p, " mem_data;");
      p = isl_printer_end_line(p);            
      
      p = isl_printer_start_line(p);      
      if (data_pack_out == 1) {
        if (hls->target == XILINX_HW) {
          p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_int(p, module->io_groups[0]->array->size * 8);
          p = isl_printer_print_str(p, ">");
        } else if (hls->target == INTEL_HW) {
          p = isl_printer_print_str(p, module->io_groups[0]->array->type);
        } else if (hls->target == CATAPULT_HW) {
          p = isl_printer_print_str(p, "ac_int<");
          p = isl_printer_print_int(p, module->io_groups[0]->array->size * 8);
          p = isl_printer_print_str(p, ", false>");
        }
      } else {
        p = autosa_print_array_type_with_lane(p, module->io_groups[0]->array, data_pack_out);
      }
      p = isl_printer_print_str(p, " mem_data_split[");
      p = isl_printer_print_int(p, data_pack_in / data_pack_out);
      p = isl_printer_print_str(p, "];");
      p = isl_printer_end_line(p);

      if (hls->target == XILINX_HW)
        p = print_str_new_line(p, "#pragma HLS ARRAY_PARTITION variable=mem_data_split complete");
      
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int p = 0; p < ");
      p = isl_printer_print_int(p, data_pack_in / data_pack_out);
      p = isl_printer_print_str(p, "; p++) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = isl_printer_start_line(p);
      new_fifo_name = concat(ctx, fifo_name, "local_in");
      p = isl_printer_print_str(p, "fifo_data = ");
      if (hls->target == XILINX_HW)
        p = print_fifo_rw_xilinx(p, new_fifo_name, 1);
      else if (hls->target == INTEL_HW) 
        p = print_fifo_rw_intel(p, new_fifo_name, 1);
      else if (hls->target == CATAPULT_HW)
        p = print_fifo_rw_catapult(p, new_fifo_name, 1);
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      if (hls->target == XILINX_HW) {
        if (data_pack_out == 1) {
          p = print_str_new_line(p, "u.ut = fifo_data;");

          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data_split[n] = ap_uint<");
          p = isl_printer_print_int(p, module->io_groups[0]->array->size * 8);
          p = isl_printer_print_str(p, ">(u.ui);");
          p = isl_printer_end_line(p);
        } else {
          p = print_str_new_line(p, "mem_data_split[p] = fifo_data;");
        }
      } else if (hls->target == INTEL_HW) {
        p = print_str_new_line(p, "mem_data_split[p] = fifo_data;");
      } else if (hls->target == CATAPULT_HW) {
        p = print_str_new_line(p, "mem_data_split[p] = fifo_data;");
      }
      
      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");

      if (hls->target == XILINX_HW) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "mem_data = (");
        for (int i = data_pack_in / data_pack_out - 1; i >= 0; i--) {
          if (i < data_pack_in / data_pack_out - 1)
            p = isl_printer_print_str(p, ", ");
          p = isl_printer_print_str(p, "mem_data_split[");
          p = isl_printer_print_int(p, i);
          p = isl_printer_print_str(p, "]");
        }
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      } else if (hls->target == INTEL_HW) {
        int first = 1;
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "mem_data.data = ");
        p = isl_printer_print_str(p, "(");
        p = isl_printer_print_str(p, module->io_groups[0]->array->type);
        p = isl_printer_print_int(p, data_pack_in);
        p = isl_printer_print_str(p, ")(");

        for (int i = 0; i < data_pack_in / data_pack_out; i++) {
          if (!first)
            p = isl_printer_print_str(p, ", ");
          if (data_pack_out > 1) {
            p = isl_printer_print_str(p, "(");
            p = isl_printer_print_str(p, module->io_groups[0]->array->type);
            p = isl_printer_print_int(p, data_pack_out);
            p = isl_printer_print_str(p, ")");
          }
          p = isl_printer_print_str(p, "mem_data_split[");
          p = isl_printer_print_int(p, i);
          p = isl_printer_print_str(p, "]");
          if (data_pack_out > 1)  {
            p = isl_printer_print_str(p, ".data");
          }
          first = 0;
        }
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      } else if (hls->target == CATAPULT_HW) {
        for (int i = 0; i < data_pack_in / data_pack_out; i++) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "mem_data.set_slc(");
          p = isl_printer_print_int(p, i * data_pack_out * module->io_groups[0]->array->size * 8);
          p = isl_printer_print_str(p, ", mem_data_split[");
          p = isl_printer_print_int(p, i);
          p = isl_printer_print_str(p, "]);");
          p = isl_printer_end_line(p);
        }
      }

      if (hls->target == XILINX_HW || hls->target == CATAPULT_HW) {
        p = isl_printer_start_line(p);
        if (axi_stream) {
          //char *fifo_name;
          //isl_printer *p_str;
          //p_str = isl_printer_to_str(ctx);
          //p_str = isl_printer_print_str(p_str,"fifo_");
          //p_str = isl_printer_print_str(p_str, module->io_groups[0]->array->name);
          //fifo_name = isl_printer_get_str(p_str);
          //isl_printer_free(p_str);

          if (hls->target == XILINX_HW)
            p = print_fifo_rw_xilinx(p, fifo_name, 0);
          else if (hls->target == INTEL_HW)
            p = print_fifo_rw_intel(p, fifo_name, 0);
          else if (hls->target == CATAPULT_HW)
            p = print_fifo_rw_catapult(p, fifo_name, 0);
          p = isl_printer_print_str(p, "mem_data);");
          p = isl_printer_print_str(p, ";");

          //free(fifo_name);  
        } else {
          p = isl_printer_print_str(p, module->io_groups[0]->array->name);
          p = isl_printer_print_str(p, "[i] = mem_data;");
        }
        p = isl_printer_end_line(p);
      } else {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "__burst_coalesced_store(&");
        p = isl_printer_print_str(p, module->io_groups[0]->array->name);
        p = isl_printer_print_str(p, "[i], mem_data);");
        p = isl_printer_end_line(p);
      }

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");

      free(new_fifo_name);
    }
  }

  free(fifo_name);
  return p;
}

/* Print the macros for the sparse data structure. 
 */
isl_stat print_sparse_macros(struct autosa_kernel *kernel, struct hls_info *hls)
{
  isl_printer *p;

  p = isl_printer_to_file(kernel->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_str_new_line(p, "/* Sparse Macros */");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#define VEC_LEN ");
  p = isl_printer_print_int(p, kernel->vec_len);
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#define NON_ZERO_NUM ");
  p = isl_printer_print_int(p, kernel->n_nzero);
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#define META_DATA_NUM ");
  p = isl_printer_print_int(p, kernel->n_meta_data);
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))");

  p = print_str_new_line(p, "/* Sparse Macros */");
  p = isl_printer_end_line(p);  

  isl_printer_free(p);

  if (hls->hls == 0) {
    p = isl_printer_to_file(kernel->ctx, hls->host_h);
    p = isl_printer_set_output_format(p, ISL_FORMAT_C);
    p = print_str_new_line(p, "/* Sparse Macros */");
  
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#define VEC_LEN ");
    p = isl_printer_print_int(p, kernel->vec_len);
    p = isl_printer_end_line(p);
  
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#define NON_ZERO_NUM ");
    p = isl_printer_print_int(p, kernel->n_nzero);
    p = isl_printer_end_line(p);
  
    p = print_str_new_line(p, "#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)");
  
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#define META_DATA_NUM ");
    p = isl_printer_print_int(p, kernel->n_meta_data);
    p = isl_printer_end_line(p);
  
    p = print_str_new_line(p, "#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))");
  
    p = print_str_new_line(p, "/* Sparse Macros */");
    p = isl_printer_end_line(p);  
  
    isl_printer_free(p);    
  }

  return isl_stat_ok;
}

/* Print the arguments to a drain merge function declaration or call.
 * If "types" is set, then print a declaration (including the types of the arguments).
 * 
 * The arguments are printed in the following order:
 * - the module identifiers
 * - the parameters
 * - the host loop iterators
 * - the arrays accssed by the module
 */
__isl_give isl_printer *print_drain_merge_arguments(
    __isl_take isl_printer *p,
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group,
    struct autosa_drain_merge_func *func,
    int types,
    int hls)
{
  int first = 1;
  int nparam;
  int n;
  isl_space *space;
  const char *type;
  struct autosa_local_array_info *local_array;

  type = isl_options_get_ast_iterator_type(kernel->ctx);
  /* module identifiers */
  const char *dims[] = {"idx", "idy", "idz"};
  n = isl_id_list_n_id(func->inst_ids);
  for (int i = 0; i < n; ++i)
  {
    if (!first)
      p = isl_printer_print_str(p, ", ");
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, dims[i]);

    first = 0;
  }

  /* params */
  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (int i = 0; i < nparam; ++i)
  {
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

  /* Host iters */
  n = isl_space_dim(kernel->space, isl_dim_set);
  for (int i = 0; i < n; ++i)
  {
    const char *name;

    if (!first)
      p = isl_printer_print_str(p, ", ");
    name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
    if (types)
    {
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
    }
    p = isl_printer_print_str(p, name);

    first = 0;
  }

  /* Arrays */
  local_array = group->local_array;
  if (!first)
    p = isl_printer_print_str(p, ", ");
  if (types)
  {
    if (hls)
    {
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *");
    }
    else
    {
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">> &");
    }
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_to");
  }
  else
  {
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "[0]");
  }
  first = 0;

  if (!first)
    p = isl_printer_print_str(p, ", ");
  if (types)
  {
    if (hls)
    {
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *");
    }
    else
    {
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">> &");
    }
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_from");
  }
  else
  {
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "[idx]");
  }
  first = 0;

  return p;
}

struct print_hw_module_data
{
  struct hls_info *hls;
  struct autosa_prog *prog;
  struct autosa_hw_module *module;
  /* Used for double buffer codegen. Modify the printed iterator prefix. */
  const char *iterator_prefix;
};

/* Print the drained data merge functions. 
 */
isl_stat print_drain_merge_funcs(
    struct autosa_kernel *kernel,
    struct autosa_drain_merge_func **funcs, int n_funcs,
    struct hls_info *hls)
{
  isl_printer *p;
  isl_ctx *ctx;

  if (n_funcs == 0)
    return isl_stat_ok;

  ctx = kernel->ctx;
  if (!hls->hls)
    p = isl_printer_to_file(kernel->ctx, hls->host_h);
  else
    p = isl_printer_to_file(kernel->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  for (int i = 0; i < n_funcs; i++)
  {
    struct autosa_array_ref_group *group = funcs[i]->group;
    isl_ast_print_options *print_options;
    struct print_hw_module_data hw_data = {hls, NULL, NULL, NULL};

    p = print_str_new_line(p, "/* Helper Function */");
    p = isl_printer_start_line(p);
    if (hls->hls)
      p = isl_printer_print_str(p, "inline ");
    p = isl_printer_print_str(p, "void ");
    p = autosa_array_ref_group_print_prefix(group, p);
    p = isl_printer_print_str(p, "_drain_merge(");
    p = print_drain_merge_arguments(p, kernel, group, funcs[i], 1, hls->hls);
    p = isl_printer_print_str(p, "){");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);

    p = print_str_new_line(p, "/* Variable Declaration */");
    if (!hls->hls)
      p = print_func_iterators(p, hls->host_h, funcs[i]);
    else
      p = print_func_iterators(p, hls->kernel_h, funcs[i]);
    p = print_str_new_line(p, "/* Variable Declaration */");
    p = isl_printer_end_line(p);

    print_options = isl_ast_print_options_alloc(ctx);
    print_options = isl_ast_print_options_set_print_user(print_options,
                                                         &print_module_stmt, &hw_data);
    p = isl_ast_node_print(funcs[i]->device_tree, p, print_options);

    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
    p = print_str_new_line(p, "/* Helper Function */");
    p = isl_printer_end_line(p);
  }  
  isl_printer_free(p);

  return isl_stat_ok;
}

__isl_give isl_printer *print_module_stmt(__isl_take isl_printer *p,
                                          __isl_take isl_ast_print_options *print_options,
                                          __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  struct autosa_kernel_stmt *stmt;
  struct print_hw_module_data *hw_data = (struct print_hw_module_data *)(user);
  struct autosa_hw_module *module = hw_data->module;

  id = isl_ast_node_get_annotation(node);
  stmt = (struct autosa_kernel_stmt *)isl_id_get_user(id);
  isl_id_free(id);

  isl_ast_print_options_free(print_options);

  switch (stmt->type)
  {
    case AUTOSA_KERNEL_STMT_DOMAIN:
      return autosa_kernel_print_domain(p, stmt);
    case AUTOSA_KERNEL_STMT_IO:
      return autosa_kernel_print_io(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_TRANSFER:
    case AUTOSA_KERNEL_STMT_IO_DRAM:
      return autosa_kernel_print_io_transfer_wrapper(
        p, stmt, hw_data->hls, module->options->autosa->double_buffer_style == 0? hw_data->iterator_prefix : NULL);
    //case AUTOSA_KERNEL_STMT_IO_TRANSFER:
    //  return autosa_kernel_print_io_transfer(p, stmt, hw_data->hls, 
    //            module->options->autosa->double_buffer_style == 0?
    //              hw_data->iterator_prefix : NULL);
    //case AUTOSA_KERNEL_STMT_IO_DRAM:
    //  return autosa_kernel_print_io_dram(p, stmt, hw_data->hls,
    //      module->options->autosa->double_buffer_style == 0? hw_data->iterator_prefix : NULL);
    case AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTER_TRANS:
      return autosa_kernel_print_inter_trans(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTRA_TRANS:
      return autosa_kernel_print_intra_trans(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTER_INTRA:
      return autosa_kernel_print_inter_intra(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTRA_INTER:
      return autosa_kernel_print_intra_inter(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_MODULE_CALL_STATE_HANDLE:
      return autosa_kernel_print_state_handle(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_DRAIN_MERGE:
      return autosa_kernel_print_drain_merge(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_HOST_SERIALIZE:
      return autosa_kernel_print_host_serialize(p, stmt, hw_data->hls);
  }

  return p;
}

/* Print the host serialization functions.
 */
isl_stat print_host_serialize_funcs(
    struct autosa_kernel *kernel,
    struct autosa_hw_module **modules,
    int n_modules, struct hls_info *hls)
{
  isl_printer *p;
  isl_ctx *ctx;

  ctx = kernel->ctx;
  if (!hls->hls)
    p = isl_printer_to_file(ctx, hls->host_h);
  else
    p = isl_printer_to_file(ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  for (int i = 0; i < n_modules; i++) {
    struct autosa_hw_module *module = modules[i];
    isl_ast_print_options *print_options;
    struct print_hw_module_data hw_data = {hls, NULL, NULL, NULL};

    if (module->serialize_tree) {
      p = print_str_new_line(p, "/* Helper Function */");
      p = isl_printer_start_line(p);
      if (hls->hls)
        p = isl_printer_print_str(p, "inline ");
      p = isl_printer_print_str(p, "void ");
      if (module->in) {
        p = isl_printer_print_str(p, "host_serialize_");
      } else {
        p = isl_printer_print_str(p, "host_deserialize_");
      }      
      p = isl_printer_print_str(p, module->io_groups[0]->array->name);
      p = isl_printer_print_str(p, "(");      
      p = print_host_serialize_arguments(p, kernel, module->io_groups[0], module, 1, hls->hls);
      p = isl_printer_print_str(p, "){");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = print_str_new_line(p, "/* Variable Declaration */");
      p = print_str_new_line(p, "unsigned int cnt = 0;");      
      p = print_str_new_line(p, "/* Variable Declaration */");
      p = isl_printer_end_line(p);

      print_options = isl_ast_print_options_alloc(ctx);
      print_options = isl_ast_print_options_set_print_user(print_options,
                                                           &print_module_stmt, &hw_data);
            
      p = isl_ast_node_print(module->serialize_tree, p, print_options);

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");
      p = print_str_new_line(p, "/* Helper Function */");
      p = isl_printer_end_line(p);
    }    
  }
  isl_printer_free(p);

  return isl_stat_ok;
}