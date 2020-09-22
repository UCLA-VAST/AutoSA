/* Defines functions used for AutoSA structs. */

#include <isl/id.h>
#include <cJSON/cJSON.h>

#include "autosa_common.h"
#include "autosa_utils.h"
#include "autosa_print.h"

/****************************************************************
 * AutoSA kernel
 ****************************************************************/
/* Free the AutoSA kernel struct. */
void *autosa_kernel_free(struct autosa_kernel *kernel)
{
  if (!kernel)
    return NULL;

  isl_schedule_free(kernel->schedule);
  isl_ast_node_free(kernel->tree);
  isl_union_map_free(kernel->sizes);
  isl_union_map_free(kernel->used_sizes);
  isl_union_set_free(kernel->core);
  isl_set_free(kernel->context);
  isl_multi_pw_aff_free(kernel->sa_grid_size);
  isl_union_set_free(kernel->arrays);
  isl_union_pw_multi_aff_free(kernel->copy_schedule);
  isl_space_free(kernel->space);
  isl_id_list_free(kernel->block_ids);
  isl_id_list_free(kernel->thread_ids);
  isl_id_list_free(kernel->pe_ids);
  isl_union_set_free(kernel->pe_filter);
  isl_multi_pw_aff_free(kernel->grid_size);
  isl_ast_expr_free(kernel->grid_size_expr);
  isl_union_pw_multi_aff_free(kernel->contraction);
  isl_union_set_free(kernel->expanded_domain);
  isl_set_free(kernel->host_domain);
  isl_union_set_free(kernel->domain);
  for (int i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];
    for (int j = 0; j < array->n_group; ++j)
      autosa_array_ref_group_free(array->groups[j]);
    free(array->groups);
    for (int j = 0; j < array->n_pe_group; ++j)
      autosa_array_ref_group_free(array->pe_groups[j]);
    free(array->pe_groups);
    for (int j = 0; j < array->n_io_group; ++j)
      autosa_array_ref_group_free(array->io_groups[j]);
    free(array->io_groups);
    autosa_array_ref_group_free(array->drain_group);

    isl_multi_pw_aff_free(array->bound);
    isl_ast_expr_free(array->bound_expr);
    
    isl_pw_qpolynomial_free(array->serialize_bound);    
  }
  if (kernel->array)
    free(kernel->array);

  for (int i = 0; i < kernel->n_var; i++)
  {
    free(kernel->var[i].name);
    isl_vec_free(kernel->var[i].size);
  }
  free(kernel->var);  

  free(kernel);
  return NULL;
}

/* Copy a new AutoSA kernel struct. */
struct autosa_kernel *autosa_kernel_copy(struct autosa_kernel *kernel)
{
  struct autosa_kernel *kernel_dup = (struct autosa_kernel *)malloc(
      sizeof(struct autosa_kernel));
  kernel_dup->ctx = kernel->ctx;
  kernel_dup->schedule = isl_schedule_copy(kernel->schedule);
  kernel_dup->scop = kernel->scop;
  kernel_dup->options = kernel->options;
  kernel_dup->n_sa_dim = kernel->n_sa_dim;
  for (int i = 0; i < kernel->n_sa_dim; i++)
  {
    kernel_dup->sa_dim[i] = kernel->sa_dim[i];
  }
  kernel_dup->array_part_w = kernel->array_part_w;
  kernel_dup->space_w = kernel->space_w;
  kernel_dup->time_w = kernel->time_w;
  kernel_dup->type = kernel->type;
  kernel_dup->sa_grid_size = isl_multi_pw_aff_copy(kernel->sa_grid_size);
  kernel_dup->sizes = isl_union_map_copy(kernel->sizes);
  kernel_dup->used_sizes = isl_union_map_copy(kernel->used_sizes);
  kernel_dup->id = kernel->id;
  kernel_dup->space_time_id = kernel->space_time_id;
  kernel_dup->core = isl_union_set_copy(kernel->core);
  kernel_dup->arrays = isl_union_set_copy(kernel->arrays);
  kernel_dup->n_array = kernel->n_array;
  kernel_dup->array = kernel->array;
  kernel_dup->copy_schedule = isl_union_pw_multi_aff_copy(kernel->copy_schedule);
  kernel_dup->copy_schedule_dim = kernel->copy_schedule_dim;
  kernel_dup->space = isl_space_copy(kernel->space);
  kernel_dup->tree = isl_ast_node_copy(kernel->tree);
  kernel_dup->n_var = kernel->n_var;
  kernel_dup->var = kernel->var;
  kernel_dup->block_ids = isl_id_list_copy(kernel->block_ids);
  kernel_dup->thread_ids = isl_id_list_copy(kernel->thread_ids);
  kernel_dup->pe_ids = isl_id_list_copy(kernel->pe_ids);
  kernel_dup->pe_filter = isl_union_set_copy(kernel->pe_filter);
  kernel_dup->n_grid = kernel->n_grid;
  kernel_dup->n_block = kernel->n_block;
  for (int i = 0; i < kernel->n_grid; i++)
  {
    kernel_dup->grid_dim[i] = kernel->grid_dim[i];
  }
  for (int i = 0; i < kernel->n_block; i++)
  {
    kernel_dup->block_dim[i] = kernel->block_dim[i];
  }
  kernel_dup->grid_size = isl_multi_pw_aff_copy(kernel->grid_size);
  kernel_dup->grid_size_expr = isl_ast_expr_copy(kernel->grid_size_expr);
  kernel_dup->context = isl_set_copy(kernel->context);
  kernel_dup->contraction = isl_union_pw_multi_aff_copy(kernel->contraction);
  kernel_dup->expanded_domain = isl_union_set_copy(kernel->expanded_domain);
  kernel_dup->host_domain = isl_set_copy(kernel->host_domain);
  kernel_dup->domain = isl_union_set_copy(kernel->domain);
  kernel_dup->single_statement = kernel->single_statement;

  return kernel_dup;
}

/* Allocate a new AutoSA kernel struct with the given schedule. */
struct autosa_kernel *autosa_kernel_from_schedule(__isl_take isl_schedule *schedule)
{
  struct autosa_kernel *kernel = (struct autosa_kernel *)malloc(
      sizeof(struct autosa_kernel));
  kernel->ctx = isl_schedule_get_ctx(schedule);
  kernel->schedule = schedule;
  kernel->scop = NULL;
  kernel->prog = NULL;
  kernel->options = NULL;
  kernel->n_sa_dim = 0;
  kernel->array_part_w = 0;
  kernel->space_w = 0;
  kernel->time_w = 0;
  kernel->type = 0;
  kernel->sa_grid_size = NULL;
  kernel->sizes = NULL;
  kernel->used_sizes = NULL;
  kernel->id = 0;
  kernel->core = NULL;
  kernel->arrays = NULL;
  kernel->n_array = 0;
  kernel->array = NULL;
  kernel->copy_schedule = NULL;
  kernel->copy_schedule_dim = -1;
  kernel->space = NULL;
  kernel->tree = NULL;
  kernel->n_var = 0;
  kernel->var = NULL;
  kernel->block_ids = NULL;
  kernel->thread_ids = NULL;
  kernel->pe_ids = NULL;
  kernel->pe_filter = NULL;
  kernel->n_grid = 0;
  kernel->n_block = 0;
  kernel->grid_size = NULL;
  kernel->grid_size_expr = NULL;
  kernel->context = NULL;
  kernel->contraction = NULL;
  kernel->expanded_domain = NULL;
  kernel->host_domain = NULL;
  kernel->domain = NULL;
  kernel->single_statement = 0;

  return kernel;
}

struct autosa_kernel *autosa_kernel_alloc(isl_ctx *ctx, struct ppcg_scop *scop)
{
  struct autosa_kernel *kernel;
  isl_space *space;
  isl_map *id;

  if (!scop)
    return NULL;

  kernel = isl_calloc_type(ctx, struct autosa_kernel);
  if (!kernel)
    return NULL;

  kernel->ctx = ctx;
  kernel->scop = scop;
  kernel->prog = NULL;
  kernel->options = NULL;
  kernel->n_sa_dim = 0;
  kernel->array_part_w = 0;
  kernel->space_w = 0;
  kernel->time_w = 0;
  kernel->type = 0;
  kernel->sa_grid_size = NULL;
  kernel->sizes = NULL;
  kernel->used_sizes = NULL;
  kernel->id = 0;
  kernel->core = NULL;
  kernel->arrays = NULL;
  kernel->n_array = 0;
  kernel->array = NULL;
  kernel->copy_schedule = NULL;
  kernel->copy_schedule_dim = -1;
  kernel->space = NULL;
  kernel->tree = NULL;
  kernel->n_var = 0;
  kernel->var = NULL;
  kernel->block_ids = NULL;
  kernel->thread_ids = NULL;
  kernel->pe_ids = NULL;
  kernel->pe_filter = NULL;
  kernel->n_grid = 0;
  kernel->n_block = 0;
  kernel->grid_size = NULL;
  kernel->grid_size_expr = NULL;
  kernel->context = NULL;
  kernel->contraction = NULL;
  kernel->expanded_domain = NULL;
  kernel->host_domain = NULL;
  kernel->domain = NULL;
  kernel->single_statement = 0;  

  return kernel;
}

/****************************************************************
 * AutoSA access
 ****************************************************************/
/* Create an identical mapping. */
static __isl_give isl_map *same(__isl_take isl_space *domain_space)
{
  isl_space *space;
  isl_aff *aff;
  isl_multi_aff *next;

  space = isl_space_map_from_set(domain_space);
  next = isl_multi_aff_identity(space);

  return isl_map_from_multi_aff(next);
}

/* Construct a map from domain_space to domain_space that increments
 * the dimension at position "pos" and leaves all other dimensions constant. 
 */
static __isl_give isl_map *next(__isl_take isl_space *domain_space, int pos)
{
  isl_space *space;
  isl_aff *aff;
  isl_multi_aff *next;

  space = isl_space_map_from_set(domain_space);
  next = isl_multi_aff_identity(space);
  aff = isl_multi_aff_get_aff(next, pos);
  aff = isl_aff_add_constant_si(aff, 1);
  next = isl_multi_aff_set_aff(next, pos, aff);

  return isl_map_from_multi_aff(next);
}

/* Check is the "access" has stride-0 access at dim "pos".
 * The access is already transformed to scheduling domains. 
 * We first create an identical mapping "next_element"that maps the accessed 
 * elements to the same elements. 
 * Then, we create a mapping "map" that maps the array elements accessed by the 
 * current iteration to the elements accssed by the next iteration.
 * We examine if the access is stride-0 by testing if map is the subset of 
 * "next_element".
 */
isl_bool access_is_stride_zero(__isl_keep isl_map *access, int pos)
{
  isl_space *space;
  int dim;
  isl_map *next_element, *map, *next_iter;
  isl_set *accessed;
  isl_bool empty, zero;

  space = isl_map_get_space(access);
  space = isl_space_range(space);
  dim = isl_space_dim(space, isl_dim_set);
  if (dim == 0)
    next_element = isl_map_empty(isl_space_map_from_set(space));
  else
    next_element = same(space);

  accessed = isl_map_range(isl_map_copy(access));
  map = isl_map_copy(next_element);
  map = isl_map_intersect_domain(map, isl_set_copy(accessed));
  map = isl_map_intersect_range(map, accessed);
  empty = isl_map_is_empty(map);
  isl_map_free(map);

  if (empty < 0 || empty)
  {
    isl_map_free(next_element);
    return empty;
  }

  space = isl_map_get_space(access);
  space = isl_space_domain(space);
  next_iter = next(space, isl_map_dim(access, isl_dim_in) - 1);
  map = isl_map_apply_domain(next_iter, isl_map_copy(access));
  map = isl_map_apply_range(map, isl_map_copy(access));
  zero = isl_map_is_subset(map, next_element);

  isl_map_free(next_element);
  isl_map_free(map);

  return zero;
}

/* Check is the "access" has stride-1 access at dim "pos".
 * The access is already transformed to scheduling domains. 
 * We first create a mapping "next_element"that maps the accessed 
 * elements to the elements with a stride of one. 
 * Then, we create a mapping "map" that maps the array elements accessed by the 
 * current iteration to the elements accssed by the next iteration.
 * We examine if the access is stride-1 by testing if map is the subset of 
 * "next_element".
 */
isl_bool access_is_stride_one(__isl_keep isl_map *access, int pos)
{
  isl_space *space;
  int dim;
  isl_map *next_element, *map, *next_iter;
  isl_set *accessed;
  isl_bool empty, coalesced;

  space = isl_map_get_space(access);
  space = isl_space_range(space);
  dim = isl_space_dim(space, isl_dim_set);
  if (dim == 0)
    next_element = isl_map_empty(isl_space_map_from_set(space));
  else
    next_element = next(space, pos);

  accessed = isl_map_range(isl_map_copy(access));
  map = isl_map_copy(next_element);
  map = isl_map_intersect_domain(map, isl_set_copy(accessed));
  map = isl_map_intersect_range(map, accessed);
  empty = isl_map_is_empty(map);
  isl_map_free(map);

  if (empty < 0 || empty)
  {
    isl_map_free(next_element);
    return empty;
  }

  space = isl_map_get_space(access);
  space = isl_space_domain(space);
  next_iter = next(space, isl_map_dim(access, isl_dim_in) - 1);
  //#ifdef _DEBUG
  //  isl_printer *pd = isl_printer_to_file(isl_map_get_ctx(access), stdout);
  //  pd = isl_printer_print_map(pd, next_iter);
  //  pd = isl_printer_end_line(pd);
  //#endif
  map = isl_map_apply_domain(next_iter, isl_map_copy(access));
  map = isl_map_apply_range(map, isl_map_copy(access));
  if (isl_map_is_empty(map))
  {
    isl_map_free(next_element);
    isl_map_free(map);
    return isl_bool_false;
  }
  coalesced = isl_map_is_subset(map, next_element);
  //#ifdef _DEBUG
  //  pd = isl_printer_print_map(pd, map);
  //  pd = isl_printer_end_line(pd);
  //  pd = isl_printer_print_map(pd, next_element);
  //  pd = isl_printer_end_line(pd);
  //  isl_printer_free(pd);
  //#endif

  isl_map_free(next_element);
  isl_map_free(map);

  return coalesced;
}

void *autosa_acc_free(struct autosa_acc *acc)
{
  if (!acc)
    return NULL;

  isl_map_free(acc->tagged_map);
  isl_map_free(acc->map);
  isl_space_free(acc->id);

  free(acc);

  return NULL;
}

/****************************************************************
 * AutoSA dep
 ****************************************************************/
/* Free up the dependence. */
void *autosa_dep_free(__isl_take struct autosa_dep *dep)
{
  if (!dep)
    return NULL;

  if (dep->src)
    dep->src = isl_id_free(dep->src);
  if (dep->dest)
    dep->dest = isl_id_free(dep->dest);
  if (dep->disvec)
    isl_vec_free(dep->disvec);
  if (dep->src_sched_domain)
    isl_set_free(dep->src_sched_domain);
  if (dep->dest_sched_domain)
    isl_set_free(dep->dest_sched_domain);
  if (dep->isl_dep)
    isl_basic_map_free(dep->isl_dep);

  free(dep);

  return NULL;
}

/****************************************************************
 * AutoSA iterator
 ****************************************************************/

__isl_null struct autosa_iter *autosa_iter_free(struct autosa_iter *iter)
{
  if (!iter)
    return NULL;

  free(iter->name);
  free(iter->ts_name);
  isl_aff_free(iter->lb);
  isl_aff_free(iter->ub);

  free(iter);

  return NULL;
}

/****************************************************************
 * AutoSA array
 ****************************************************************/

static void free_array_info(struct autosa_prog *prog)
{
  int i;

  for (i = 0; i < prog->n_array; ++i)
  {
    free(prog->array[i].type);
    free(prog->array[i].name);
    isl_multi_pw_aff_free(prog->array[i].bound);
    isl_ast_expr_free(prog->array[i].bound_expr);
    isl_space_free(prog->array[i].space);
    isl_set_free(prog->array[i].declared_extent);
    isl_set_free(prog->array[i].extent);
    isl_ast_expr_free(prog->array[i].declared_size);
    free(prog->array[i].refs);
    isl_union_map_free(prog->array[i].dep_order);
  }
  free(prog->array);
}

/* Is the array "array" being extracted a read-only scalar?
 *
 * That is, is "array" a scalar that is never possibly written to.
 * An array containing structures is never considered to be a scalar.
 */
static int is_read_only_scalar(struct autosa_array_info *array,
                               struct autosa_prog *prog)
{
  isl_set *space;
  isl_union_map *write;
  int empty;

  if (array->has_compound_element)
    return 0;
  if (array->n_index != 0)
    return 0;

  write = isl_union_map_copy(prog->may_write);
  space = isl_set_universe(isl_space_copy(array->space));
  write = isl_union_map_intersect_range(write,
                                        isl_union_set_from_set(space));
  empty = isl_union_map_is_empty(write);
  isl_union_map_free(write);

  return empty;
}

/* Compute and return the extent of "array", taking into account the set of
 * accessed elements.
 *
 * In particular, the extent in the outer dimension is taken
 * from "accessed", while the extents in the remaining dimensions
 * are taken from array->extent.
 *
 * The extent in the outer dimension cannot be taken from array->extent
 * because that may be unbounded.  Furthermore, even if it is bounded,
 * it may be larger than the piece of the array that is being accessed.
 */
static __isl_give isl_set *compute_extent(struct pet_array *array,
                                          __isl_keep isl_set *accessed)
{
  int n_index;
  isl_id *id;
  isl_set *outer;
  isl_set *extent;

  extent = isl_set_copy(array->extent);

  n_index = isl_set_dim(accessed, isl_dim_set);
  if (n_index == 0)
    return extent;

  extent = isl_set_project_out(extent, isl_dim_set, 0, 1);
  outer = isl_set_copy(accessed);
  outer = isl_set_project_out(outer, isl_dim_set, 1, n_index - 1);
  extent = isl_set_flat_product(outer, extent);
  id = isl_set_get_tuple_id(accessed);
  extent = isl_set_set_tuple_id(extent, id);

  return extent;
}

/* Return the name of the outer array (of structs) accessed by "access".
 */
static const char *get_outer_array_name(__isl_keep isl_map *access)
{
  isl_space *space;
  const char *name;

  space = isl_space_range(isl_map_get_space(access));
  while (space && isl_space_is_wrapping(space))
    space = isl_space_domain(isl_space_unwrap(space));
  name = isl_space_get_tuple_name(space, isl_dim_set);
  isl_space_free(space);

  return name;
}

/* Collect all references to the given array and store pointers to them
 * in array->refs.
 */
static isl_stat collect_references(struct autosa_prog *prog,
                                   struct autosa_array_info *array)
{
  int i;
  int n;

  n = 0;
  for (i = 0; i < prog->n_stmts; ++i)
  {
    struct autosa_stmt *stmt = &prog->stmts[i];
    struct autosa_stmt_access *access;

    for (access = stmt->accesses; access; access = access->next)
    {
      const char *name;
      name = get_outer_array_name(access->access);
      if (name && !strcmp(array->name, name))
        n++;
    }
  }

  array->refs = isl_alloc_array(prog->ctx, struct autosa_stmt_access *, n);
  if (!array->refs)
    return isl_stat_error;
  array->n_ref = n;

  n = 0;
  for (i = 0; i < prog->n_stmts; ++i)
  {
    struct autosa_stmt *stmt = &prog->stmts[i];
    struct autosa_stmt_access *access;

    for (access = stmt->accesses; access; access = access->next)
    {
      const char *name;
      name = get_outer_array_name(access->access);
      if (!name || strcmp(array->name, name))
        continue;

      array->refs[n++] = access;
    }
  }

  return isl_stat_ok;
}

/* Is "array" only accessed as individual, fixed elements?
 * That is, does each access to "array" access a single, fixed element?
 */
static isl_bool only_fixed_element_accessed(struct autosa_array_info *array)
{
  int i;

  for (i = 0; i < array->n_ref; ++i)
    if (!array->refs[i]->fixed_element)
      return isl_bool_false;

  return isl_bool_true;
}

/* Compute bounds on the host array "pa" based on the corresponding
 * accessed elements in "arrays"
 * and collect all references to the array.
 * Store the results in "info".
 *
 * If the array is zero-dimensional and does not contain structures,
 * i.e., if the array is a scalar, we check whether it is read-only.
 * We also check whether the array is accessed at all.
 */
static isl_stat extract_array_info(struct autosa_prog *prog,
                                   struct autosa_array_info *info, struct pet_array *pa,
                                   __isl_keep isl_union_set *arrays)
{
  int empty;
  const char *name;
  int n_index;
  isl_multi_pw_aff *bounds;
  isl_set *accessed, *extent;

  n_index = isl_set_dim(pa->extent, isl_dim_set);
  name = isl_set_get_tuple_name(pa->extent);

  info->space = isl_set_get_space(pa->extent);
  info->name = strdup(name);
  info->n_index = n_index;
  info->linearize = prog->scop->options->linearize_device_arrays;

  info->type = strdup(pa->element_type);
  info->size = pa->element_size;
  info->local = pa->declared && !pa->exposed;
  info->has_compound_element = pa->element_is_record;
  info->read_only_scalar = is_read_only_scalar(info, prog);

  info->declared_extent = isl_set_copy(pa->extent);
  accessed = isl_union_set_extract_set(arrays,
                                       isl_space_copy(info->space));
  empty = isl_set_is_empty(accessed);
  extent = compute_extent(pa, accessed);
  isl_set_free(accessed);
  info->extent = extent;
  if (empty < 0)
    return isl_stat_error;
  info->accessed = !empty;
  bounds = ppcg_size_from_extent(isl_set_copy(extent));
  bounds = isl_multi_pw_aff_gist(bounds, isl_set_copy(prog->context));
  if (!bounds)
    return isl_stat_error;
  if (!isl_multi_pw_aff_is_cst(bounds))
    info->linearize = 1;
  info->bound = bounds;

  if (collect_references(prog, info) < 0)
    return isl_stat_error;
  info->only_fixed_element = only_fixed_element_accessed(info);

  /* AutoSA Extended */
  info->n_lane = 0;
  info->local_array = NULL;
  info->copy_in = 0;
  info->copy_out = 0;
  /* AutoSA Extended */

  return isl_stat_ok;
}

/* Remove independence from the order constraints "order" on array "array".
 * Since the pairs of iterations in the filter relation of an independence
 * are guaranteed to be completely independent by the user, there is
 * no need to ensure that live ranges are ordered along those pairs.
 * We make an exception for local variables, though, as the independence
 * guarantee does not apply to those.
 *
 * The order constraints are used in two places.
 * Those on scalars are used in check_scalar_live_ranges to check if
 * we need to force the scalar to be private.  Any non-local scalar
 * should not be forced scalar if it only appears in independent loops.
 * Those on non-scalars are added to the coincidence constraints
 * in compute_schedule because we do not support any array expansion.
 * Accesses to non-local arrays should not prevent a loop from being
 * considered coincident so we should indeed remove those constraints
 * from the order constraints.
 */
static __isl_give isl_union_map *remove_independences(struct autosa_prog *prog,
                                                      struct autosa_array_info *array, __isl_take isl_union_map *order)
{
  int i;

  for (i = 0; i < prog->scop->pet->n_independence; ++i)
  {
    struct pet_independence *pi = prog->scop->pet->independences[i];
    if (isl_union_set_contains(pi->local, array->space))
      continue;

    order = isl_union_map_subtract(order,
                                   isl_union_map_copy(pi->filter));
  }

  return order;
}

/* Can "array" be mapped to private memory?
 * That is, is it only accessed as individual elements with
 * constant index expressions?
 */
static isl_bool autosa_array_can_be_private(struct autosa_array_info *array)
{
  if (!array)
    return isl_bool_error;
  return array->only_fixed_element ? isl_bool_true : isl_bool_false;
}

/* For each array in "prog", store the (untagged) order dependences
 * derived from the array in array->dep_order.
 * In particular, consider all references that access the given array
 * and take the order dependences that have one of these references
 * as source.  (Since an order dependence relates two references to
 * the same array, the target of these order dependences will also
 * be one of these references.)
 * Additionally, store the union of these array->dep_order relations
 * for all arrays that cannot be mapped to private memory in prog->array_order.
 */
static void collect_order_dependences(struct autosa_prog *prog)
{
  int i;
  isl_space *space;
  isl_union_map *accesses;

  space = isl_union_map_get_space(prog->read);
  prog->array_order = isl_union_map_empty(space);

  accesses = isl_union_map_copy(prog->scop->tagged_reads);
  accesses = isl_union_map_union(accesses,
                                 isl_union_map_copy(prog->scop->tagged_may_writes));
  accesses = isl_union_map_universe(accesses);
  accesses = isl_union_map_apply_range(accesses,
                                       isl_union_map_copy(prog->to_outer));

  for (i = 0; i < prog->n_array; ++i)
  {
    struct autosa_array_info *array = &prog->array[i];
    isl_set *set;
    isl_union_set *uset;
    isl_union_map *order;

    set = isl_set_universe(isl_space_copy(array->space));
    uset = isl_union_set_from_set(set);
    uset = isl_union_map_domain(
        isl_union_map_intersect_range(isl_union_map_copy(accesses),
                                      uset));
    order = isl_union_map_copy(prog->scop->tagged_dep_order);
    order = isl_union_map_intersect_domain(order, uset);
    order = isl_union_map_zip(order);
    order = isl_union_set_unwrap(isl_union_map_domain(order));
    order = remove_independences(prog, array, order);
    array->dep_order = order;

    if (autosa_array_can_be_private(array))
      continue;

    prog->array_order = isl_union_map_union(prog->array_order,
                                            isl_union_map_copy(array->dep_order));
  }

  isl_union_map_free(accesses);
}

/* Construct a autosa_array_info for each array referenced by prog->scop and
 * collect them in prog->array.
 * 
 * The sizes are based on the extents and the set of possibly accessed
 * elements by "prog".
 * If there are any member accesses involved, then they are first mapped
 * to the outer arrays of structs.
 * Only extract autosa_array_info entries for these outer arrays.
 * 
 * If we are allowing live range reordering, then also set 
 * the dep_order field. Otherwise leave it NULL.
 */
isl_stat collect_array_info(struct autosa_prog *prog)
{
  int i;
  isl_stat r = isl_stat_ok;
  isl_union_set *arrays;

  prog->n_array = 0;
  prog->array = isl_calloc_array(prog->ctx,
                                 struct autosa_array_info, prog->scop->pet->n_array);
  if (!prog->array)
    return isl_stat_error;

  arrays = isl_union_map_range(isl_union_map_copy(prog->read));
  arrays = isl_union_set_union(arrays,
                               isl_union_map_range(isl_union_map_copy(prog->may_write)));

  arrays = isl_union_set_apply(arrays,
                               isl_union_map_copy(prog->to_outer));

  arrays = isl_union_set_coalesce(arrays);

  for (i = 0; i < prog->scop->pet->n_array; ++i)
  {
    isl_bool field;

    field = isl_set_is_wrapping(prog->scop->pet->arrays[i]->extent);
    if (field < 0)
      break;
    if (field)
      continue;
    if (extract_array_info(prog, &prog->array[prog->n_array++],
                           prog->scop->pet->arrays[i], arrays) < 0)
      r = isl_stat_error;
  }
  if (i < prog->scop->pet->n_array)
    r = isl_stat_error;

  isl_union_set_free(arrays);

  if (prog->scop->options->live_range_reordering)
    collect_order_dependences(prog);

  return r;
}

/* Is "array" a read-only scalar?
 */
int autosa_array_is_read_only_scalar(struct autosa_array_info *array)
{
  return array->read_only_scalar;
}

/* Check if a autosa array is a scalar.  A scalar is a value that is not stored
 * as an array or through a pointer reference, but as a single data element.
 * At the moment, scalars are represented as zero-dimensional arrays.
 * Note that the single data element may be an entire structure.
 */
int autosa_array_is_scalar(struct autosa_array_info *array)
{
  return array->n_index == 0;
}

/* Does "kernel" need to be passed an argument corresponding to array "i"?
 *
 * The argument is only needed if the kernel accesses this device memory.
 */
int autosa_kernel_requires_array_argument(struct autosa_kernel *kernel, int i)
{
  return kernel->array[i].global;
}

/* If group->n_ref == 1, then group->refs was set by
 * populate_array_references to point directly into
 * group->array->refs and should not be freed.
 * If group->n_ref > 1, then group->refs was set by join_groups
 * to point to a newly allocated array.
 */
struct autosa_array_ref_group *autosa_array_ref_group_free(
    struct autosa_array_ref_group *group)
{
  if (!group)
    return NULL;
  autosa_array_tile_free(group->local_tile); // TODO: fix it
  autosa_array_tile_free(group->pe_tile);
  isl_map_free(group->access);
  if (group->n_ref > 1)
    free(group->refs);
  isl_vec_free(group->dir);
  isl_vec_free(group->old_dir);
  isl_multi_aff_free(group->io_trans);
  isl_multi_aff_free(group->io_L1_trans);
  isl_ast_expr_free(group->io_pe_expr);
  isl_ast_expr_free(group->io_L1_pe_expr);
  isl_ast_expr_free(group->io_pe_expr_boundary);
  isl_ast_expr_free(group->io_L1_pe_expr_boundary);
  for (int i = 0; i < group->n_io_buffer; i++)
  {
    autosa_array_tile_free(group->io_buffers[i]->tile);
    free(group->io_buffers[i]);
  }
  free(group->io_buffers);
  isl_schedule_free(group->io_schedule);
  isl_schedule_free(group->io_L1_schedule);
  isl_schedule_free(group->io_L1_lower_schedule);
  isl_union_pw_multi_aff_free(group->copy_schedule);
  if (group->attached_drain_group)
    autosa_array_ref_group_free(group->attached_drain_group);
  free(group);

  return NULL;
}

struct autosa_array_ref_group *autosa_array_ref_group_init(
    struct autosa_array_ref_group *group)
{
  group->local_array = NULL;
  group->array = NULL;
  group->nr = -1;
  group->access = NULL;
  group->write = -1;
  group->exact_write = -1;
  group->slice = -1;
  group->min_depth = -1;
  group->shared_tile = NULL;
  group->private_tile = NULL;
  group->local_tile = NULL;
  group->n_ref = 0;
  group->refs = NULL;
  group->io_buffers = NULL;
  group->n_io_buffer = 0;
  group->io_type = AUTOSA_UNKNOWN_IO;
  group->pe_io_dir = IO_UNKNOWN;
  group->array_io_dir = IO_UNKNOWN;
  group->io_trans = NULL;
  group->io_L1_trans = NULL;
  group->io_pe_expr = NULL;
  group->io_L1_pe_expr = NULL;
  group->io_pe_expr_boundary = NULL;
  group->io_L1_pe_expr_boundary = NULL;
  group->io_schedule = NULL;
  group->io_L1_schedule = NULL;
  group->io_L1_lower_schedule = NULL;
  group->io_level = 0;
  group->space_dim = 0;
  group->n_lane = 0;
  group->copy_schedule_dim = 0;
  group->copy_schedule = NULL;
  group->attached_drain_group = NULL;

  return group;
}

struct autosa_array_tile *autosa_array_tile_free(struct autosa_array_tile *tile)
{
  int j;

  if (!tile)
    return NULL;

  for (j = 0; j < tile->n; ++j)
  {
    isl_val_free(tile->bound[j].size);
    isl_val_free(tile->bound[j].stride);
    isl_aff_free(tile->bound[j].lb);
    isl_aff_free(tile->bound[j].shift);
  }
  free(tile->bound);
  isl_multi_aff_free(tile->tiling);
  free(tile);

  return NULL;
}

/* Create a autosa_array_tile for an array of dimension "n_index".
 */
struct autosa_array_tile *autosa_array_tile_create(isl_ctx *ctx, int n_index)
{
  int i;
  struct autosa_array_tile *tile;

  tile = isl_calloc_type(ctx, struct autosa_array_tile);
  if (!tile)
    return NULL;

  tile->ctx = ctx;
  tile->bound = isl_alloc_array(ctx, struct autosa_array_bound, n_index);
  if (!tile->bound)
    return autosa_array_tile_free(tile);

  tile->n = n_index;

  for (i = 0; i < n_index; ++i)
  {
    tile->bound[i].size = NULL;
    tile->bound[i].lb = NULL;
    tile->bound[i].stride = NULL;
    tile->bound[i].shift = NULL;
  }

  return tile;
}

/* Compute the size of the tile specified by "tile"
 * in number of elements and return the result.
 */
__isl_give isl_val *autosa_array_tile_size(struct autosa_array_tile *tile)
{
  int i;
  isl_val *size;

  if (!tile)
    return NULL;

  size = isl_val_one(tile->ctx);

  for (i = 0; i < tile->n; ++i)
    size = isl_val_mul(size, isl_val_copy(tile->bound[i].size));

  return size;
}

/****************************************************************
 * AutoSA statement
 ****************************************************************/
static void *free_autosa_io_info(struct autosa_io_info *io_info)
{
  autosa_dep_free(io_info->dep);
  isl_vec_free(io_info->dir);
  isl_vec_free(io_info->old_dir);

  free(io_info);
  return NULL;
}

static void *free_stmts(struct autosa_stmt *stmts, int n)
{
  int i;

  if (!stmts)
    return NULL;

  for (i = 0; i < n; ++i)
  {
    struct autosa_stmt_access *access, *next;

    for (access = stmts[i].accesses; access; access = next)
    {
      next = access->next;
      isl_id_free(access->ref_id);
      isl_map_free(access->access);
      isl_map_free(access->tagged_access);

      for (int k = 0; k < access->n_io_info; k++)
        free_autosa_io_info(access->io_info[k]);
      free(access->io_info);

      free(access);
    }

    isl_id_free(stmts[i].id);
  }
  free(stmts);

  return NULL;
}

/* Has statement "stmt" been killed from "scop"?
 * That is, is the instance set of "scop" free from any
 * instances of "stmt"?
 */
static isl_bool is_stmt_killed(struct ppcg_scop *scop, struct pet_stmt *stmt)
{
  isl_space *space;
  isl_set *left;
  isl_bool empty;

  if (!scop || !stmt)
    return isl_bool_error;
  space = isl_set_get_space(stmt->domain);
  left = isl_union_set_extract_set(scop->domain, space);
  empty = isl_set_plain_is_empty(left);
  isl_set_free(left);

  return empty;
}

/* Given a tagged access relation to a single array "tagged", extract it
 * as a map, taking into account that the input may be empty.
 * If the access relation is empty, then it does not contain
 * any space information, so we try to recover it from the index
 * expression.
 * The space of the index expression is of the form I -> A,
 * with I the statement instances and A the array, or [I -> F] -> A,
 * with F the filters corresponding to arguments.
 * We first drop F, if present, obtaining I -> A.
 * Then we construct I -> R, with R the reference tag,
 * combine the two into I -> [R -> A] and uncurry to obtain
 * the final result [I -> R] -> A.
 * Note that the index expression may have a lower dimension
 * than that of the array, but this dimension is not used
 * if the access relation is empty.
 */
static __isl_give isl_map *extract_single_tagged_access(
    __isl_take isl_union_map *tagged, __isl_keep pet_expr *expr)
{
  int empty;
  isl_id *id;
  isl_space *space, *space2;
  isl_multi_pw_aff *index;

  empty = isl_union_map_is_empty(tagged);
  if (empty < 0)
    goto error;
  if (!empty)
    return isl_map_from_union_map(tagged);
  isl_union_map_free(tagged);

  index = pet_expr_access_get_index(expr);
  space = isl_multi_pw_aff_get_space(index);
  isl_multi_pw_aff_free(index);
  if (isl_space_domain_is_wrapping(space))
    space = isl_space_domain_factor_domain(space);
  space2 = isl_space_copy(space);
  space2 = isl_space_from_domain(isl_space_domain(space));
  id = pet_expr_access_get_ref_id(expr);
  space2 = isl_space_set_tuple_id(space2, isl_dim_out, id);
  space = isl_space_range_product(space2, space);
  space = isl_space_uncurry(space);

  return isl_map_empty(space);
error:
  isl_union_map_free(tagged);
  return NULL;
}

/* Does the index expression "index" of "expr" represent an access
 * to a single element?
 * That is, is "index" completely specified?
 *
 * If "expr" accesses elements from different spaces (i.e., fields
 * of a structure), then it does not access a single element.
 * Otherwise, if the single space of the access matches the space
 * of "index", then the index expression is completely specified
 * (no pointer to a lower-dimensional slice of the accessed array)
 * and a single element is being accessed.
 */
static isl_bool complete_index(__isl_keep pet_expr *expr,
                               __isl_keep isl_multi_pw_aff *index)
{
  isl_union_map *read, *write, *all;
  isl_map *map;
  isl_space *space1, *space2;
  isl_bool complete;

  read = pet_expr_access_get_may_read(expr);
  write = pet_expr_access_get_may_write(expr);
  all = isl_union_map_union(read, write);
  if (!all)
    return isl_bool_error;
  if (isl_union_map_n_map(all) != 1)
  {
    isl_union_map_free(all);
    return isl_bool_false;
  }
  map = isl_map_from_union_map(all);
  space1 = isl_map_get_space(map);
  isl_map_free(map);
  space2 = isl_multi_pw_aff_get_space(index);
  complete = isl_space_tuple_is_equal(space1, isl_dim_out,
                                      space2, isl_dim_out);
  isl_space_free(space1);
  isl_space_free(space2);

  return complete;
}

/* Does "expr" access a single, fixed element (independently of the statement
 * instance)?
 * That is, does it have a completely specified constant index expression?
 *
 * Note that it is not sufficient for the index expression to be
 * piecewise constant.  isl_multi_pw_aff_is_cst can therefore not be used.
 */
static isl_bool accesses_fixed_element(__isl_keep pet_expr *expr)
{
  int i, n;
  isl_multi_pw_aff *index;
  isl_bool fixed = isl_bool_true;

  index = pet_expr_access_get_index(expr);
  if (index < 0)
    return isl_bool_error;
  n = isl_multi_pw_aff_dim(index, isl_dim_out);
  for (i = 0; i < n; ++i)
  {
    isl_pw_aff *pa;

    pa = isl_multi_pw_aff_get_pw_aff(index, 0);
    fixed = (isl_pw_aff_n_piece(pa) == 1) ? isl_bool_true : isl_bool_false;
    if (fixed)
      fixed = isl_pw_aff_is_cst(pa);
    isl_pw_aff_free(pa);
    if (fixed < 0 || !fixed)
      break;
  }
  if (fixed >= 0 && fixed)
    fixed = complete_index(expr, index);
  isl_multi_pw_aff_free(index);

  return fixed;
}

/* Extract a autosa_stmt_access from "expr", append it to the list
 * that ends in *data->next_access and update the end of the list.
 * If the access expression performs a write, then it is considered
 * exact only if it appears in a single expression statement and
 * if its may access relation is equal to its must access relation.
 *
 * The combined set of may accesses may be a union if member accesses
 * are involved, but the entire set is derived from a single reference and
 * therefore from a single index expression.  These accesses therefore
 * all map to the same outer array.
 */
static int extract_access(__isl_keep pet_expr *expr, void *user)
{
  struct ppcg_extract_access_data *data = (struct ppcg_extract_access_data *)user;
  isl_union_map *tagged;
  struct autosa_stmt_access *access;
  isl_ctx *ctx = pet_expr_get_ctx(expr);
  isl_multi_pw_aff *index;

  access = isl_alloc_type(ctx, struct autosa_stmt_access);
  if (!access)
    return -1;
  access->next = NULL;
  access->read = pet_expr_access_is_read(expr);
  access->write = pet_expr_access_is_write(expr);
  tagged = pet_expr_access_get_tagged_may_read(expr);
  tagged = isl_union_map_union(tagged,
                               pet_expr_access_get_tagged_may_write(expr));
  tagged = isl_union_map_apply_range(tagged,
                                     isl_union_map_copy(data->any_to_outer));
  if (!access->write)
  {
    access->exact_write = 1;
  }
  else if (!data->single_expression)
  {
    access->exact_write = 0;
  }
  else
  {
    isl_union_map *must, *may;
    may = isl_union_map_copy(tagged);
    may = isl_union_map_domain_factor_domain(may);
    must = pet_expr_access_get_must_write(expr);
    access->exact_write = isl_union_map_is_equal(must, may);
    isl_union_map_free(must);
    isl_union_map_free(may);
  }
  index = pet_expr_access_get_index(expr);
  access->n_index = isl_multi_pw_aff_dim(index, isl_dim_out);
  isl_multi_pw_aff_free(index);
  access->ref_id = pet_expr_access_get_ref_id(expr);
  access->tagged_access = extract_single_tagged_access(tagged, expr);
  access->access = isl_map_copy(access->tagged_access);
  access->access = isl_map_domain_factor_domain(access->access);
  access->fixed_element = accesses_fixed_element(expr);

  /* AutoSA Extended */
  access->n_io_info = 0;
  access->io_info = NULL;
  access->layout_trans = -1;
  access->simd_dim = -1;
  access->simd_stride = -1;
  /* AutoSA Extended */

  *data->next_access = access;
  data->next_access = &(*data->next_access)->next;

  if (!access->access || access->fixed_element < 0)
    return -1;

  return 0;
}

/* Construct a linked list of autosa_stmt_access objects,
 * one for each access expression in the statement body.
 * "any_to_outer" maps all intermediate arrays to their outer arrays.
 */
static int pet_stmt_extract_accesses(struct autosa_stmt *stmt,
                                     __isl_keep isl_union_map *any_to_outer)
{
  struct ppcg_extract_access_data data;

  stmt->accesses = NULL;
  data.next_access = &stmt->accesses;
  data.single_expression =
      pet_tree_get_type(stmt->stmt->body) == pet_tree_expr;
  data.any_to_outer = any_to_outer;
  return pet_tree_foreach_access_expr(stmt->stmt->body,
                                      &extract_access, &data);
}

/* Return an array of autosa_stmt representing the statements in "scop".
 * Do not collect array accesses for statements that have been killed.
 */
struct autosa_stmt *extract_stmts(isl_ctx *ctx, struct ppcg_scop *scop,
                                  __isl_keep isl_union_map *any_to_outer)
{
  int i;
  struct autosa_stmt *stmts;

  stmts = isl_calloc_array(ctx, struct autosa_stmt, scop->pet->n_stmt);
  if (!stmts)
    return NULL;

  for (i = 0; i < scop->pet->n_stmt; ++i)
  {
    struct autosa_stmt *s = &stmts[i];
    isl_bool killed;

    s->id = isl_set_get_tuple_id(scop->pet->stmts[i]->domain);
    s->stmt = scop->pet->stmts[i];
    killed = is_stmt_killed(scop, scop->pet->stmts[i]);
    if (killed < 0)
      return (struct autosa_stmt *)free_stmts(stmts, i + 1);
    if (killed)
      continue;
    if (pet_stmt_extract_accesses(s, any_to_outer) < 0)
      return (struct autosa_stmt *)free_stmts(stmts, i + 1);
  }

  return stmts;
}

void autosa_kernel_stmt_free(void *user)
{
  struct autosa_kernel_stmt *stmt = (struct autosa_kernel_stmt *)user;

  if (!stmt)
    return;

  switch (stmt->type)
  {
  case AUTOSA_KERNEL_STMT_COPY:
    isl_ast_expr_free(stmt->u.c.index);
    isl_ast_expr_free(stmt->u.c.local_index);
    break;
  case AUTOSA_KERNEL_STMT_DOMAIN:
    isl_id_to_ast_expr_free(stmt->u.d.ref2expr);
    break;
  case AUTOSA_KERNEL_STMT_SYNC:
    break;
  case AUTOSA_KERNEL_STMT_IO:
  case AUTOSA_KERNEL_STMT_IO_TRANSFER:
  case AUTOSA_KERNEL_STMT_IO_TRANSFER_BUF:
  case AUTOSA_KERNEL_STMT_IO_DRAM:    
    free(stmt->u.i.in_fifo_name);
    free(stmt->u.i.out_fifo_name);
    isl_ast_expr_free(stmt->u.i.local_index);
    isl_ast_expr_free(stmt->u.i.index);
    free(stmt->u.i.reduce_op);
    break;
  case AUTOSA_KERNEL_STMT_MODULE_CALL:
  case AUTOSA_KERNEL_STMT_EXT_MODULE:
    free(stmt->u.m.module_name);
    break;
  case AUTOSA_KERNEL_STMT_FIFO_DECL:
    break;
  case AUTOSA_KERNEL_STMT_DRAIN_MERGE:
    isl_ast_expr_free(stmt->u.dm.index);
    break;
  case AUTOSA_KERNEL_STMT_HOST_SERIALIZE:
    isl_ast_expr_free(stmt->u.s.index);
    break;
  }

  free(stmt);
}

/* Find the element in gen->stmt that has the given "id".
 * Return NULL if no such autosa_stmt can be found.
 */
struct autosa_stmt *find_stmt(struct autosa_prog *prog, __isl_keep isl_id *id)
{
  int i;

  for (i = 0; i < prog->n_stmts; ++i)
  {
    if (id == prog->stmts[i].id)
      break;
  }

  return i < prog->n_stmts ? &prog->stmts[i] : NULL;
}

/****************************************************************
 * AutoSA prog
 ****************************************************************/
/* Compute the set of inner array elements that may have their values
 * preserved by "prog".  In particular, collect the array elements of
 * arrays that are not local to "prog" and remove those elements that
 * are definitely killed or definitely written by "prog".
 */
static __isl_give isl_union_set *compute_may_persist(struct autosa_prog *prog)
{
  int i;
  isl_union_set *may_persist, *killed;
  isl_union_map *must_kill;

  may_persist = isl_union_set_empty(isl_set_get_space(prog->context));
  for (i = 0; i < prog->n_array; ++i)
  {
    isl_set *extent;

    if (prog->array[i].local)
      continue;

    extent = isl_set_copy(prog->array[i].extent);
    may_persist = isl_union_set_add_set(may_persist, extent);
  }

  may_persist = isl_union_set_intersect_params(may_persist,
                                               isl_set_copy(prog->context));
  may_persist = isl_union_set_apply(may_persist,
                                    isl_union_map_copy(prog->to_inner));
  must_kill = isl_union_map_copy(prog->tagged_must_kill);
  killed = isl_union_map_range(must_kill);
  must_kill = isl_union_map_copy(prog->must_write);
  killed = isl_union_set_union(killed, isl_union_map_range(must_kill));

  may_persist = isl_union_set_subtract(may_persist, killed);
  return may_persist;
}

struct autosa_prog *autosa_prog_alloc(isl_ctx *ctx, struct ppcg_scop *scop)
{
  struct autosa_prog *prog;
  isl_space *space;
  isl_map *id;

  if (!scop)
    return NULL;

  prog = isl_calloc_type(ctx, struct autosa_prog);
  if (!prog)
    return NULL;

  prog->ctx = ctx;
  prog->scop = scop;
  prog->context = isl_set_copy(scop->context);
  prog->n_stmts = scop->pet->n_stmt;
  prog->any_to_outer = pet_scop_compute_outer_to_any(scop->pet);
  prog->any_to_outer = isl_union_map_reverse(prog->any_to_outer);
  space = isl_union_map_get_space(prog->any_to_outer);
  space = isl_space_set_from_params(space);
  space = isl_space_add_dims(space, isl_dim_set, 1);
  space = isl_space_map_from_set(space);
  id = isl_map_identity(space);
  prog->any_to_outer = isl_union_map_add_map(prog->any_to_outer, id);
  prog->stmts = extract_stmts(ctx, scop, prog->any_to_outer);
  prog->read = isl_union_map_copy(scop->reads);
  prog->may_write = isl_union_map_copy(scop->may_writes);
  prog->must_write = isl_union_map_copy(scop->must_writes);
  prog->tagged_must_kill = isl_union_map_copy(scop->tagged_must_kills);
  prog->to_inner = pet_scop_compute_outer_to_inner(scop->pet);
  prog->to_outer = isl_union_map_copy(prog->to_inner);
  prog->to_outer = isl_union_map_reverse(prog->to_outer);

  if (!prog->stmts)
    return (struct autosa_prog *)autosa_prog_free(prog);

  if (collect_array_info(prog) < 0)
    return (struct autosa_prog *)autosa_prog_free(prog);
  prog->may_persist = compute_may_persist(prog); // TODO

  return prog;
}

void *autosa_prog_free(struct autosa_prog *prog)
{
  if (!prog)
    return NULL;
  free_array_info(prog);
  free_stmts(prog->stmts, prog->n_stmts);
  isl_union_map_free(prog->any_to_outer);
  isl_union_map_free(prog->to_outer);
  isl_union_map_free(prog->to_inner);
  isl_union_map_free(prog->read);
  isl_union_map_free(prog->may_write);
  isl_union_map_free(prog->must_write);
  isl_union_map_free(prog->tagged_must_kill);
  isl_union_map_free(prog->array_order);
  isl_union_set_free(prog->may_persist);
  isl_set_free(prog->context);
  free(prog);

  return NULL;
}

/****************************************************************
 * AutoSA hw module
 ****************************************************************/
struct autosa_hw_module *autosa_hw_module_alloc(struct autosa_gen *gen)
{
  struct autosa_hw_module *module = (struct autosa_hw_module *)malloc(
      sizeof(struct autosa_hw_module));
  module->options = gen->options;
  module->name = NULL;
  module->tree = NULL;
  module->device_tree = NULL;
  module->inst_ids = NULL;
  module->n_var = 0;
  module->var = NULL;
  module->kernel = NULL;
  module->n_io_group = 0;
  module->io_groups = NULL;
  module->to_pe = 0;
  module->to_mem = 0;
  module->double_buffer = 0;
  module->is_filter = 0;
  module->is_buffer = 0;
  module->outer_sched = NULL;
  module->inter_sched = NULL;
  module->intra_sched = NULL;
  module->inter_space = NULL;
  module->intra_space = NULL;
  module->space = NULL;
  module->inter_tree = NULL;
  module->intra_tree = NULL;
  module->credit = 0;
  module->boundary_sched = NULL;
  module->boundary_tree = NULL;
  module->boundary = 0;
  module->boundary_outer_sched = NULL;
  module->boundary_inter_sched = NULL;
  module->boundary_outer_tree = NULL;
  module->boundary_inter_tree = NULL;
  module->n_pe_dummy_modules = 0;
  module->pe_dummy_modules = NULL;
  module->n_array_ref = 0;
  module->serialize_sched = NULL;
  module->serialize_tree = NULL;
  module->coalesce_bound = -1;
  module->is_serialized = 0;
  module->use_FF = 0;

  return module;
}

void *autosa_hw_module_free(struct autosa_hw_module *module)
{
  if (!module)
    return NULL;

  free(module->name);

  isl_ast_node_free(module->tree);
  isl_ast_node_free(module->device_tree);
  isl_ast_node_free(module->inter_tree);
  isl_ast_node_free(module->intra_tree);
  isl_ast_node_free(module->boundary_tree);
  isl_ast_node_free(module->boundary_outer_tree);
  isl_ast_node_free(module->boundary_inter_tree);
  isl_ast_node_free(module->serialize_tree);

  isl_space_free(module->inter_space);
  isl_space_free(module->intra_space);
  isl_space_free(module->space);

  isl_id_list_free(module->inst_ids);
  for (int i = 0; i < module->n_var; i++)
  {
    free(module->var[i].name);
    isl_vec_free(module->var[i].size);
  }
  free(module->var);
  free(module->io_groups);
  for (int i = 0; i < module->n_pe_dummy_modules; i++)
  {
    autosa_pe_dummy_module_free(module->pe_dummy_modules[i]);
  }
  free(module->pe_dummy_modules);

  free(module);

  return NULL;
}

struct autosa_hw_top_module *autosa_hw_top_module_alloc()
{
  struct autosa_hw_top_module *module = (struct autosa_hw_top_module *)malloc(
      sizeof(struct autosa_hw_top_module));

  module->n_module_calls = 0;
  module->n_fifo_decls = 0;
  module->module_call_scheds = NULL;
  module->fifo_decl_scheds = NULL;
  module->module_call_trees = NULL;
  module->fifo_decl_trees = NULL;
  module->fifo_decl_names = NULL;

  module->n_module_call_wrapped = 0;
  module->n_fifo_decl_wrapped = 0;
  module->module_call_wrapped_trees = NULL;
  module->fifo_decl_wrapped_trees = NULL;

  module->kernel = NULL;
  module->hw_modules = NULL;
  module->n_hw_modules = 0;

  module->n_ext_module = 0;
  module->ext_module_scheds = NULL;
  module->ext_module_trees = NULL;
  module->n_ext_module_wrapped = 0;
  module->ext_module_wrapped_trees = NULL;

  return module;
}

void *autosa_hw_top_module_free(struct autosa_hw_top_module *module)
{
  if (!module)
    return NULL;

  if (module->module_call_trees)
  {
    for (int i = 0; i < module->n_module_calls; i++)
    {
      isl_ast_node_free(module->module_call_trees[i]);
    }
  }

  if (module->fifo_decl_trees)
  {
    for (int i = 0; i < module->n_fifo_decls; i++)
    {
      isl_ast_node_free(module->fifo_decl_trees[i]);
      free(module->fifo_decl_names[i]);
    }
  }

  if (module->module_call_wrapped_trees)
  {
    for (int i = 0; i < module->n_module_call_wrapped; i++)
    {
      isl_ast_node_free(module->module_call_wrapped_trees[i]);
    }
  }

  if (module->fifo_decl_wrapped_trees)
  {
    for (int i = 0; i < module->n_fifo_decl_wrapped; i++)
    {
      isl_ast_node_free(module->fifo_decl_wrapped_trees[i]);
    }
  }

  if (module->ext_module_trees)
  {
    for (int i = 0; i < module->n_ext_module; i++)
    {
      isl_ast_node_free(module->ext_module_trees[i]);
    }
  }

  if (module->ext_module_wrapped_trees)
  {
    for (int i = 0; i < module->n_ext_module_wrapped; i++)
    {
      isl_ast_node_free(module->ext_module_wrapped_trees[i]);
    }
  }

  free(module->module_call_scheds);
  free(module->fifo_decl_scheds);
  free(module->ext_module_scheds);
  free(module->module_call_trees);
  free(module->fifo_decl_trees);
  free(module->ext_module_trees);
  free(module->module_call_wrapped_trees);
  free(module->fifo_decl_wrapped_trees);
  free(module->ext_module_wrapped_trees);
  free(module->fifo_decl_names);
  free(module);

  return NULL;
}

struct autosa_pe_dummy_module *autosa_pe_dummy_module_alloc()
{
  struct autosa_pe_dummy_module *module = (struct autosa_pe_dummy_module *)malloc(
      sizeof(struct autosa_pe_dummy_module));
  module->module = NULL;
  module->io_group = NULL;
  module->sched = NULL;
  module->tree = NULL;
  module->device_tree = NULL;

  return module;
}

void *autosa_pe_dummy_module_free(struct autosa_pe_dummy_module *module)
{
  if (!module)
    return NULL;

  isl_ast_node_free(module->tree);
  isl_ast_node_free(module->device_tree);
  free(module);

  return NULL;
}

struct autosa_drain_merge_func *autosa_drain_merge_func_alloc(struct autosa_gen *gen)
{
  struct autosa_drain_merge_func *func = (struct autosa_drain_merge_func *)
      malloc(sizeof(struct autosa_drain_merge_func));
  func->group = NULL;
  func->kernel = NULL;
  func->inst_ids = NULL;
  func->sched = NULL;
  func->tree = NULL;
  func->device_tree = NULL;

  return func;
}

void *autosa_drain_merge_func_free(struct autosa_drain_merge_func *func)
{
  if (!func)
    return NULL;

  isl_id_list_free(func->inst_ids);
  isl_ast_node_free(func->tree);
  isl_ast_node_free(func->device_tree);
  free(func);

  return NULL;
}

/****************************************************************
 * AutoSA AST node
 ****************************************************************/
struct autosa_ast_node_userinfo *alloc_ast_node_userinfo()
{
  struct autosa_ast_node_userinfo *info =
      (struct autosa_ast_node_userinfo *)malloc(sizeof(
          struct autosa_ast_node_userinfo));
  info->is_pipeline = 0;
  info->is_unroll = 0;
  info->is_outermost_for = 0;
  info->is_infinitize_legal = 0;
  info->is_first_infinitizable_loop = 0;  
  info->visited = 0;

  return info;
}

void free_ast_node_userinfo(void *ptr)
{
  struct autosa_ast_node_userinfo *info = (struct autosa_ast_node_userinfo *)ptr;
  free(info);
}

/****************************************************************
 * AutoSA PE opt
 ****************************************************************/
/* Internal data structure for extract_size_of_type.
 * "type" specifies the name of the space that we want to extract.
 * "res" is used to store the subset of that space.
 */
struct autosa_extract_size_data
{
  const char *type;
  isl_set *res;
};

/* This function is called for each set in a union_set.
 * If the name of the set matches data->type, we store the
 * set in data->res.
 */
static isl_stat extract_size_of_type(__isl_take isl_set *size, void *user)
{
  struct autosa_extract_size_data *data = (struct autosa_extract_size_data *)user;
  const char *name;

  name = isl_set_get_tuple_name(size);
  if (name && !strcmp(name, data->type))
  {
    data->res = size;
    return isl_stat_error;
  }

  isl_set_free(size);
  return isl_stat_ok;
}

/* Given a union map { kernel[] -> *[...] },
 * return the range in the space called "type" for the kernel with 
 * sequence number "id".
 */
__isl_give isl_set *extract_sa_sizes(__isl_keep isl_union_map *sizes,
                                     const char *type)
{
  isl_space *space;
  isl_set *dom;
  isl_union_set *local_sizes;
  struct autosa_extract_size_data data = {type, NULL};

  if (!sizes)
    return NULL;

  space = isl_union_map_get_space(sizes);
  space = isl_space_set_from_params(space);
  //space = isl_space_add_dims(space, isl_dim_set, 1);
  space = isl_space_set_tuple_name(space, isl_dim_set, "kernel");
  dom = isl_set_universe(space);
  //dom = isl_set_fix_si(dom, isl_dim_set, 0, id);

  local_sizes = isl_union_set_apply(isl_union_set_from_set(dom),
                                    isl_union_map_copy(sizes));
  isl_union_set_foreach_set(local_sizes, &extract_size_of_type, &data);
  isl_union_set_free(local_sizes);
  return data.res;
}

/* Given a singleton set, extract the *len elements of the single integer tuple
 * into *sizes. 
 *
 * If the element value is "-1", the loop at the same position is not tiled.
 *  
 * If "set" is NULL, then the "sizes" array is not updated.
 */
static isl_stat read_sa_sizes_from_set(__isl_take isl_set *set, int *sizes, int len)
{
  int i;
  int dim;

  if (!set)
    return isl_stat_ok;

  dim = isl_set_dim(set, isl_dim_set);
  if (dim < len)
    isl_die(isl_set_get_ctx(set), isl_error_invalid,
            "fewer sa_sizes than required", return isl_stat_error);

  for (i = 0; i < len; ++i)
  {
    isl_val *v;

    v = isl_set_plain_get_val_if_fixed(set, isl_dim_set, i);
    if (!v)
      goto error;    
    sizes[i] = isl_val_get_num_si(v);    
    isl_val_free(v);
  }

  isl_set_free(set);
  return isl_stat_ok;
error:
  isl_set_free(set);
  return isl_stat_error;
}

/* Given a union map { kernel[] -> *[...] },
 * return the range in the space called "type" for the kernel.
 */
static __isl_give isl_set *extract_config_sizes(__isl_keep isl_union_map *sizes,
  const char *type)
{
  isl_space *space;
  isl_set *dom;
  isl_union_set *local_sizes;
  struct autosa_extract_size_data data = {type, NULL};

  if (!sizes)
    return NULL;
  
  space = isl_union_map_get_space(sizes);
  space = isl_space_set_from_params(space);
  //space = isl_space_add_dims(space, isl_dim_set, 1);
  space = isl_space_set_tuple_name(space, isl_dim_set, "kernel");
  dom = isl_set_universe(space);
//#ifdef _DEBUG
//  isl_printer *pd = isl_printer_to_file(isl_set_get_ctx(dom), stdout);
//  pd = isl_printer_print_set(pd, dom);
//  pd = isl_printer_end_line(pd);
//#endif

  local_sizes = isl_union_set_apply(isl_union_set_from_set(dom),
                                    isl_union_map_copy(sizes));

//#ifdef _DEBUG
//  pd = isl_printer_print_union_set(pd, local_sizes);
//  pd = isl_printer_end_line(pd);
//#endif
  isl_union_set_foreach_set(local_sizes, &extract_size_of_type, &data);                                      
  isl_union_set_free(local_sizes);
  return data.res;
}

/* Given a singleton set, extract the *len elements of the single integer tuple
 * into *sizes. 
 *
 * If the element value is "-1", the loop at the same position is not tiled.
 *  
 * If "set" is NULL, then the "sizes" array is not updated.
 */
static isl_stat read_config_sizes_from_set(__isl_take isl_set *set, 
  int *sizes, int len)
{
  int i;
  int dim;

  if (!set)
    return isl_stat_ok;

  dim = isl_set_dim(set, isl_dim_set);
  if (dim < len)
    isl_die(isl_set_get_ctx(set), isl_error_invalid,
            "fewer sizes than required", return isl_stat_error);

  for (i = 0; i < len; ++i)
  {
    isl_val *v;

    v = isl_set_plain_get_val_if_fixed(set, isl_dim_set, i);
    if (!v)
      goto error;
    sizes[i] = isl_val_get_num_si(v);
    isl_val_free(v);
  }

  isl_set_free(set);
  return isl_stat_ok;
error:
  isl_set_free(set);
  return isl_stat_error;
}

/* Add the map { kernel[id] -> type[sizes] } to gen->used-sizes 
 * if the option debug->dump_sa_sizes is set.
 */
static void set_sa_used_sizes(struct autosa_kernel *sa, const char *type, int id,
                              int *sizes, int len)
{
  // TODO: fix it
}

/* Extract user specified "sa_tile" sizes from the "sa_sizes" command line options,
 * defaulting to option->sa_tile_size in each dimension.
 * *tile_len contains the maximum number of tile sizes needed.
 * Update *tile_len to the number of specified tile sizes, if any, and
 * return a pointer to the tile sizes (or NULL on error).
 * And the effectively used sizes to sa->used_sizes.
 */
int *read_hbm_tile_sizes(struct autosa_kernel *sa, int tile_len, char *name)
{
  int n;
  int *tile_size;
  isl_set *size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;

  size = extract_sa_sizes(sa->sizes, name);
  if (isl_set_dim(size, isl_dim_set) < tile_len)
  {
    free(tile_size);
    isl_set_free(size);
    return NULL;
  }
  if (read_sa_sizes_from_set(size, tile_size, tile_len) < 0)
    goto error;
  set_sa_used_sizes(sa, name, sa->id, tile_size, tile_len);

  return tile_size;
error:
  free(tile_size);
  return NULL;
}

int *read_default_hbm_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;
  for (n = 0; n < tile_len; ++n)
    tile_size[n] = sa->scop->options->autosa->n_hbm_port;

  return tile_size;
}

/* Extract user specified data pack sizes from the "data_pack_sizes" command line
 * option, defaulting to 8, 32, 64, correponding to the upper bounds of data 
 * pack factors at the innermost, in-between, and outermost I/O module levels.
 * Return a pointer to the tile sizes (or NULL on error).
 */
int *read_data_pack_sizes(__isl_keep isl_union_map *sizes, int tile_len)
{
  int n;
  int *tile_size;
  isl_set *size;
  isl_ctx *ctx;

  ctx = isl_union_map_get_ctx(sizes);
  tile_size = isl_alloc_array(ctx, int, tile_len);
  if (!tile_size)
    return NULL;

  size = extract_config_sizes(sizes, "data_pack");
//#ifdef _DEBUG
//  isl_printer *pd = isl_printer_to_file(ctx, stdout);
//  pd = isl_printer_print_union_map(pd, sizes);
//  pd = isl_printer_end_line(pd);
//  if (!size)
//    printf("null\n");
//  pd = isl_printer_print_set(pd, size);
//  pd = isl_printer_end_line(pd);
//  isl_printer_free(pd);
//#endif
  
  if (isl_set_dim(size, isl_dim_set) < tile_len) 
  {
    free(tile_size);
    isl_set_free(size);
    return NULL;
  }
  if (read_config_sizes_from_set(size, tile_size, tile_len) < 0)
    goto error;

  return tile_size;
error:
  free(tile_size);
  return NULL;
}

/* Extract user specified "sa_tile" sizes from the "sa_sizes" command line option,
 * defaulting to option->sa_tile_size in each dimension.
 * *tile_len contains the maximum number of tile sizes needed.
 * Update *tile_len to the number of specified tile sizes, if any, and 
 * return a pointer to the tile sizes (or NULL on error).
 * And the effectively used sizes to sa->used_sizes.
 */
int *read_array_part_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;
  isl_set *size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;

  size = extract_sa_sizes(sa->sizes, "array_part");
  if (isl_set_dim(size, isl_dim_set) < tile_len)
  {
    free(tile_size);
    isl_set_free(size);
    return NULL;
  }
  if (read_sa_sizes_from_set(size, tile_size, tile_len) < 0)
    goto error;
  set_sa_used_sizes(sa, "array_part", sa->id, tile_size, tile_len);

  return tile_size;
error:
  free(tile_size);
  return NULL;
}

int *read_default_array_part_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;
  for (n = 0; n < tile_len; ++n)
    tile_size[n] = sa->scop->options->autosa->sa_tile_size;

  return tile_size;
}

/* Extract user specified "sa_tile" sizes from the "sa_sizes" command line option,
 * defaulting to option->sa_tile_size in each dimension.
 * *tile_len contains the maximum number of tile sizes needed.
 * Update *tile_len to the number of specified tile sizes, if any, and
 * return a pointer to the tile sizes (or NULL on error).
 * And store the effectively used sizes to sa->used_sizes.
 */
int *read_latency_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;
  isl_set *size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;

  size = extract_sa_sizes(sa->sizes, "latency");
  if (isl_set_dim(size, isl_dim_set) < tile_len)
  {
    free(tile_size);
    isl_set_free(size);
    return NULL;
  }
  if (read_sa_sizes_from_set(size, tile_size, tile_len) < 0)
    goto error;
  set_sa_used_sizes(sa, "latency", sa->id, tile_size, tile_len);

  return tile_size;
error:
  free(tile_size);
  return NULL;
}

int *read_default_latency_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;
  for (n = 0; n < tile_len; ++n)
    tile_size[n] = sa->scop->options->autosa->sa_tile_size / 2;

  return tile_size;
}

int *read_simd_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;
  isl_set *size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;

  size = extract_sa_sizes(sa->sizes, "simd");
  if (isl_set_dim(size, isl_dim_set) < tile_len)
  {
    free(tile_size);
    isl_set_free(size);
    return NULL;
  }
  if (read_sa_sizes_from_set(size, tile_size, tile_len) < 0)
    goto error;
  set_sa_used_sizes(sa, "simd", sa->id, tile_size, tile_len);

  return tile_size;
error:
  free(tile_size);
  return NULL;
}

int *read_default_simd_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;
  for (n = 0; n < tile_len; ++n)
    tile_size[n] = sa->scop->options->autosa->sa_tile_size / 2;

  return tile_size;
}

int read_space_time_kernel_id(__isl_keep isl_union_map *sizes)
{
  isl_set *size;
  int kernel_id;
  int dim;
  size = extract_sa_sizes(sizes, "space_time");
  if (!size)
    return -1;
  dim = isl_set_dim(size, isl_dim_set);
  if (dim == 0)
    return -1;
  else
  {
    read_sa_sizes_from_set(size, &kernel_id, 1);
    return kernel_id;
  }
}

int *read_array_part_L2_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;
  isl_set *size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;

  size = extract_sa_sizes(sa->sizes, "array_part_L2");
  if (isl_set_dim(size, isl_dim_set) < tile_len)
  {
    free(tile_size);
    isl_set_free(size);
    return NULL;
  }
  if (read_sa_sizes_from_set(size, tile_size, tile_len) < 0)
    goto error;
  set_sa_used_sizes(sa, "array_part_L2", sa->id, tile_size, tile_len);

  return tile_size;
error:
  free(tile_size);
  return NULL;
}

int *read_default_array_part_L2_tile_sizes(struct autosa_kernel *sa, int tile_len)
{
  int n;
  int *tile_size;

  tile_size = isl_alloc_array(sa->ctx, int, tile_len);
  if (!tile_size)
    return NULL;
  for (n = 0; n < tile_len; ++n)
    tile_size[n] = sa->scop->options->autosa->sa_tile_size;

  return tile_size;
}

/****************************************************************
 * AutoSA latency and resource estimation
 ****************************************************************/
struct extract_loop_info_data
{
  cJSON *loop_struct;
};

/* Extract the loop info containing: iterator, lower bound,
 * upper bound, and stride.
 * Return the pointer to the loop child.
 */
static cJSON *extract_isl_ast_node_for(__isl_keep isl_ast_node *node, cJSON *loop,
                                       isl_bool degenerate)
{
  cJSON *loop_info = cJSON_CreateObject();
  cJSON *loop_child = cJSON_CreateObject();
  isl_printer *p_str = NULL;
  isl_ctx *ctx = isl_ast_node_get_ctx(node);
  char *str = NULL;

  /* Extract the loop info */
  isl_ast_expr *init, *cond, *inc, *iterator, *arg;
  init = isl_ast_node_for_get_init(node);
  cond = isl_ast_node_for_get_cond(node);
  inc = isl_ast_node_for_get_inc(node);
  iterator = isl_ast_node_for_get_iterator(node);

  /* iterator */
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
  p_str = isl_printer_print_ast_expr(p_str, iterator);
  str = isl_printer_get_str(p_str);
  cJSON_AddStringToObject(loop_info, "iter", str);
  isl_printer_free(p_str);
  free(str);
  isl_ast_expr_free(iterator);

  /* lower bound */
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
  p_str = isl_printer_print_ast_expr(p_str, init);
  str = isl_printer_get_str(p_str);
  cJSON_AddStringToObject(loop_info, "lb", str);
  isl_printer_free(p_str);
  free(str);
  isl_ast_expr_free(init);

  if (!degenerate)
  {
    /* upper bound */
    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
    arg = isl_ast_expr_op_get_arg(cond, 1);
    p_str = isl_printer_print_ast_expr(p_str, arg);
    str = isl_printer_get_str(p_str);
    cJSON_AddStringToObject(loop_info, "ub", str);
    isl_printer_free(p_str);
    free(str);
    isl_ast_expr_free(arg);

    /* stride */
    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
    p_str = isl_printer_print_ast_expr(p_str, inc);
    str = isl_printer_get_str(p_str);
    cJSON_AddStringToObject(loop_info, "stride", str);
    isl_printer_free(p_str);
    free(str);
  }
  else
  {
    const cJSON *lb;

    lb = cJSON_GetObjectItemCaseSensitive(loop_info, "lb");
    cJSON_AddStringToObject(loop_info, "ub", lb->valuestring);
    cJSON_AddStringToObject(loop_info, "stride", "1");
  }
  isl_ast_expr_free(cond);
  isl_ast_expr_free(inc);

  cJSON_AddItemToObject(loop, "loop_info", loop_info);
  cJSON_AddItemToObject(loop, "child", loop_child);

  return loop_child;
}

static cJSON *extract_isl_ast_node_block(__isl_keep isl_ast_node *node, cJSON *block)
{
  cJSON *block_child = cJSON_CreateArray();
  cJSON_AddItemToObject(block, "child", block_child);

  return block_child;
}

static cJSON *extract_isl_ast_node_mark(__isl_keep isl_ast_node *node, cJSON *mark)
{
  cJSON *mark_child = cJSON_CreateObject();
  isl_id *id = isl_ast_node_mark_get_id(node);
  char *name = (char *)isl_id_get_name(id);
  isl_id_free(id);
  cJSON_AddStringToObject(mark, "mark_name", name);
  cJSON_AddItemToObject(mark, "child", mark_child);

  return mark_child;
}

static cJSON *extract_isl_ast_node_user(__isl_keep isl_ast_node *node, cJSON *user)
{
  isl_ctx *ctx = isl_ast_node_get_ctx(node);
  isl_ast_expr *expr = isl_ast_node_user_get_expr(node);
  isl_printer *p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
  p_str = isl_printer_print_ast_expr(p_str, expr);
  char *user_expr = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  cJSON_AddStringToObject(user, "user_expr", user_expr);
  free(user_expr);
  isl_ast_expr_free(expr);

  return user;
}

static cJSON *extract_loop_info_at_ast_node(__isl_keep isl_ast_node *node,
                                            cJSON *loop_struct)
{
  enum isl_ast_node_type type;
  isl_ctx *ctx = isl_ast_node_get_ctx(node);
  type = isl_ast_node_get_type(node);

  switch (type)
  {
  case isl_ast_node_for:
  {
    isl_bool degenerate = isl_ast_node_for_is_degenerate(node);
    /* Extract the loop information and insert it into the loop struct */
    cJSON *loop = cJSON_CreateObject();
    cJSON *loop_child = extract_isl_ast_node_for(node, loop, degenerate);
    if (cJSON_IsObject(loop_struct))
    {
      cJSON_AddItemToObject(loop_struct, "loop", loop);
    }
    else if (cJSON_IsArray(loop_struct))
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddItemToObject(item, "loop", loop);
      cJSON_AddItemToArray(loop_struct, item);
    }
    isl_ast_node *child_node;
    /* Update the JSON pointer */
    child_node = isl_ast_node_for_get_body(node);
    extract_loop_info_at_ast_node(child_node, loop_child);
    isl_ast_node_free(child_node);

    break;
  }
  case isl_ast_node_block:
  {
    /* Extract the block information and insert it into the loop struct */
    isl_ast_node_list *child_list = isl_ast_node_block_get_children(node);
    int n_child = isl_ast_node_list_n_ast_node(child_list);
    cJSON *block = cJSON_CreateObject();
    cJSON *block_child = extract_isl_ast_node_block(node, block);
    if (cJSON_IsObject(loop_struct))
    {
      cJSON_AddItemToObject(loop_struct, "block", block);
    }
    else if (cJSON_IsArray(loop_struct))
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddItemToObject(item, "block", block);
      cJSON_AddItemToArray(loop_struct, item);
    }

    isl_ast_node *child_node;
    for (int i = 0; i < n_child; i++)
    {
      cJSON *child_struct;
      child_node = isl_ast_node_list_get_ast_node(child_list, i);
      extract_loop_info_at_ast_node(child_node, block_child);
      isl_ast_node_free(child_node);
    }
    isl_ast_node_list_free(child_list);

    break;
  }
  case isl_ast_node_user:
  {
    /* Extract the user information and insert it into the loop struct */
    cJSON *user = cJSON_CreateObject();
    user = extract_isl_ast_node_user(node, user);

    if (cJSON_IsObject(loop_struct))
    {
      cJSON_AddItemToObject(loop_struct, "user", user);
    }
    else if (cJSON_IsArray(loop_struct))
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddItemToObject(item, "user", user);
      cJSON_AddItemToArray(loop_struct, item);
    }

    break;
  }
  case isl_ast_node_if:
  {
    cJSON *if_struct = cJSON_CreateObject();
    cJSON *then_struct = cJSON_CreateObject();
    cJSON *else_struct = NULL;
    if (cJSON_IsObject(loop_struct))
    {
      cJSON_AddItemToObject(loop_struct, "if", if_struct);
    }
    else if (cJSON_IsArray(loop_struct))
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddItemToObject(item, "if", if_struct);
      cJSON_AddItemToArray(loop_struct, item);
    }

    isl_ast_node *child_node;
    child_node = isl_ast_node_if_get_then_node(node);
    cJSON_AddItemToObject(if_struct, "then", then_struct);
    extract_loop_info_at_ast_node(child_node, then_struct);
    isl_ast_node_free(child_node);

    child_node = isl_ast_node_if_get_else_node(node);
    if (child_node)
    {
      else_struct = cJSON_CreateObject();
      cJSON_AddItemToObject(if_struct, "else", else_struct);
      extract_loop_info_at_ast_node(child_node, else_struct);
      isl_ast_node_free(child_node);
    }

    break;
  }
  case isl_ast_node_mark:
  {
    /* Extract the mark id and insert it into the loop struct */
    cJSON *mark = cJSON_CreateObject();
    cJSON *mark_child = extract_isl_ast_node_mark(node, mark);
    if (cJSON_IsObject(loop_struct))
    {
      cJSON_AddItemToObject(loop_struct, "mark", mark);
    }
    else if (cJSON_IsArray(loop_struct))
    {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddItemToObject(item, "mark", mark);
      cJSON_AddItemToArray(loop_struct, item);
    }

    isl_ast_node *child_node;
    child_node = isl_ast_node_mark_get_node(node);
    extract_loop_info_at_ast_node(child_node, mark_child);
    isl_ast_node_free(child_node);

    break;
  }
  default:
    break;
  }

  return NULL;
}

/* Extract the loop structure and detailed information of the hardware module into 
 * a JSON struct. If "print" is set, we will print out the JSON file. 
 * Otherwise, return it as a string.
 */
static char *extract_loop_info_from_module(
    struct autosa_gen *gen, __isl_keep isl_ast_node *tree,
    char *module_name, int double_buffer, int in,
    int print)
{
  if (!tree)
    return NULL;

  cJSON *loop_struct = cJSON_CreateObject();
  cJSON *module_props = cJSON_CreateObject();
  char *json_str = NULL;

  cJSON_AddStringToObject(loop_struct, "module_name", module_name);
  cJSON_AddNumberToObject(module_props, "double_buffer", double_buffer);
  cJSON_AddNumberToObject(module_props, "in", in);
  cJSON_AddItemToObject(loop_struct, "module_prop", module_props);
  
  extract_loop_info_at_ast_node(tree, loop_struct);

  /* Print the JSON file */
  json_str = cJSON_Print(loop_struct);

  if (!print)
  {
    cJSON_Delete(loop_struct);
    return json_str;
  }
  else
  {
    char *file_name;
    FILE *fp;
    isl_printer *p_str;
    const cJSON *module_name = NULL;

    module_name = cJSON_GetObjectItemCaseSensitive(loop_struct, "module_name");
    p_str = isl_printer_to_str(gen->ctx);
    p_str = isl_printer_print_str(p_str, gen->options->autosa->output_dir);
    p_str = isl_printer_print_str(p_str, "/latency_est/");
    p_str = isl_printer_print_str(p_str, module_name->valuestring);
    p_str = isl_printer_print_str(p_str, "_loop_info.json");
    file_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    cJSON_Delete(loop_struct);

    fp = fopen(file_name, "w");
    if (!fp)
    {
      printf("[AutoSA] Error: Cannot open file: %s\n", file_name);
      exit(1);
    }
    free(file_name);
    fprintf(fp, "%s", json_str);
    fclose(fp);
    free(json_str);

    return NULL;
  }
}

/* Extract the loop structure and detailed information of the hardware module into 
 * a JSON struct.
 */
isl_stat sa_extract_loop_info(struct autosa_gen *gen, struct autosa_hw_module *module)
{
  char *module_name = NULL;
  char *json_str = NULL;
  isl_ctx *ctx = gen->ctx;

  if (module->is_filter && module->is_buffer)
  {
    /* Parse the loop structure of the intra trans module */
    module_name = concat(ctx, module->name, "intra_trans");
    json_str = extract_loop_info_from_module(gen, module->intra_tree, module_name, module->double_buffer, module->in, 1);
    free(module_name);

    /* Parse the loop structure of the inter trans module */
    module_name = concat(ctx, module->name, "inter_trans");
    json_str = extract_loop_info_from_module(gen, module->inter_tree, module_name, module->double_buffer, module->in, 1);
    free(module_name);

    if (module->boundary)
    {
      module_name = concat(ctx, module->name, "inter_trans_boundary");
      json_str = extract_loop_info_from_module(gen, module->boundary_inter_tree, module_name, module->double_buffer, module->in, 1);
      free(module_name);
    }
  }

  /* Parse the loop structure of the default module */
//#ifdef _DEBUG
//  if (!module->device_tree) {
//    printf("non tree module_name: %s\n", module->name);
//    exit(0);
//  }
//#endif
  json_str = extract_loop_info_from_module(gen, module->device_tree, module->name, module->double_buffer, module->in, 1);

  /* Parse the loop structure of the boundary module */
  if (module->boundary)
  {
    module_name = concat(ctx, module->name, "boundary");
    json_str = extract_loop_info_from_module(gen, module->boundary_tree, module_name, module->double_buffer, module->in, 1);
    free(module_name);
  }

  /* Parse the loop structure of the dummy module */
  if (module->n_pe_dummy_modules > 0)
  {
    for (int i = 0; i < module->n_pe_dummy_modules; i++)
    {
      struct autosa_pe_dummy_module *dummy_module = module->pe_dummy_modules[i];
      struct autosa_array_ref_group *group = dummy_module->io_group;

      /* Generate module name */
      isl_printer *p_str = isl_printer_to_str(gen->ctx);
      p_str = autosa_array_ref_group_print_prefix(group, p_str);
      p_str = isl_printer_print_str(p_str, "_PE_dummy");
      module_name = isl_printer_get_str(p_str);
      isl_printer_free(p_str);
      json_str = extract_loop_info_from_module(gen, dummy_module->device_tree, module_name, 0, 0, 1);
      free(module_name);
    }
  }

  return isl_stat_ok;
}

/* Extract the array type information that will be used for latency estimation.
 */
isl_stat sa_extract_array_info(struct autosa_kernel *kernel)
{
  cJSON *array_info = cJSON_CreateObject();
  char *json_str = NULL;
  FILE *fp;
  isl_printer *p_str;
  char *file_path;

  for (int i = 0; i < kernel->n_array; i++)
  {
    cJSON *array = cJSON_CreateObject();
    struct autosa_local_array_info *local_array = &kernel->array[i];
    char *array_name = local_array->array->name; /* Name of the array */
    char *array_type = local_array->array->type; /* Element type */

    cJSON *n_lane = cJSON_CreateNumber(local_array->n_lane);          /* Data pack factor of the array */
    cJSON *array_size = cJSON_CreateNumber(local_array->array->size); /* Element size */

    cJSON_AddItemToObject(array, "n_lane", n_lane);
    cJSON_AddStringToObject(array, "ele_type", array_type);
    cJSON_AddItemToObject(array, "ele_size", array_size);
    cJSON_AddItemToObject(array_info, array_name, array);
  }

  /* Print out the JSON */
  json_str = cJSON_Print(array_info);
  p_str = isl_printer_to_str(kernel->ctx);
  p_str = isl_printer_print_str(p_str, kernel->options->autosa->output_dir);
  p_str = isl_printer_print_str(p_str, "/latency_est/array_info.json");
  file_path = isl_printer_get_str(p_str);
  fp = fopen(file_path, "w");
  if (!fp)
  {
    printf("[AutoSA] Error: Cannot open file: %s\n", file_path);
    exit(1);
  }
  isl_printer_free(p_str);
  free(file_path);
  fprintf(fp, "%s", json_str);
  fclose(fp);
  free(json_str);
  cJSON_Delete(array_info);

  return isl_stat_ok;
}

/* Extract the memory type of the local array.
 * Heuristics: 
 * Compute the buffer utilization (18Kb BRAM):
 * - If the buffer port width < 18bits, util = #ele / 1024
 * - Otherwise, util = #ele / 512
 * 
 * If the local buffer is inside PE module or I/O/drain module at IO_L1:
 * - If the buffer uses primitive type (n_lane == 1) and #ele <= 32, use FF
 * - Otherwise, use BRAM
 * Otherwise:
 * - If the module is connected to DRAM, use URAM if URAM is allowed, otherwise
 *   use BRAM.
 * - Otherwise, if memory util > 0.2 use BRAM, else use LUTRAM.
 */
int extract_memory_type(struct autosa_hw_module *module,
                        struct autosa_kernel_var *var, int uram)
{
  /* 0: FF 1: LUTRAM 2: BRAM 3: URAM */
  int use_memory = 0;
  int var_size = 1;
  float bram_util;

  for (int i = 0; i < isl_vec_size(var->size); ++i)
  {
    isl_val *v = isl_vec_get_element_val(var->size, i);    
    long v_i = isl_val_get_num_si(v);
    var_size *= v_i;
    isl_val_free(v);
  }
  if (var->array->size * var->n_lane < 3)
    bram_util = (float)var_size / 1024;
  else
    bram_util = (float)var_size / 512;

  //if (module->type == PE_MODULE) {
  //  if (var->n_lane == 1 && var_size <= 32)
  //    use_memory = 0;
  //  else
  //    use_memory = 2;    
  //} else if (module->type != PE_MODULE && module->level == 1) {
  //  if (var->n_lane == 1 && var_size <= 32)
  //    use_memory = 0;
  //  else {
  //    //use_memory = 2;
  //    if (bram_util > 0.2)
  //      use_memory = 2;
  //    else
  //      use_memory = 0;      
  //  }      
  //} else {
  //  if (module->to_mem == 1) {
  //    if (uram)
  //      use_memory = 3;
  //    else
  //      use_memory = 2;
  //  } else {
  //    if (bram_util > 0.2)
  //      use_memory = 2;
  //    else
  //      use_memory = 0;
  //      //use_memory = 1;        
  //  }
  //}
  
  if (module->type != PE_MODULE && module->to_mem == 1) {
    if (uram)
      use_memory = 3;
    else
      use_memory = 2;
  } else {    
    if (module->type == IO_MODULE && module->level == 1) {          
      use_memory = 1;      
    } else {
      if (var->n_lane == 1 && var_size <= 32)
        use_memory = 0;
      else
        use_memory = 2;
    }    
  }  

  if (use_memory == 0) 
    module->use_FF = 1;

  return use_memory;
}

static cJSON *extract_buffer_info_from_module(struct autosa_gen *gen,
                                              struct autosa_hw_module *module,
                                              struct autosa_kernel_var *var, const char *suffix)
{
  cJSON *buffer = cJSON_CreateObject();

  /* Generate buffer name */
  char *buffer_name = var->name;
  if (suffix)
    buffer_name = concat(gen->ctx, buffer_name, suffix);
  cJSON_AddStringToObject(buffer, "buffer_name", buffer_name);
  if (suffix)
    free(buffer_name);

  /* Generate buffer port width */
  int n_lane = var->n_lane;
  int ele_size = var->array->size;
  int port_w = n_lane * ele_size; // in bytes
  cJSON *port_width = cJSON_CreateNumber(port_w);
  cJSON_AddItemToObject(buffer, "port_width", port_width);

  /* Generate buffer size */
  int size = 1;
  for (int j = 0; j < isl_vec_size(var->size); j++)
  {
    isl_val *v;
    int v_int;
    v = isl_vec_get_element_val(var->size, j);
    v_int = isl_val_get_num_si(v);
    isl_val_free(v);
    size *= v_int;
  }
  cJSON *buffer_size = cJSON_CreateNumber(size);
  cJSON_AddItemToObject(buffer, "buffer_depth", buffer_size);

  /* Partition number */
  cJSON *n_part = cJSON_CreateNumber(var->n_part);
  cJSON_AddItemToObject(buffer, "partition_number", n_part);

  /* Buffer memory type */
  int mem_type = extract_memory_type(module, var, gen->options->autosa->uram);
  if (mem_type == 0)
    cJSON_AddStringToObject(buffer, "mem_type", "FF");
  else if (mem_type == 1)
    cJSON_AddStringToObject(buffer, "mem_type", "LUTRAM");
  else if (mem_type == 2)
    cJSON_AddStringToObject(buffer, "mem_type", "BRAM");
  else
    cJSON_AddStringToObject(buffer, "mem_type", "URAM");

  ///* Array map */
  //if (module->double_buffer) {
  //  cJSON_AddStringToObject(buffer, "array_map", "horizontal");
  //}

  return buffer;
}

/* If "buffer" is set 1, extract local buffer information. */
static cJSON *extract_design_info_from_module(struct autosa_gen *gen,
                                              struct autosa_hw_module *module, char *module_name, int buffer)
{
  cJSON *info = cJSON_CreateObject();
  int double_buffer = module->double_buffer;

  if (module->type == PE_MODULE)
  {
    /* Extract the SIMD factor */
    cJSON *unroll = cJSON_CreateNumber(gen->kernel->simd_w);
    cJSON_AddItemToObject(info, "unroll", unroll);
    cJSON *lat_hide_len = cJSON_CreateNumber(gen->kernel->lat_hide_len);
    cJSON_AddItemToObject(info, "latency_hide_len", lat_hide_len);

    int *fifo_lanes_num = (int *)malloc(module->n_io_group * sizeof(int));
    for (int i = 0; i < module->n_io_group; i++)
      fifo_lanes_num[i] = module->io_groups[i]->n_lane;
    cJSON *fifo_lanes = cJSON_CreateIntArray(fifo_lanes_num, module->n_io_group);
    cJSON_AddItemToObject(info, "fifo_lanes", fifo_lanes);
    free(fifo_lanes_num);
  }
  else
  {
    /* Extract the input and output data lanes and width */
    cJSON *data_pack_inter = cJSON_CreateNumber(module->data_pack_inter);
    cJSON *data_pack_intra = cJSON_CreateNumber(module->data_pack_intra);
    cJSON_AddItemToObject(info, "data_pack_inter", data_pack_inter);
    cJSON_AddItemToObject(info, "data_pack_intra", data_pack_intra);

    struct autosa_array_ref_group *group = module->io_groups[0];
    struct autosa_array_info *array = group->array;
    cJSON_AddStringToObject(info, "ele_type", array->type);
    cJSON *data_size = cJSON_CreateNumber(array->size);
    cJSON_AddItemToObject(info, "ele_size", data_size);

    /* Mark the module accessing the DRAM */
    if (module->to_mem) {
      cJSON_AddNumberToObject(info, "access_mem", 1);
    } else {
      cJSON_AddNumberToObject(info, "access_mem", 0);
    }
  }
  /* Extract the local buffer */
  if (buffer)
  {
    cJSON *buffers = cJSON_CreateArray();
    for (int i = 0; i < module->n_var; ++i)
    {
      cJSON *buffer = NULL;
      struct autosa_kernel_var *var = &module->var[i];
      if (double_buffer)
      {
        buffer = extract_buffer_info_from_module(gen, module, var, "ping");
        cJSON_AddItemToArray(buffers, buffer);
        buffer = extract_buffer_info_from_module(gen, module, var, "pong");
        cJSON_AddItemToArray(buffers, buffer);
      }
      else
      {
        buffer = extract_buffer_info_from_module(gen, module, var, NULL);
        cJSON_AddItemToArray(buffers, buffer);
      }
    }
    cJSON_AddItemToObject(info, "local_buffers", buffers);
  }

  return info;
}

static cJSON *extract_design_info_from_serialize_module(struct autosa_gen *gen,
                                                        struct autosa_hw_module *module, char *module_name)
{
  cJSON *info = cJSON_CreateObject();
  /* Extract the input and output data lanes and width */
  cJSON *data_pack_inter = cJSON_CreateNumber(module->data_pack_serialize);
  cJSON *data_pack_intra = cJSON_CreateNumber(module->data_pack_intra);
  cJSON_AddItemToObject(info, "data_pack_inter", data_pack_inter);
  cJSON_AddItemToObject(info, "data_pack_intra", data_pack_intra);

  struct autosa_array_ref_group *group = module->io_groups[0];
  struct autosa_array_info *array = group->array;
  cJSON_AddStringToObject(info, "ele_type", array->type);
  cJSON *data_size = cJSON_CreateNumber(array->size);
  cJSON_AddItemToObject(info, "ele_size", data_size);

  return info;
}

/* Extract the data packing factor "n_lane" for PE dummy module.
 * Note that for PE dummay module with internal array, if the I/O type is 
 * interior I/O, we look for the n_lane of IO_L1 buffer.
 */
static cJSON *extract_design_info_from_pe_dummy_module(struct autosa_gen *gen,
                                                       struct autosa_pe_dummy_module *module, char *module_name)
{
  cJSON *info = cJSON_CreateObject();
  struct autosa_array_ref_group *group = module->io_group;
  int n_lane = (group->local_array->array_type == AUTOSA_EXT_ARRAY) ? group->n_lane : ((group->group_type == AUTOSA_DRAIN_GROUP) ? group->n_lane : (group->io_type == AUTOSA_EXT_IO) ? group->n_lane : group->io_buffers[0]->n_lane);
  cJSON *data_pack = cJSON_CreateNumber(n_lane);
  cJSON_AddItemToObject(info, "unroll", data_pack);

  return info;
}

/* Exatract the design information into a JSON struct for resource estimation.
 * If the module contains buffers, extract the buffer information.
 * For I/O modules, extract:
 * - input and output data lanes and width
 * For PE modules, extract:
 * - simd factor if any
 */
isl_stat sa_extract_design_info(struct autosa_gen *gen)
{
  cJSON *design_info = cJSON_CreateObject();
  char *json_str = NULL;
  FILE *fp;
  struct autosa_hw_top_module *top = gen->hw_top_module;
  isl_ctx *ctx = gen->ctx;
  isl_printer *p_str;
  char *file_path;

  /* kernel id */
  //DBGVAR(std::cout, gen->kernel->id);
  cJSON *kernel_id = cJSON_CreateNumber(gen->kernel->id);
  cJSON_AddItemToObject(design_info, "kernel_id", kernel_id);

  /* module */
  cJSON *modules = cJSON_CreateObject();
  cJSON_AddItemToObject(design_info, "modules", modules);
  for (int i = 0; i < gen->n_hw_modules; i++)
  {
    struct autosa_hw_module *module = gen->hw_modules[i];
    char *module_name;
    cJSON *info;

    if (module->is_filter && module->is_buffer)
    {
      /* intra_trans */
      module_name = concat(ctx, module->name, "intra_trans");
      info = extract_design_info_from_module(gen, module, module_name, 0);
      cJSON_AddItemToObject(modules, module_name, info);
      free(module_name);

      /* inter_trans */
      module_name = concat(ctx, module->name, "inter_trans");
      info = extract_design_info_from_module(gen, module, module_name, 0);
      cJSON_AddItemToObject(modules, module_name, info);
      free(module_name);

      if (module->boundary)
      {
        module_name = concat(ctx, module->name, "inter_trans_boundary");
        info = extract_design_info_from_module(gen, module, module_name, 0);
        cJSON_AddItemToObject(modules, module_name, info);
        free(module_name);
      }
    }

    /* default module */
    info = extract_design_info_from_module(gen, module, module_name, 1);
    cJSON_AddItemToObject(modules, module->name, info);

    /* boundary module */
    if (module->boundary)
    {
      module_name = concat(ctx, module->name, "boundary");
      info = extract_design_info_from_module(gen, module, module_name, 1);
      cJSON_AddItemToObject(modules, module_name, info);
      free(module_name);
    }

    if (module->n_pe_dummy_modules > 0)
    {
      for (int i = 0; i < module->n_pe_dummy_modules; i++)
      {
        struct autosa_pe_dummy_module *dummy_module = module->pe_dummy_modules[i];
        struct autosa_array_ref_group *group = dummy_module->io_group;
        char *module_name;
        /* Generate module name */
        isl_printer *p_str = isl_printer_to_str(ctx);
        p_str = isl_printer_print_str(p_str, group->array->name);
        if (group->group_type == AUTOSA_IO_GROUP)
        {
          if (group->local_array->n_io_group > 1)
          {
            p_str = isl_printer_print_str(p_str, "_");
            p_str = isl_printer_print_int(p_str, group->nr);
          }
        }
        else if (group->group_type == AUTOSA_DRAIN_GROUP)
        {
          p_str = isl_printer_print_str(p_str, "_");
          p_str = isl_printer_print_str(p_str, "drain");
        }
        p_str = isl_printer_print_str(p_str, "_PE_dummy");
        if (dummy_module->in) 
          p_str = isl_printer_print_str(p_str, "_in");
        else
          p_str = isl_printer_print_str(p_str, "_out");
        module_name = isl_printer_get_str(p_str);
        isl_printer_free(p_str);
        info = extract_design_info_from_pe_dummy_module(gen, dummy_module, module_name);
        cJSON_AddItemToObject(modules, module_name, info);
        free(module_name);
      }
    }

    if (module->is_serialized) {
      if (module->boundary)
        module_name = concat(ctx, module->name, "boundary_serialize");
      else
        module_name = concat(ctx, module->name, "serialize");
      info = extract_design_info_from_serialize_module(gen, module, module_name);
      cJSON_AddItemToObject(modules, module_name, info);
      free(module_name);
    }
  }

  json_str = cJSON_Print(design_info);
  p_str = isl_printer_to_str(gen->ctx);
  p_str = isl_printer_print_str(p_str, gen->options->autosa->output_dir);
  p_str = isl_printer_print_str(p_str, "/resource_est/design_info.json");
  file_path = isl_printer_get_str(p_str);
  fp = fopen(file_path, "w");
  if (!fp)
  {
    printf("[AutoSA] Error: Cannot open file: %s\n", file_path);
  }
  fprintf(fp, "%s", json_str);
  fclose(fp);
  free(file_path);
  isl_printer_free(p_str);
  cJSON_Delete(design_info);
  free(json_str);

  return isl_stat_ok;
}
