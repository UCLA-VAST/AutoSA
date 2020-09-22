#include <isl/aff.h>

#include <barvinok/isl.h>

#include "autosa_codegen.h"
#include "autosa_utils.h"
#include "autosa_print.h"
#include "autosa_schedule_tree.h"
#include "autosa_comm.h"

/* Generate the I/O module name.
 * [io_group_name]_IO_L[X]_in/out
 */
static char *generate_io_module_name(isl_ctx *ctx,
                                     struct autosa_array_ref_group *group, int level, int read)
{
  isl_printer *p;

  p = isl_printer_to_str(ctx);
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
  p = isl_printer_print_int(p, level);
  if (read)
    p = isl_printer_print_str(p, "_in");
  else
    p = isl_printer_print_str(p, "_out");

  char *str = isl_printer_get_str(p);
  isl_printer_free(p);

  return str;
}

/* Return an isl_multi_aff, with as elements the parameters in "space"
 * that have the names specified by the elements in "names".
 * If (some of) these parameters do not already appear in "space",
 * then they are added first.
 */
static __isl_give isl_multi_aff *parameter_vector(__isl_take isl_space *space,
                                                  __isl_keep isl_id_list *names)
{
  int i, n;
  isl_local_space *ls;
  isl_multi_aff *ma;

  if (!names)
    space = isl_space_free(space);

  n = isl_id_list_n_id(names);
  for (i = 0; i < n; ++i)
  {
    int pos;
    isl_id *id;

    id = isl_id_list_get_id(names, i);
    pos = isl_space_find_dim_by_id(space, isl_dim_param, id);
    if (pos >= 0)
    {
      isl_id_free(id);
      continue;
    }
    pos = isl_space_dim(space, isl_dim_param);
    space = isl_space_add_dims(space, isl_dim_param, 1);
    space = isl_space_set_dim_id(space, isl_dim_param, pos, id);
  }
  ma = isl_multi_aff_zero(isl_space_copy(space));
  ls = isl_local_space_from_space(isl_space_domain(space));
  for (i = 0; i < n; ++i)
  {
    int pos;
    isl_id *id;
    isl_aff *aff;

    id = isl_id_list_get_id(names, i);
    pos = isl_space_find_dim_by_id(space, isl_dim_param, id);
    isl_id_free(id);
    aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
                                isl_dim_param, pos);
    ma = isl_multi_aff_set_aff(ma, i, aff);
  }
  isl_local_space_free(ls);

  return ma;
}

/* Return constraints on the domain elements that are greater or equal 
 * to a sequence of parameters called "names", to the partial schedule of "node".
 * The number of members of the band node "node" should be smaller
 * than or equal to the number of elements in "names". 
 * If it is smaller, then the first elements of "names" are equated to zero.
 */
static __isl_give isl_union_set *set_schedule_ge(
    __isl_keep isl_schedule_node *node, __isl_keep isl_id_list *names)
{
  int n, n_zero;
  isl_multi_union_pw_aff *mupa, *mupa2;
  isl_multi_aff *ma;
  isl_space *space;
  isl_union_set *domain;

  if (!node)
    return NULL;
  n = isl_id_list_n_id(names);
  if (n == 0)
    return isl_schedule_node_get_universe_domain(node);
  n_zero = n - isl_schedule_node_band_n_member(node);

  mupa = isl_schedule_node_band_get_partial_schedule(node);
  space = isl_multi_union_pw_aff_get_space(mupa);
  space = isl_space_params(space);
  space = isl_space_set_from_params(space);
  space = isl_space_add_dims(space, isl_dim_set, n_zero);
  ma = isl_multi_aff_zero(space);
  domain = isl_schedule_node_get_universe_domain(node);
  /* Generate the mupa that is on the same domain of partial schedule, with
   * a function that maps to the n_zero dims to zero. */
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(
      isl_union_set_copy(domain), ma);

  /* Generate the mupa with the n_zero dims as paramters and equal zero. */
  mupa = isl_multi_union_pw_aff_range_product(mupa2, mupa);
  space = isl_multi_union_pw_aff_get_space(mupa);
  ma = parameter_vector(space, names);
  /* Generate the mupa that is on the same domain of partial schedule, with
   * a function that maps the domain elements to the parameters. */
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(domain, ma);
  mupa = isl_multi_union_pw_aff_sub(mupa, mupa2);

  return isl_multi_union_pw_aff_nonneg_union_set(mupa);
}

/* Return constraints on the domain elements that less or equal to a sequence of
 * parameters called "names", to the partial schedule of "node".
 * The number of members of the band node "node" should be smaller
 * than or equal to the number of elements in "names". 
 * If it is smaller, then the first elements of "names" are equated to zero.
 */
static __isl_give isl_union_set *set_schedule_le(
    __isl_keep isl_schedule_node *node, __isl_keep isl_id_list *names)
{
  int n, n_zero;
  isl_multi_union_pw_aff *mupa, *mupa2;
  isl_multi_aff *ma;
  isl_space *space;
  isl_union_set *domain;

  if (!node)
    return NULL;
  n = isl_id_list_n_id(names);
  if (n == 0)
    return isl_schedule_node_get_universe_domain(node);
  n_zero = n - isl_schedule_node_band_n_member(node);

  mupa = isl_schedule_node_band_get_partial_schedule(node);
  space = isl_multi_union_pw_aff_get_space(mupa);
  space = isl_space_params(space);
  space = isl_space_set_from_params(space);
  space = isl_space_add_dims(space, isl_dim_set, n_zero);
  ma = isl_multi_aff_zero(space);
  domain = isl_schedule_node_get_universe_domain(node);
  /* Generate the mupa that is on the same domain of partial schedule, with
   * a function that maps to the n_zero dims to zero. */
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(
      isl_union_set_copy(domain), ma);

  /* Generate the mupa with the n_zero dims as paramters and equal zero. */
  mupa = isl_multi_union_pw_aff_range_product(mupa2, mupa);
  space = isl_multi_union_pw_aff_get_space(mupa);
  ma = parameter_vector(space, names);
  /* Generate the mupa that is on the same domain of partial schedule, with
   * a function that maps the domain elements to the parameters. */
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(domain, ma);
  mupa = isl_multi_union_pw_aff_sub(mupa2, mupa);

  return isl_multi_union_pw_aff_nonneg_union_set(mupa);
}

/* Construct an isl_multi_val for use as tile sizes for tiling "node"
 * from the elements in "tile_size".
 */
static __isl_give isl_multi_val *construct_band_tiles_sizes(
    __isl_keep isl_schedule_node *node, int *tile_size)
{
  isl_space *space;

  if (!node)
    return NULL;

  space = isl_schedule_node_band_get_space(node);
  return ppcg_multi_val_from_int_list(space, tile_size);
}

/* Return constraints on the domain elements that equate a sequence of
 * parameters called "names", to the partial schedule
 * of "node" modulo the integers in "size".
 * The number of elements in the array "size" should be equal
 * to the number of elements in "names".
 * The number of members of the band node "node" should be smaller
 * than or equal to this number.  If it is smaller, then the first
 * elements of "names" are equated to zero.
 */
static __isl_give isl_union_set *set_schedule_modulo(
    __isl_keep isl_schedule_node *node, __isl_keep isl_id_list *names,
    int *size)
{
  int n, n_zero;
  isl_space *space;
  isl_multi_aff *ma;
  isl_multi_union_pw_aff *mupa, *mupa2;
  isl_multi_val *mv;
  isl_union_set *domain;

  if (!node)
    return NULL;
  n = isl_id_list_n_id(names);
  if (n == 0)
    return isl_schedule_node_get_universe_domain(node);
  n_zero = n - isl_schedule_node_band_n_member(node);

  mupa = isl_schedule_node_band_get_partial_schedule(node);
  mv = construct_band_tiles_sizes(node, size + n_zero);
  mupa = isl_multi_union_pw_aff_mod_multi_val(mupa, mv);
  space = isl_multi_union_pw_aff_get_space(mupa);
  space = isl_space_params(space);
  space = isl_space_set_from_params(space);
  space = isl_space_add_dims(space, isl_dim_set, n_zero);
  ma = isl_multi_aff_zero(space);

  domain = isl_schedule_node_get_universe_domain(node);
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(
      isl_union_set_copy(domain), ma);
  mupa = isl_multi_union_pw_aff_range_product(mupa2, mupa);

  space = isl_multi_union_pw_aff_get_space(mupa);
  ma = parameter_vector(space, names);

  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(domain, ma);
  mupa = isl_multi_union_pw_aff_sub(mupa, mupa2);

  return isl_multi_union_pw_aff_zero_union_set(mupa);
}

/* Generate two prefixes: fifo_prefix and buffer_prefix
 * fifo_prefix: fifo_A_0
 * buffer_prefix: local_A_0
 */
static void init_suffix(struct autosa_hw_module *module,
                        struct autosa_array_ref_group *group, char **fifo_suffix, char **buf_suffix)
{
  isl_ctx *ctx = isl_map_get_ctx(group->access);

  isl_printer *p = isl_printer_to_str(ctx);
  p = autosa_array_ref_group_print_fifo_name(group, p);
  *fifo_suffix = isl_printer_get_str(p);
  isl_printer_free(p);

  p = isl_printer_to_str(ctx);
  p = isl_printer_print_str(p, "local_");
  p = isl_printer_print_str(p, group->array->name);
  if ((group->group_type == AUTOSA_IO_GROUP && group->local_array->n_io_group > 1) ||
      (group->group_type == AUTOSA_PE_GROUP && group->local_array->n_pe_group > 1))
  {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_int(p, group->nr);
  }
  if (group->group_type == AUTOSA_DRAIN_GROUP)
  {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, "drain");
  }
  *buf_suffix = isl_printer_get_str(p);
  isl_printer_free(p);
}

///* Return constraints on the domain elements that equate the partial schedule
// * of "node" to the lower bound of partial schedule. 
// */
//static __isl_give isl_union_set *schedule_eq_lb(
//    __isl_keep isl_schedule_node *node)
//{
//  int n, n_zero;
//  isl_multi_union_pw_aff *mupa, *mupa2;
//  isl_multi_aff *ma;
//  isl_space *space;
//  isl_union_set *domain;
//  isl_union_map *umap;
//  isl_union_set *uset;
//  isl_schedule_node *node2;
//  isl_bool under_extension = isl_bool_false;
//
//  if (!node)
//    return NULL;
//
//  /* Test if it is under extension node */
//  node2 = isl_schedule_node_copy(node);
//  while (node2)
//  {
//    if (isl_schedule_node_get_type(node2) == isl_schedule_node_extension)
//    {
//      under_extension = isl_bool_true;
//      break;
//    }
//    if (isl_schedule_node_has_parent(node2))
//      node2 = isl_schedule_node_parent(node2);
//    else
//      break;
//  }
//  isl_schedule_node_free(node2);
//
//  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
//  if (!under_extension)
//  {
//    domain = isl_schedule_node_get_domain(node);
//    umap = isl_union_map_intersect_domain(umap, domain);
//  }
//  uset = isl_union_map_range(isl_union_map_copy(umap));
//  uset = isl_union_set_lexmin(uset);
//  umap = isl_union_map_reverse(umap);
//  uset = isl_union_set_apply(uset, umap);
//
//  return uset;
//}
static __isl_give isl_union_set *schedule_eq_lb(
  __isl_keep isl_schedule_node *node)
{
  isl_schedule_node *child;
  isl_union_map *prefix, *prefix_ge;
  int depth1, depth2;
  isl_set *prefix_range;
  isl_map *sched_identity, *ge;
  isl_union_set *domain;
  isl_schedule_node *node_tmp;
  isl_bool under_extension = isl_bool_false;

  if (!node)
    return NULL;

  /* Test if "node" is under extension node */
  node_tmp = isl_schedule_node_copy(node);
  while (node_tmp) {
    if (isl_schedule_node_get_type(node_tmp) == isl_schedule_node_extension) {
      under_extension = isl_bool_true;
      break;
    }
    if (isl_schedule_node_has_parent(node_tmp)) 
      node_tmp = isl_schedule_node_parent(node_tmp);
    else
      break;
  }
  isl_schedule_node_free(node_tmp);

  if (under_extension) {
//#ifdef _DEBUG    
//    printf("debug: under extension\n");
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif    
    /* Currently all the extension nodes are inserted with rectangular schedule domains.
     * Therefore, we will safely call a routine that handles the rectangular 
     * domains to get the lower bound. 
     */
    isl_union_map *umap;
    isl_union_set *uset;
    umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
    uset = isl_union_map_range(isl_union_map_copy(umap));
    uset = isl_union_set_lexmin(uset);
    umap = isl_union_map_reverse(umap);
    uset = isl_union_set_apply(uset, umap);

    return uset;
  }

  depth1 = isl_schedule_node_get_schedule_depth(node);
  child = isl_schedule_node_child(isl_schedule_node_copy(node), 0);
  depth2 = isl_schedule_node_get_schedule_depth(child);
  prefix = isl_schedule_node_get_prefix_schedule_relation(child);
  isl_schedule_node_free(child);
  prefix_range = isl_set_from_union_set(isl_union_map_range(isl_union_map_copy(prefix)));
  ge = isl_map_lex_ge(isl_set_get_space(prefix_range));
  /* Set the outer dims equal */
  for (int i = 0; i < depth1; i++) {
    ge = isl_map_equate(ge, isl_dim_in, i, isl_dim_out, i);
  }
  ge = isl_map_intersect_domain(ge, isl_set_copy(prefix_range));
  ge = isl_map_intersect_range(ge, prefix_range);
  prefix_ge = isl_union_map_apply_range(isl_union_map_copy(prefix), isl_union_map_from_map(ge));
  prefix_ge = isl_union_map_lexmin(prefix_ge);
  prefix = isl_union_map_intersect(prefix, prefix_ge);
  domain = isl_union_map_domain(prefix);

  return domain;
}

/* Return constraints on the domain elements that not equate the partial schedule
 * of "node" to the lower bound of partial schedule. 
 */
static __isl_give isl_union_set *schedule_neq_lb(
    __isl_keep isl_schedule_node *node)
{
  isl_union_set *uset, *domain;
  isl_union_map *umap;

  if (!node)
    return NULL;

  uset = schedule_eq_lb(node);
  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  domain = isl_union_map_domain(umap);
  uset = isl_union_set_subtract(domain, uset);

  return uset;
}

/* Return constraints on the domain elements that equate the partial schedule
 * of "node" to the upper bound of partial schedule. 
 */
static __isl_give isl_union_set *schedule_eq_ub(
    __isl_keep isl_schedule_node *node)
{
  /* Compute the prefix schedule, 
   * Build a relation that sets the demensions before the current band
   * equal, and the current dim le. 
   * Intersect the relation with the schedule range.
   * Apply the relation to the current prefix schedule range.
   * Compute the lexmax of the range.
   * Get the domain.
   */
  isl_schedule_node *child;
  isl_union_map *prefix, *prefix_le;
  int depth1, depth2;
  isl_set *prefix_range;
  isl_map *sched_identity, *le;
  isl_union_set *domain;

  if (!node)
    return NULL;

  depth1 = isl_schedule_node_get_schedule_depth(node);
  child = isl_schedule_node_child(isl_schedule_node_copy(node), 0);
  depth2 = isl_schedule_node_get_schedule_depth(child);
  prefix = isl_schedule_node_get_prefix_schedule_relation(child);
  isl_schedule_node_free(child);
  prefix_range = isl_set_from_union_set(isl_union_map_range(isl_union_map_copy(prefix)));   
  le = isl_map_lex_le(isl_set_get_space(prefix_range));  
  /* Set the outer dims equal */
  for (int i = 0; i < depth1; i++) {
    le = isl_map_equate(le, isl_dim_in, i, isl_dim_out, i);
  }
  le = isl_map_intersect_domain(le, isl_set_copy(prefix_range));
  le = isl_map_intersect_range(le, prefix_range);
  prefix_le = isl_union_map_apply_range(isl_union_map_copy(prefix), isl_union_map_from_map(le));
  prefix_le = isl_union_map_lexmax(prefix_le);
  prefix = isl_union_map_intersect(prefix, prefix_le);
  domain = isl_union_map_domain(prefix);

  return domain;
}

/* Return constraints on the domain elements that not equate the partial schedule
 * of "node" to the upper bound of partial schedule. 
 */
static __isl_give isl_union_set *schedule_neq_ub(
    __isl_keep isl_schedule_node *node)
{
  isl_union_set *uset, *domain, *sched_domain;
  isl_union_map *umap;

  if (!node)
    return NULL;

  uset = schedule_eq_ub(node);
  domain = isl_schedule_node_get_domain(node);
  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  umap = isl_union_map_intersect_domain(umap, domain);
  sched_domain = isl_union_map_domain(umap);
  uset = isl_union_set_subtract(sched_domain, uset);

  return uset;
}

/* Internal struct used for add_io_copies_stmt_acc. */
struct add_io_copies_stmt_acc_data
{
  struct autosa_kernel *kernel;
  struct autosa_array_ref_group *group;
  struct autosa_stmt_access *ref;
  struct autosa_array_tile *local_tile; /* Local buffer tile */
  int n_lane;
  int read;
  char *stmt_name;
  int insert_dependence;
};

/* Create an IO statement. 
 * "io_group" is the current I/O group that is analyzed.
 * "local_tile" is the tile that the current IO stmt accesses.
 * "depth" is the schedule depth that the current stmt is inserted at.
 */
static __isl_give isl_multi_aff *autosa_create_io_access_stmt(
    isl_ctx *ctx,
    struct autosa_array_ref_group *local_group,
    struct autosa_array_ref_group *io_group,
    struct autosa_array_tile *tile,
    int depth,
    __isl_keep char *stmt_name)
{
  isl_space *space;
  isl_id *id;
  char buf[100];
  struct autosa_array_ref_group_pair *pair =
      (struct autosa_array_ref_group_pair *)malloc(
          sizeof(struct autosa_array_ref_group_pair));
  pair->local_group = local_group;
  pair->io_group = io_group;
  pair->local_tile = tile;
  pair->in_use = 0;  
  if (io_group->n_lane > 1 && io_group->local_array->array_type == AUTOSA_INT_ARRAY) {    
    pair->simd_depth = depth;
  } else {    
    pair->simd_depth = -1;
  }

  space = isl_space_copy(io_group->array->space);
  space = isl_space_from_range(space);
  space = isl_space_add_dims(space, isl_dim_in, depth);
  space = isl_space_wrap(space);
  space = isl_space_map_from_set(space);

  sprintf(buf, "%s", stmt_name);

  id = isl_id_alloc(ctx, buf, pair);
  id = isl_id_set_free_user(id, &free_group_pair);
  space = isl_space_set_tuple_id(space, isl_dim_in, id);

  return isl_multi_aff_identity(space);
}

/* Test if the array access "ref" is stride-0 or stride-1 under the current
 * schedule node.
 */
static isl_bool is_acc_stride_one_at_node(
    __isl_keep isl_schedule_node *node, struct autosa_stmt_access *ref)
{
  isl_union_set *domain;
  isl_union_map *prefix;
  isl_map *acc;
  isl_bool is_zero = isl_bool_false, is_one = isl_bool_false;
  
  prefix = isl_schedule_node_get_prefix_schedule_union_map(node);

  /* Scalar access */
  if (ref->n_index == 0)
    return isl_bool_true;

  /* Transform the domain of access function to scheduling domains. */
  acc = isl_map_copy(ref->access);
  acc = isl_map_from_union_map(
      isl_union_map_apply_domain(isl_union_map_from_map(acc), prefix));
  is_one = access_is_stride_one(acc, ref->n_index - 1);

  isl_map_free(acc);  
  return is_one;
}

/* Insert the copy statement at the statement level.
 */
static __isl_give isl_schedule_node *add_io_copies_stmt_acc_single(
    __isl_take isl_schedule_node *node, void *user)
{
  struct add_io_copies_stmt_acc_data *data =
      (struct add_io_copies_stmt_acc_data *)(user);
  struct autosa_array_ref_group *group = data->group;
  struct autosa_stmt_access *ref = data->ref;
  char *stmt_name = data->stmt_name;
  int read = data->read;
  isl_union_set *uset, *empty_filter, *domain;
  isl_set *set;
  isl_space *space;
  isl_id *id, *id2;
  isl_ctx *ctx;
  isl_union_map *access;
  int empty;
  struct autosa_array_tile *tile;
  isl_multi_aff *ma, *from_access;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  isl_schedule_node *graft;
  int n_lane = data->n_lane;
  int is_simd;
  isl_id *hls_id;
  isl_bool stride_one;
  isl_bool insert_dependence = isl_bool_false;
  isl_bool under_extension;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return node;  

  /* Examine if the statement contains the access. */
  uset = isl_schedule_node_get_domain(node);
  if (isl_union_set_is_empty(uset)) {
    isl_union_set_free(uset);
    return node;
  }

  set = isl_set_from_union_set(isl_union_set_copy(uset));
  space = isl_set_get_space(set);
  isl_set_free(set);
  id = isl_space_get_tuple_id(space, isl_dim_set);
  isl_space_free(space);
  space = isl_map_get_space(ref->access);
  id2 = isl_space_get_tuple_id(space, isl_dim_in);
  empty_filter = isl_union_set_empty(isl_union_set_get_space(uset));
  isl_union_set_free(uset);
  isl_space_free(space);

  if (id != id2)
  {
    isl_id_free(id);
    isl_id_free(id2);
    node = isl_schedule_node_insert_filter(node, empty_filter);
    return node;
  }
  isl_id_free(id);
  isl_id_free(id2);
  ctx = isl_schedule_node_get_ctx(node);
  is_simd = is_node_under_simd(node);

  /* S -> [D -> A] */
  access = io_comm_access_ref(data->kernel, node, group, ref, read);
  //DBGUMAP(stdout, access, isl_union_map_get_ctx(access))

  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    isl_union_set_free(empty_filter);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return node;
  }

  /* Update the stmt_name. */
  if (data->insert_dependence)
  {
    isl_schedule_node *node2;

    node2 = isl_schedule_node_copy(node);
    if (n_lane >= 1 && is_simd)
    {
      //node2 = isl_schedule_node_parent(node);
      while (!is_marked(node2, "simd")) {
        node2 = isl_schedule_node_parent(node2);
      }
      node2 = isl_schedule_node_child(node2, 0);
    }
    /* Test if the access is stride one at the current loop. */
    stride_one = is_acc_stride_one_at_node(node2, ref);
    if (stride_one)
    {
      /* Test if the loop bound/n_lane > 1. 
       * If so, insert a hls_dep mark.
       * Only do this when there is a single access in the group.
       */
//#ifdef _DEBUG
//      DBGSCHDNODE(stdout, node2, isl_schedule_node_get_ctx(node2));
//#endif
      int *ubs = NULL;
      isl_schedule_node *node_copy = isl_schedule_node_copy(node2);
      if (is_simd) {
        while (node_copy && isl_schedule_node_has_parent(node_copy)) {
          if (is_marked(node_copy, "simd")) 
            break;
          node_copy = isl_schedule_node_parent(node_copy);
        }
      }
      while (node_copy && isl_schedule_node_has_parent(node_copy))
      {
        if (isl_schedule_node_get_type(node_copy) == isl_schedule_node_band)
          break;
        node_copy = isl_schedule_node_parent(node_copy);
      }
      if (isl_schedule_node_get_type(node_copy) == isl_schedule_node_band)
      {
        int n = isl_schedule_node_band_n_member(node_copy);
//#ifdef _DEBUG
//        DBGSCHDNODE(stdout, node_copy, isl_schedule_node_get_ctx(node_copy));
//#endif        
        ubs = extract_band_upper_bounds(node_copy);
        if (ubs[n - 1] / n_lane > 1)
        {
          insert_dependence = isl_bool_true;
          /* Update the stmt_name. */
          int coalesce_depth;
          int coalesce_bound;

          //coalesce_depth = isl_schedule_node_get_schedule_depth(node_copy) - 1;
          node_copy = isl_schedule_node_child(node_copy, 0);
          coalesce_depth = isl_schedule_node_get_schedule_depth(node_copy) - 1;
          coalesce_bound = ubs[n - 1] / n_lane;

          isl_printer *p_str = isl_printer_to_str(ctx);
          p_str = isl_printer_print_str(p_str, stmt_name);
          p_str = isl_printer_print_str(p_str, ".");
          p_str = isl_printer_print_int(p_str, coalesce_depth);
          p_str = isl_printer_print_str(p_str, ".");
          p_str = isl_printer_print_int(p_str, coalesce_bound);
          free(stmt_name);
          stmt_name = isl_printer_get_str(p_str);
          isl_printer_free(p_str);
        }
      }
      free(ubs);
      isl_schedule_node_free(node_copy);
    }
    isl_schedule_node_free(node2);
  }

  from_access = autosa_create_io_access_stmt(
      ctx, group, group, data->local_tile,
      isl_schedule_node_get_schedule_depth(node), stmt_name);
  free(stmt_name);

  /* Create a register tiling. */
  tile = create_register_tiling(node, group, ref);
  ma = isl_multi_aff_copy(tile->tiling);
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);

  /* [D -> A] */
  domain = isl_union_map_range(access);
  /* Only for read, we extend the access to a rectangular hull which helps to 
   * improve the memory coalescing. 
   */
  if (read && !autosa_array_is_scalar(group->array))
  {
    isl_map *map;
    isl_set *set;
    set = isl_map_domain(isl_map_from_union_map(isl_union_set_unwrap(domain)));
    map = group_tile_buffer(group, tile);
    map = isl_map_intersect_domain(map, set);
    domain = isl_union_set_from_set(isl_map_wrap(map));
  }

  /* read.fifoX[D -> A] */
  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  /* read.fifoX[D -> A] -> D */
  access = isl_union_set_wrapped_domain_map(domain);
  /* D -> read.fifoX[D -> A] */
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

  graft = isl_schedule_node_from_extension(access);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  /* If the current statement is under the SIMD loop, we will add a filter 
   * to only transfer the data at one loop since we will later insert a 
   * statement to handle the data transfer of the entire SIMD loop.
   */
#ifdef ISL_SINK  
  if (n_lane >= 1 && is_simd)
  {
    /* The loop above is the SIMD loop.
     * Check the node is below the simd mark. 
     */
    int n_index;
    int tile_size[1];
    isl_id *id;
    isl_printer *p_str;
    isl_union_map *umap;
    isl_union_set *filter;
    /* Create a filter. */    
    node = isl_schedule_node_parent(node);
    if (data->read)
      filter = schedule_eq_lb(node);
    else
      filter = schedule_eq_ub(node);
    node = isl_schedule_node_insert_filter(node, filter);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_child(node, 0);
  }
#endif

  /* Insert a "pipeline" mark under the band node. */
  hls_id = isl_id_alloc(ctx, "hls_pipeline", NULL);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_mark(graft, hls_id);
  graft = isl_schedule_node_parent(graft);

  if (insert_dependence)
  {
    char *mark_name;
    isl_id *id;
    isl_printer *p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "hls_dependence.");
    p_str = autosa_array_ref_group_print_name(group, p_str);
    mark_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    id = isl_id_alloc(ctx, mark_name, NULL);
    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_insert_mark(graft, id);
    free(mark_name);
  }

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);
  node = isl_schedule_node_insert_filter(node, empty_filter);
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_parent(node);  

  autosa_array_tile_free(tile);

  return node;
}

static __isl_give isl_schedule_node *modify_simd_loop(
  __isl_take isl_schedule_node *node, void *user)
{
  struct add_io_copies_stmt_acc_data *data =
      (struct add_io_copies_stmt_acc_data *)(user);
  if (data->n_lane >= 1 && is_marked(node, "simd")) {
    int n_index;
    int tile_size[1];
    isl_id *id;
    isl_printer *p_str;
    isl_union_map *umap;
    isl_union_set *filter;

    node = isl_schedule_node_child(node, 0);
    if (data->read)
      filter = schedule_eq_lb(node);
    else
      filter = schedule_eq_ub(node);
    node = isl_schedule_node_insert_filter(node, filter);
    node = isl_schedule_node_parent(node);
  }
  return node;
}

/* Add copies at the stmt level for each array reference in the "group" 
 * in the I/O modules.
 * 
 * "group" is an I/O group.
 * "read" denotes if copy-in or copy-out from/to the external memory.
 * "in" denotes the fifo direction.
 * "insert_dependence" determines if it is necessary to insert a hls dependence mark.
 */
__isl_give isl_schedule_node *add_io_copies_stmt_acc(
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group,
  __isl_take isl_schedule_node *node,
  struct autosa_array_tile *tile, /* local tile */
  int n_lane,
  int read,
  __isl_take char *stmt_name,
  int before,
  int insert_dependence)
{
  struct add_io_copies_stmt_acc_data data = {
      kernel, group, NULL, tile, n_lane, read, stmt_name,
      insert_dependence && group->n_ref == 1};

  for (int i = 0; i < group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = group->refs[i];
    data.ref = ref;
    //DBGMAP(stdout, ref->access, kernel->ctx)    
    if ((read && ref->read) || (!read && ref->write)) {
      node = isl_schedule_node_map_descendant_bottom_up(
          node, &add_io_copies_stmt_acc_single, &data);
    }
  }
#ifndef ISL_SINK  
  /* Modify the SIMD loop.
   * If the current statement is under the SIMD loop, we will add a filter 
   * to only transfer the data at one loop since we will later insert a 
   * statement to handle the data transfer of the entire SIMD loop.   
   */
  node = isl_schedule_node_map_descendant_bottom_up(node, &modify_simd_loop, &data);
#endif  

  return node;
}

/* Insert the copy statement at the node level to transfer the entire tie.
 * If "is_buffer" is set, add a marker for dependence false. This is
 * only for Xilinx platform.
 */
static __isl_give isl_schedule_node *add_io_copies_stmt_tile(
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group,
  __isl_take isl_schedule_node *node,
  struct autosa_array_tile *local_tile, /* Local buffer */
  struct autosa_array_tile *tile,       /* The tile to be copied */
  int n_lane,
  int read,
  __isl_take char *stmt_name,
  int before, int is_buffer,
  /* If it is proper to insert hls_pipeline for Xilinx platforms. */
  int insert_dependence,
  /* If needs to insert a access_serialize mark. */
  int insert_serialize)
{
  isl_union_map *access = NULL;
  int empty;
  isl_multi_aff *from_access;
  isl_multi_aff *ma;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  isl_union_set *domain;
  isl_schedule_node *graft;
  int n;
  isl_id *id;
  isl_ctx *ctx = kernel->ctx;
  int coalesce_depth;
  int coalesce_bound;
  isl_val *coalesce_bound_val;

  access = io_comm_access(kernel, node, group, read);

  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return node;
  }

  from_access = autosa_create_io_access_stmt(kernel->ctx, group, group,
                                             local_tile, isl_schedule_node_get_schedule_depth(node), stmt_name);

  ma = isl_multi_aff_copy(tile->tiling);
//#ifdef _DEBUG
//  DBGMA(stdout, from_access, isl_multi_aff_get_ctx(from_access))
//  DBGMA(stdout, ma, isl_multi_aff_get_ctx(from_access))
//#endif
  
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);

  domain = isl_union_map_range(access);
  /* Restrain the buffer to the local tile size. */
  if (!autosa_array_is_scalar(group->array))
  {
    isl_map *map;
    isl_set *set;
    set = isl_map_domain(isl_map_from_union_map(isl_union_set_unwrap(domain)));
    map = group_tile_buffer(group, tile);
    map = isl_map_intersect_domain(map, set);
    domain = isl_union_set_from_set(isl_map_wrap(map));
  }

  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  access = isl_union_set_wrapped_domain_map(domain);
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

  graft = isl_schedule_node_from_extension(access);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  /* Split off the last dimension. */
  n = isl_schedule_node_band_n_member(graft);
  if (n > 1)
  {
    graft = isl_schedule_node_band_split(graft, n - 1);
    graft = isl_schedule_node_child(graft, 0);
  }

  /* Insert a coalesce mark indicating the loop below could be used for
   * memory coalescing.
   */
  id = isl_id_alloc(ctx, "access_coalesce", NULL);
  graft = isl_schedule_node_insert_mark(graft, id);
  graft = isl_schedule_node_child(graft, 0);

  if (insert_serialize) {
    id = isl_id_alloc(ctx, "access_serialize", NULL);
    graft = isl_schedule_node_insert_mark(graft, id);
    graft = isl_schedule_node_child(graft, 0);
  }

  if (n_lane > 1)
  {
    /* Peform data packing. 
     * We will tile the last dimension by the factor of data packing.
     * Then we insert a filter to transfer data only once.
     */
    int tile_size[1];
    isl_id *id;
    isl_printer *p_str;
    isl_union_map *umap;
    isl_union_set *filter;
    int depth;

    /* Tile the last dimension. */
    tile_size[0] = n_lane;
    graft = autosa_tile_band(graft, tile_size);
    graft = isl_schedule_node_child(graft, 0);
    /* Create a filter. */
    filter = schedule_eq_lb(graft);
    graft = isl_schedule_node_insert_filter(graft, filter);
    /* Move to the tile loop */
    graft = isl_schedule_node_parent(graft);
  }
  free(stmt_name);
  /* Insert a "pipeline" mark inside the band node. */
  id = isl_id_alloc(ctx, "hls_pipeline", NULL);

  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_mark(graft, id);
  graft = isl_schedule_node_parent(graft);

  if (is_buffer && !read && insert_dependence)
  {
    // TODO: should not be inter_trans or intra_trans.
    // TODO: only add this pragma for io_transfer statement which requires data packing.
    /* Insert a "dependence" mark. 
     * This is not safe. Currently only insert the mark when there is at least 
     * one level of coalesce loop (coalesce_bound > 1) and
     * when data_pack does not equal to the nxt_data_pack. 
     */
    char *mark_name;
    isl_printer *p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "hls_dependence.");
    p_str = autosa_array_ref_group_print_name(group, p_str);
    mark_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    id = isl_id_alloc(ctx, mark_name, NULL);
    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_insert_mark(graft, id);
    free(mark_name);
  }

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  if (before)
  {
    node = isl_schedule_node_graft_before(node, graft);
  }
  else
  {
    node = isl_schedule_node_graft_after(node, graft);
  }

  return node;
}

/* Set all the module io dims equals to the module identifier above the io_level.
 * If the module is a filter, set the io dim greater or equal than the 
 * identifier at the io_level.
 * If the module is connect to pe, set the level 1 io dim equal to the lb/ub.
 * The node should point to the "array" mark.
 */
static __isl_give isl_schedule_node *add_io_ids_filter(
  __isl_take isl_schedule_node *node, 
  __isl_keep isl_id_list *io_ids,  
  int io_level, int n_io_ids, int is_filter, int to_pe, int read)
{
  isl_union_set *core;
  int io_id = 0;

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  core = isl_union_set_universe(isl_schedule_node_get_domain(node));
  //for (int i = n_io_ids + 1; i >= io_level; i--) {
  for (int i = io_level + n_io_ids - 1; i >= io_level; i--) {
    node = autosa_tree_move_down_to_io_mark(node, core, i);
    node = isl_schedule_node_parent(node);
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
      isl_id *id;
      isl_id_list *ids;
      isl_union_set *uset;

      ids = isl_id_list_from_id(isl_id_list_get_id(io_ids, io_id));
      if (io_id == n_io_ids - 1) {
        if (is_filter)
          uset = set_schedule_ge(node, ids);
        else
          uset = set_schedule_eq(node, ids);
      } else {
        uset = set_schedule_eq(node, ids);
      }
      io_id++;
      node = isl_schedule_node_insert_filter(node, uset);
      isl_id_list_free(ids);
    }
  }
  if (to_pe && io_level > 1)
  {
    /* Add filter to only send data to boundary PEs. */
    while (!isl_schedule_node_is_io_mark(node, 2)) {
      node = isl_schedule_node_child(node, 0);
    }
    node = isl_schedule_node_child(node, 0);
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
      isl_union_set *uset;

      if (read)
        uset = schedule_eq_lb(node);
      else
        uset = schedule_eq_ub(node);
      node = isl_schedule_node_insert_filter(node, uset);
      node = isl_schedule_node_child(node, 0);
    }
  }

  isl_union_set_free(core);

  return node;

  //int io_id = 0;
  ////node = autosa_tree_move_down_to_array(node, kernel->core);  
  //while (!isl_schedule_node_is_io_mark(node, io_level))
  //{
  //  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  //  {
  //    isl_id *id;
  //    isl_id_list *ids;
  //    isl_union_set *uset;
//
  //    ids = isl_id_list_from_id(isl_id_list_get_id(io_ids, io_id));
  //    if (io_id == n_io_ids - 1)
  //    {
  //      if (is_filter)
  //      {
  //        uset = set_schedule_ge(node, ids);
  //      }
  //      else
  //      {
  //        uset = set_schedule_eq(node, ids);
  //      }
  //    }
  //    else
  //    {
  //      uset = set_schedule_eq(node, ids);
  //    }
  //    io_id++;
  //    node = isl_schedule_node_insert_filter(node, uset);
  //    isl_id_list_free(ids);
  //    node = isl_schedule_node_child(node, 0);      
  //  }
  //  node = isl_schedule_node_child(node, 0);
  //}
  //if (to_pe && io_level > 1)
  //{
  //  /* Add filter to only send data to boundary PEs. */
  //  while (!isl_schedule_node_is_io_mark(node, 2)) {
  //    node = isl_schedule_node_child(node, 0);
  //  }
  //  node = isl_schedule_node_child(node, 0);
  //  if (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
  //    isl_union_set *uset;
//
  //    if (read)
  //      uset = schedule_eq_lb(node);
  //    else
  //      uset = schedule_eq_ub(node);
  //    node = isl_schedule_node_insert_filter(node, uset);
  //    node = isl_schedule_node_child(node, 0);
  //  }
  //}
//
  //return node;
}

static __isl_give isl_printer *print_io_stmt_prefix(
  __isl_take isl_printer *p,
  int read, int dummy, int reduce,
  struct autosa_array_ref_group *group)
{
  /* io_type */
  p = isl_printer_print_str(p, read ? "in" : "out");
  if (dummy)
    p = isl_printer_print_str(p, "_dummy");
  if (reduce)
    p = isl_printer_print_str(p, "_reduce");
  
  /* fifo_name */
  p = isl_printer_print_str(p, ".");
  if (group->group_type != AUTOSA_PE_GROUP)
  {
    p = isl_printer_print_str(p, "fifo_");
  }
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

  /* cur_data_pack */
  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_int(p, group->n_lane);

  /* next_data_pack */
  p = isl_printer_print_str(p, ".1");

  return p;
}

/* Print the io transfer statement prefix in the format of:
 * in/out_trans[_dram]/[_dram_serialize]/[_boundary]/[_reduce_[op]].
 * [in_fifo_name].[out_fifo_name].[is_buffer].[cur_pack_lane].[nxt_pack_lane].
 * [coalesce_depth].[coalesce_bound]
 */
static __isl_give isl_printer *print_io_trans_stmt_prefix(
  __isl_take isl_printer *p, 
  int read, int to_mem, int serialize, int boundary, int reduce,
  char *reduce_op,
  int in_local, int out_local,
  int is_buffer,
  char *fifo_suffix, int n_lane) 
{
  /* io_trans_type */
  p = isl_printer_print_str(p, read ? "in_trans" : "out_trans");
  if (to_mem) {
    p = isl_printer_print_str(p, "_dram");
    if (serialize)
      p = isl_printer_print_str(p, "_serialize");
  }
  if (boundary)
    p = isl_printer_print_str(p, "_boundary");
  if (reduce) {
    p = isl_printer_print_str(p, "_reduce_");
    p = isl_printer_print_str(p, reduce_op);
  }

  /* in_fifo_name */
  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_str(p, fifo_suffix);
  if (in_local)
    p = isl_printer_print_str(p, "_local");

  /* out_fifo_name */
  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_str(p, fifo_suffix);
  if (out_local)
    p = isl_printer_print_str(p, "_local");  

  /* is_buffer */
  p = isl_printer_print_str(p, is_buffer == 0 ? ".0" : ".1");

  /* cur_pack_lane */
  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_int(p, n_lane);

  return p;
}

static __isl_give isl_printer *print_trans_stmt_coalesce(
    __isl_take isl_printer *p,
    __isl_keep isl_schedule_node *node,
    struct autosa_io_buffer *buf,
    int *coalesce_bound
    ) 
{
  int coalesce_depth;
  isl_val *coalesce_bound_val;
  
  coalesce_depth = isl_schedule_node_get_schedule_depth(node) + buf->tile->n - 1;
  coalesce_bound_val = buf->tile->bound[buf->tile->n - 1].size;  
  *coalesce_bound = isl_val_get_num_si(coalesce_bound_val) / buf->n_lane;    
  if (*coalesce_bound <= 1)
    coalesce_depth = -1;

  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_int(p, coalesce_depth);
  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_int(p, *coalesce_bound);

  return p;
}

static __isl_give isl_union_set *compute_io_group_access_domain(
  __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  int read
){
  isl_union_map *group_access;
  isl_union_set *group_domain;
  isl_union_map *prefix;
  isl_schedule_node *node_tmp;

  node_tmp = isl_schedule_node_copy(node);
  node_tmp = autosa_tree_move_up_to_kernel(node_tmp);
  group_access = autosa_io_group_access_relation(group, kernel, read, !read);  
  /* Remove the local accesses below the array level. */
  node_tmp = autosa_tree_move_down_to_array(node_tmp, kernel->core);
  prefix = isl_schedule_node_get_prefix_schedule_relation(node_tmp);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  if (group->local_array->array_type == AUTOSA_INT_ARRAY)
    group_access = remove_local_accesses_group_flow(kernel, group, group_access, prefix, read);  
  isl_union_map_free(prefix);
  isl_schedule_node_free(node_tmp);

  group_domain = isl_union_map_domain(group_access);
  group_domain = isl_union_set_coalesce(group_domain);

  return group_domain;  
}

/* Compute the iteration domain used by the io_group and add the 
 * domain as a filter at the top of the schedule tree.
 */
static __isl_give isl_schedule_node *insert_io_group_access_domain(
  __isl_take isl_schedule_node *node, 
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  int read)
{
  isl_union_set *group_domain;
  group_domain = compute_io_group_access_domain(node, group, kernel, read);
  node = isl_schedule_node_insert_filter(node, group_domain);
  return node;

//  isl_union_map *group_access;
//  isl_union_set *group_domain;
//  isl_union_map *prefix;
//  isl_schedule_node *node_tmp;
//
//  node_tmp = isl_schedule_node_copy(node);
//  node_tmp = autosa_tree_move_up_to_kernel(node_tmp);
//  group_access = autosa_io_group_access_relation(group, kernel, read, !read);  
//  /* Remove the local accesses below the array level. */
//  node_tmp = autosa_tree_move_down_to_array(node_tmp, kernel->core);
//  prefix = isl_schedule_node_get_prefix_schedule_relation(node_tmp);
//  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
//                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
//  if (group->local_array->array_type == AUTOSA_INT_ARRAY)
//    group_access = remove_local_accesses_group_flow(kernel, group, group_access, prefix, read);  
//  isl_union_map_free(prefix);
//  isl_schedule_node_free(node_tmp);
//
//  group_domain = isl_union_map_domain(group_access);
//  group_domain = isl_union_set_coalesce(group_domain);
//  /* Add the group domain as the filter. */   
//  node = isl_schedule_node_insert_filter(node, group_domain);
//
//  return node;
}

//static __isl_give isl_schedule_node *insert_io_group_access_domain_universe(
//  __isl_take isl_schedule_node *node, 
//  struct autosa_array_ref_group *group,
//  struct autosa_kernel *kernel,
//  int read)
//{
//  isl_union_map *group_access;
//  isl_union_set *group_domain;
//  isl_union_map *prefix;
//  isl_schedule_node *node_tmp;
//
//  node_tmp = isl_schedule_node_copy(node);
//  node_tmp = autosa_tree_move_up_to_kernel(node_tmp);
//  group_access = autosa_io_group_access_relation(group, kernel, read, !read);  
//  /* Remove the local accesses below the array level. */
//  node_tmp = autosa_tree_move_down_to_array(node_tmp, kernel->core);
//  prefix = isl_schedule_node_get_prefix_schedule_relation(node_tmp);
//  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
//                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
//  if (group->local_array->array_type == AUTOSA_INT_ARRAY)
//    group_access = remove_local_accesses_group_flow(kernel, group, group_access, prefix, read);  
//  isl_union_map_free(prefix);
//  isl_schedule_node_free(node_tmp);
//
//  group_domain = isl_union_map_domain(group_access);
//  group_domain = isl_union_set_coalesce(group_domain);
//  group_domain = isl_union_set_universe(group_domain);
//  
//  /* Add the group domain as the filter. */  
//  node = isl_schedule_node_insert_filter(node, group_domain);  
//
//  return node;
//}

static __isl_give isl_union_set *compute_io_group_access_domain_local_reduce(
  __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  int read, int io_group, int drain_group)
{
  isl_union_map *group_access;
  isl_union_set *group_domain;
  isl_union_map *prefix;
  isl_schedule_node *node_tmp;

  node_tmp = isl_schedule_node_copy(node);
  group_access = isl_union_map_empty(isl_map_get_space(group->access));

  if (io_group) {
    struct autosa_array_ref_group *cur_group = group;
    group_access = isl_union_map_union(group_access,
                                       autosa_io_group_access_relation(cur_group, kernel, read, !read));  
    /* Remove the local accesses below the array level. */  
    node_tmp = autosa_tree_move_up_to_kernel(node_tmp);  
    node_tmp = autosa_tree_move_down_to_array(node_tmp, kernel->core);
    prefix = isl_schedule_node_get_prefix_schedule_relation(node_tmp);
    prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                              isl_union_pw_multi_aff_copy(kernel->contraction));
    if (group->local_array->array_type == AUTOSA_INT_ARRAY)
      group_access = remove_local_accesses_group_flow(kernel, cur_group, group_access, prefix, read);  
    isl_union_map_free(prefix);                                                                
  }
  if (drain_group) {
    struct autosa_array_ref_group *cur_group = group->attached_drain_group;
    group_access = isl_union_map_union(group_access,
                                       autosa_io_group_access_relation(cur_group, kernel, read, !read));
  }
  isl_schedule_node_free(node_tmp);

  group_domain = isl_union_map_domain(group_access);
  group_domain = isl_union_set_coalesce(group_domain);

  return group_domain;  
}

/* Compute the iteration domain used by the io_group and add the 
 * domain as a filter at the top of the schedule tree.
 * If io_group is one, consider io_group domain.
 * If drain_group is one, consider the attached drain group domain.
 */
static __isl_give isl_schedule_node *insert_io_group_access_domain_local_reduce(
  __isl_take isl_schedule_node *node, 
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  int read, int io_group, int drain_group)
{
  isl_union_set *group_domain;
  group_domain = compute_io_group_access_domain_local_reduce(node, group, kernel, read, io_group, drain_group);
  node = isl_schedule_node_insert_filter(node, group_domain);  
  return node;
}

/* Insert a filter node that filters the valid access domain of the current
 * io group. The "node" should point to the "kernel" mark, and will be returned 
 * at the "kernel" mark.
 */
__isl_give isl_schedule_node *insert_io_group_domain(
  __isl_take isl_schedule_node *node, 
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  struct autosa_gen *gen,
  int read)
{
  node = isl_schedule_node_child(node, 0); // context
  if (gen->options->autosa->local_reduce && group->attached_drain_group) 
    node = insert_io_group_access_domain_local_reduce(node, group, kernel, read, 0, 1);
  else
    node = insert_io_group_access_domain(node, group, kernel, read);
  node = autosa_tree_move_up_to_kernel(node);

  return node;
}

static __isl_give isl_union_set *compute_io_group_domain(
  __isl_keep isl_schedule_node *node, 
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  struct autosa_gen *gen,
  int read)
{
  isl_union_set *domain;
  node = autosa_tree_move_down_to_kernel(isl_schedule_node_copy(node));
  if (gen->options->autosa->local_reduce && group->attached_drain_group)
    domain = compute_io_group_access_domain_local_reduce(node, group, kernel, read, 1, 1);
  else
    domain = compute_io_group_access_domain(node, group, kernel, read);
  isl_schedule_node_free(node);

  return domain;
}

/* Compute the minimal group domain to filter the elements at the io_level "level.
 * The original group domain is first inserted at root.
 * Then, we compute the prefix schedule down to the io_level "level".
 * Next, we derive the range of the prefix schedule, and compute the 
 * reverse elements that are required for this range set.
 */
static __isl_give isl_union_set *compute_io_group_domain_at_level(
  __isl_keep isl_union_set *group_domain,
  __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  int level
){
  isl_union_map *prefix, *filter_prefix;
  isl_union_set *filter_range, *filter_domain;
  
  node = autosa_tree_move_down_to_io_mark(isl_schedule_node_copy(node), kernel->core, level);
  prefix = isl_schedule_node_get_prefix_schedule_relation(node);

  node = isl_schedule_node_insert_filter(node, isl_union_set_copy(group_domain));
  node = isl_schedule_node_child(node, 0);
  filter_prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  isl_schedule_node_free(node);
  filter_range = isl_union_map_range(filter_prefix);
  prefix = isl_union_map_reverse(prefix);
  filter_domain = isl_union_set_apply(filter_range, prefix);

  return filter_domain;
}

/* Extend the group domain so that the domain sets include elements that are
 * lexicographically less or equal to the IO band at the io_level "level".
 */
static __isl_give isl_union_set *extend_io_group_domain(
  __isl_take isl_union_set *group_domain,
  __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  int level
){
//#ifdef _DEBUG
//  DBGUSET(stdout, group_domain, isl_schedule_node_get_ctx(node));
//#endif
  isl_union_map *prefix;
  isl_set *group_range, *all_range;
  isl_map *ge;

  /* Get the all range */
  node = autosa_tree_move_down_to_io_mark(isl_schedule_node_copy(node), kernel->core, level);  
  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  all_range = isl_set_from_union_set(isl_union_map_range(isl_union_map_copy(prefix)));

  //node = isl_schedule_node_insert_filter(node, isl_union_set_copy(group_domain));
  //node = isl_schedule_node_child(node, 0);
  //prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  isl_schedule_node_free(node);
  group_range = isl_set_from_union_set(isl_union_set_apply(group_domain, isl_union_map_copy(prefix)));
//#ifdef _DEBUG
//  DBGSET(stdout, group_range, kernel->ctx);
//#endif
  ge = isl_map_lex_ge(isl_set_get_space(group_range));
  /* Set the dimensions except the last one as equal */
  for (int i = 0; i < isl_set_dim(group_range, isl_dim_set) - 1; i++) {
    ge = isl_map_equate(ge, isl_dim_in, i, isl_dim_out, i);
  }
  ge = isl_map_intersect_domain(ge, isl_set_copy(all_range));
  ge = isl_map_intersect_range(ge, all_range);
//#ifdef _DEBUG
//  DBGMAP(stdout, ge, kernel->ctx);
//#endif
  group_range = isl_set_apply(group_range, ge);
  group_range = isl_set_coalesce(group_range);
//#ifdef _DEBUG
//  DBGSET(stdout, group_range, kernel->ctx);
//#endif  
  prefix = isl_union_map_reverse(prefix);
  group_domain = isl_union_set_apply(isl_union_set_from_set(group_range), prefix);

  return group_domain;
} 

static __isl_give isl_schedule_node *insert_io_stmts_acc(
  __isl_take isl_schedule_node *node,
  int nxt_data_pack,
  __isl_take isl_printer *p,
  struct autosa_kernel *kernel, 
  struct autosa_array_ref_group *group,
  struct autosa_io_buffer *buf, /* Local buffer */
  int read, int is_buffer
)
{
  char *stmt_name;

  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_int(p, nxt_data_pack);
  stmt_name = isl_printer_get_str(p);
  isl_printer_free(p);

  int insert_hls_dep = is_buffer && !read && 
                       buf->n_lane != nxt_data_pack && 
                       kernel->options->autosa->insert_hls_dependence;

  node = add_io_copies_stmt_acc(kernel, group, node,
                                buf->tile, nxt_data_pack, read, stmt_name, read ? 1 : 0,
                                insert_hls_dep);

  return node;
}

static __isl_give isl_schedule_node *insert_io_stmts_tile(
    __isl_take isl_schedule_node *node,
    int nxt_data_pack,
    __isl_take isl_printer *p,
    struct autosa_kernel *kernel, 
    struct autosa_array_ref_group *group,
    //struct autosa_io_buffer *buf,
    struct autosa_io_buffer *local_buffer,      /* local buffer */
    struct autosa_io_buffer *copy_buffer,       /* buffer to be transferred */
    int read, int is_buffer,
    struct autosa_hw_module *module,
    int cut /* If to cut the sub tree */
)
{
  char *stmt_name;
  int coalesce_bound;

  p = isl_printer_print_str(p, ".");
  p = isl_printer_print_int(p, nxt_data_pack);  

  p = print_trans_stmt_coalesce(p, node, copy_buffer, &coalesce_bound);   
  module->coalesce_bound = coalesce_bound;
  
  stmt_name = isl_printer_get_str(p);
  isl_printer_free(p);

  int insert_hls_dep = coalesce_bound > 1 && 
                       copy_buffer->n_lane != nxt_data_pack && 
                       kernel->options->autosa->insert_hls_dependence;

  node = add_io_copies_stmt_tile(kernel, group, node,
                                 local_buffer? local_buffer->tile : NULL, copy_buffer->tile, 
                                 nxt_data_pack, read, stmt_name, read ? 1 : 0,
                                 is_buffer & 0,
                                 insert_hls_dep,
                                 module->is_serialized);

  if (cut) {
    node = isl_schedule_node_cut(node);
    /* Insert empty filter. */
    isl_union_set *empty_filter = isl_union_set_from_set(isl_set_empty(
          isl_set_get_space(kernel->context)));
    node = isl_schedule_node_insert_filter(node, empty_filter);
  }  

  return node;
}

static __isl_give isl_schedule_node *insert_filter_trans_stmts(
  __isl_take isl_schedule_node *node,
  isl_id_list *io_ids,
  int io_id_level,
  int io_level,
  int read,
  struct autosa_io_buffer *buf,
  struct autosa_hw_module *module,
  struct autosa_kernel *kernel,
  struct autosa_gen *gen,
  int boundary, int is_lower,
  int is_buffer,
  char *fifo_suffix,
  struct autosa_array_ref_group *group,
  __isl_keep isl_union_set *group_core
)
{
  isl_id_list *ids;
  isl_union_set *eq_filter, *neq_filter;
  isl_ctx *ctx;
  isl_printer *p;
  int upper_io_level;

  ctx = isl_schedule_node_get_ctx(node);
  if (io_id_level < 0) {
    /* This is the highest-level module that also connects to the DRAM.
     * Filter node is not required, since all data belongs to this module.
     */
    if (boundary == 0) {
      return isl_schedule_node_free(node);
    } else {
      node = autosa_tree_move_down_to_io_mark(node, group_core, buf->level);
      node = isl_schedule_node_child(node, 0);
      goto INSERT_STMT;
    }
  }
  
  node = autosa_tree_move_down_to_io_mark(node, group_core, io_level);
  node = isl_schedule_node_parent(node);
  ids = isl_id_list_from_id(isl_id_list_get_id(io_ids, io_id_level));
  eq_filter = set_schedule_eq(node, ids);  
  isl_id_list_free(ids);

  upper_io_level = io_level + 1;
  node = autosa_tree_move_down_to_io_mark(node, group_core, io_level);
  node = isl_schedule_node_child(node, 0);

//#ifdef _DEBUG
//  //DBGUSET(stdout, eq_filter, ctx);
//  DBGSCHDNODE(stdout, node, ctx);
//#endif

  node = isl_schedule_node_order_before(node, eq_filter); // point to the second tree.  

  /* Pass the data not filtered */  
  if (boundary) {
    isl_union_set *empty_filter = isl_union_set_from_set(isl_set_empty(isl_set_get_space(kernel->context)));
    node = isl_schedule_node_cut(node);
    node = isl_schedule_node_insert_filter(node, empty_filter);
  } else {
    if (io_level != buf->level) {
      node = autosa_tree_move_down_to_io_mark(node, group_core, buf->level);
      node = isl_schedule_node_child(node, 0);
    }
    p = isl_printer_to_str(ctx);
    p = print_io_trans_stmt_prefix(
          p, read, module->to_mem, gen->options->autosa->host_serialize, boundary, 0, NULL,
          0, 0, 0, fifo_suffix, buf->n_lane);    
    if (!buf->tile) {
      node = insert_io_stmts_acc(node, buf->n_lane, p, kernel, group, buf, read, is_buffer);   
    } else {
      node = insert_io_stmts_tile(node, buf->n_lane, p, kernel, group, buf, buf, read, is_buffer, module, 1);    
    }
  }

  /* Keep the data filtered */
  node = autosa_tree_move_up_to_kernel(node);
  node = autosa_tree_move_down_to_io_mark(node, group_core, io_level);
  node = isl_schedule_node_child(node, 0); // seqeuence
  node = isl_schedule_node_child(node, 0); // filter  
  node = isl_schedule_node_child(node, 0); // filter  

  //node = autosa_tree_move_down_to_io_mark(node, group_core, upper_io_level);
  //node = isl_schedule_node_child(node, 0); // filter
  //node = isl_schedule_node_child(node, 0); // band
  //node = isl_schedule_node_child(node, 0); // sequence
  //node = isl_schedule_node_child(node, 0); // filter
  //node = isl_schedule_node_child(node, 0); // filter

  if (io_level != buf->level) {
    node = autosa_tree_move_down_to_io_mark(node, group_core, buf->level);
    node = isl_schedule_node_child(node, 0);
  }

INSERT_STMT:  
  //node = autosa_tree_move_down_to_io_mark(node, group_core, buf->level);
  //node = isl_schedule_node_child(node, 0);
  p = isl_printer_to_str(ctx);
  p = print_io_trans_stmt_prefix(
        p, read, module->to_mem, gen->options->autosa->host_serialize, boundary, 0, NULL,
        !read && is_lower ? 1 : 0, read && is_lower? 1 : 0, is_buffer, fifo_suffix, buf->n_lane);

  if (!buf->tile)  {
    node = insert_io_stmts_acc(node, buf->n_lane, p, kernel, group, buf, read, is_buffer);   
  } else {
    node = insert_io_stmts_tile(node, buf->n_lane, p, kernel, group, buf, buf, read, is_buffer, module, 1);    
  }

  return node;
}

/* The node points to the "kernel" mark.
 */
static int get_local_reduce_sched_depth(
  __isl_take isl_schedule_node *node,
  struct autosa_kernel *kernel)
{
  node = autosa_tree_move_down_to_array(node, kernel->core);
  if (kernel->array_part_w > 0) {
    int pos = 0;
    int n;
    node = isl_schedule_node_parent(node);
    n = isl_schedule_node_band_n_member(node);
    for (pos = n - 1; pos >= 0; pos--)
    {
      if (isl_schedule_node_band_member_get_coincident(node, pos))
        break;
    }
//#ifdef _DEBUG
//    DBGVAR(std::cout, pos)
//#endif
    if (pos == n - 1) {
      node = isl_schedule_node_child(node, 0);
    } else {
      node = isl_schedule_node_band_split(node, pos + 1);
      node = isl_schedule_node_child(node, 0);      
    }
  }

  int depth = isl_schedule_node_get_schedule_depth(node);
  isl_schedule_node_free(node);
//#ifdef _DEBUG
//  DBGVAR(std::cout, depth)
//#endif

  return depth;
}

/* Generate the inter_trans module for the I/O group.
 * We will add data transfer statements into the schedule tree, 
 * filters that restrain the space loops to the current module,
 * and add the module and function type mark above the tree.
 */
static __isl_give isl_schedule *generate_io_module_inter_trans(
  __isl_keep isl_schedule *sched, struct autosa_hw_module *module,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel, struct autosa_gen *gen,
  int io_level, int space_dim, int read, int boundary)
{
  isl_schedule *new_sched;
  isl_ctx *ctx;
  isl_printer *p;  
  int n_io_ids;
  isl_id_list *io_ids;
  isl_id *id;
  char *fifo_suffix, *buf_suffix;
  isl_union_set *empty_filter = NULL;  
  char *stmt_name;
  struct autosa_io_buffer *buf = NULL;  
  isl_schedule_node *node;
  int upper_io_level = io_level + 1;
  int is_filter = 1;
  int is_buffer = 1;
  int i;
  isl_union_set *group_core = NULL;

  if (io_level > space_dim && boundary == 0) {
    return NULL;
  }

//#ifdef _DEBUG
//  std::cout << module->name << "_inter_trans" << std::endl;
//#endif

  new_sched = isl_schedule_dup(sched);
  node = isl_schedule_get_root(new_sched);
  isl_schedule_free(new_sched);
  ctx = isl_schedule_node_get_ctx(node);

  /* Compute the union of domains of all the array references in the group. */
  node = autosa_tree_move_down_to_kernel(node);
  node = isl_schedule_node_child(node, 0); // context
  node = isl_schedule_node_child(node, 0);
  if (gen->options->autosa->local_reduce && group->attached_drain_group)
    node = insert_io_group_access_domain_local_reduce(node, group, kernel, read, 0, 1);
  else
    node = insert_io_group_access_domain(node, group, kernel, read);
  node = isl_schedule_node_child(node, 0);
  group_core = isl_union_set_universe(isl_schedule_node_get_domain(node));
  node = autosa_tree_move_up_to_kernel(node);

  /* Add the filters. */
  n_io_ids = space_dim - io_level + 1;
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");
  n_io_ids = 0;  
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = add_io_ids_filter(node, io_ids, io_level, space_dim - io_level + 1, is_filter, 0, read);
  node = autosa_tree_move_up_to_kernel(node);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  /* Locate the buffer. */
  for (i = io_level; i >= 1; i--)
  {
    buf = group->io_buffers[i - 1];
    if (buf->tile != NULL)
      break;
  }
  if (is_buffer)
  {
    if (i != io_level)
    {
      /* IO buffer is optimized out. */
      is_buffer = 0;
    }
  }

  init_suffix(module, group, &fifo_suffix, &buf_suffix);
  node = insert_filter_trans_stmts(node, io_ids, space_dim - io_level, io_level, read,
      buf, module, kernel, gen, boundary, 0, is_buffer, fifo_suffix, group, group_core);

  free(fifo_suffix);
  free(buf_suffix);      
  isl_id_list_free(io_ids);
  if (!node) {
    isl_union_set_free(group_core);
    return NULL;  
  }

  module->data_pack_inter = buf->n_lane;
  /* Insert the "io_module.inter_trans" function mark. */
  node = autosa_tree_move_up_to_kernel(node);

  if (gen->options->autosa->local_reduce && group->attached_drain_group) {
    node = autosa_tree_move_down_to_depth(
              node, 
              get_local_reduce_sched_depth(isl_schedule_node_copy(node), kernel), 
              kernel->core);    
  } else {
    if (io_level > space_dim) {
      node = autosa_tree_move_down_to_array(node, kernel->core);
      node = isl_schedule_node_child(node, 0);  
    } else {
      //node = autosa_tree_move_down_to_io_mark(node, group_core, upper_io_level);
      node = autosa_tree_move_down_to_io_mark(node, group_core, io_level);
      node = isl_schedule_node_parent(node);
      node = isl_schedule_node_parent(node);
    }    
  }
  
  id = isl_id_alloc(ctx, "io_module.inter_trans", NULL);
  node = isl_schedule_node_insert_mark(node, id);

  /* Add the module mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  new_sched = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);  
  isl_union_set_free(group_core);

  return new_sched;
}

/* The "node" points to the kernel mark. 
 * This function should be called before inserting module ids into the schedule.
 */
static __isl_give isl_schedule_node *insert_io_group_guard(
  __isl_take isl_schedule_node *node, 
  struct autosa_gen *gen,
  struct autosa_kernel *kernel,
  int n_io_ids)
{
  isl_union_set *domain;
  isl_set *guard;
  isl_schedule_node *node_tmp;
  isl_id_list *io_ids;
  
  node_tmp = isl_schedule_node_copy(node);
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");
  node_tmp = add_io_ids_filter(node_tmp, io_ids, 1, n_io_ids, 0, 0, 0);  
  domain = isl_schedule_node_get_domain(node_tmp);
  guard = isl_union_set_params(domain);
  guard = isl_set_from_params(guard);
  isl_schedule_node_free(node_tmp);
  isl_id_list_free(io_ids);
  
//#ifdef _DEBUG
//  DBGSET(stdout, guard, isl_set_get_ctx(guard));
//#endif

  //node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0); // context;
  node = isl_schedule_node_child(node, 0); // filter;
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_guard(node, guard);
  node = autosa_tree_move_up_to_kernel(node);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  return node;
}

static __isl_give isl_set *get_io_group_guard(
  __isl_keep isl_schedule_node *node,
  struct autosa_gen *gen,
  struct autosa_kernel *kernel,
  int n_io_ids)
{
  isl_union_set *domain;
  isl_set *guard;
  isl_schedule_node *node_tmp;
  isl_id_list *io_ids;
  int depth;
  
  node_tmp = isl_schedule_node_copy(node);
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");  
  node_tmp = add_io_ids_filter(node_tmp, io_ids, 1, n_io_ids, 0, 0, 0);  
  isl_id_list_free(io_ids);

  domain = isl_schedule_node_get_domain(node_tmp);
  guard = isl_union_set_params(domain);
  guard = isl_set_from_params(guard);
  isl_schedule_node_free(node_tmp);
  
  return guard;
}

/* Generate the intra_trans module for the I/O group.
 * We will add data transfer statements into the schedule tree that 
 * transfer data to/from the lower-level modules,
 * filters that restrain the space loops to the current module,
 * and add the module and function type mark above the tree.
 */
static __isl_give isl_schedule *generate_io_module_intra_trans(
  __isl_keep isl_schedule *sched, struct autosa_hw_module *module,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel, struct autosa_gen *gen,
  int io_level, int space_dim, int read, int is_buffer)
{
  isl_ctx *ctx;
  isl_printer *p;  
  int n_io_ids;
  isl_id_list *io_ids;  
  isl_id *id;    
  char *fifo_suffix, *buf_suffix;
  isl_union_set *empty_filter = NULL;    
  char *stmt_name;
  struct autosa_io_buffer *buf = NULL;  
  isl_schedule *new_sched;
  isl_schedule_node *node;  
  int i;
  isl_set *guard;
  isl_schedule_node *node_tmp;
  isl_union_set *group_core = NULL;
  isl_union_set *group_domain;

//#ifdef _DEBUG
//  std::cout << module->name << "_intra_trans" << std::endl;
//#endif

  new_sched = isl_schedule_dup(sched);
  node = isl_schedule_get_root(new_sched);  
  node = autosa_tree_move_down_to_kernel(node);
  isl_schedule_free(new_sched);
  ctx = isl_schedule_node_get_ctx(node);
  n_io_ids = space_dim - io_level + 1;
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");  
  int upper_io_level = io_level + 1;

//#ifdef _DEBUG
//  //DBGSCHDNODE(stdout, node, gen->ctx);
//#endif

  ///* Insert the group domain. */   
  node = isl_schedule_node_child(node, 0); // context
  node = isl_schedule_node_child(node, 0);
  if (gen->options->autosa->local_reduce && group->attached_drain_group)
    node = insert_io_group_access_domain_local_reduce(node, group, kernel, read, 1, 1);
  else
    node = insert_io_group_access_domain(node, group, kernel, read);  
  node = isl_schedule_node_child(node, 0);
  group_core = isl_union_set_universe(isl_schedule_node_get_domain(node)); 
  node = autosa_tree_move_up_to_kernel(node);
  //group_domain = compute_io_group_domain(node, group, kernel, gen, read);
  //group_core = isl_union_set_universe(isl_union_set_copy(group_domain));

  /* Add the filters. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = add_io_ids_filter(node, io_ids, io_level, space_dim - io_level + 1, 0, module->to_pe, read);
  node = autosa_tree_move_up_to_kernel(node);  

  /* Add the data transfer statements. */
  init_suffix(module, group, &fifo_suffix, &buf_suffix);

  /* Locate the current buffer. */
  for (i = io_level; i >= 1; i--)
  {
    buf = group->io_buffers[i - 1];
    if (buf->tile != NULL)
      break;
  }
  if (is_buffer)
  {
    if (i != io_level)
    {
      /* IO buffer is optimized out. */
      is_buffer = 0;
    }
  }

  /* Insert the extra transfer statement. */
  p = isl_printer_to_str(ctx);
  p = print_io_trans_stmt_prefix(p, !read, 0, 0, 0, 
                                 gen->options->autosa->local_reduce && group->attached_drain_group,
                                 gen->options->autosa->reduce_op,
                                 !read, read, is_buffer, fifo_suffix, buf->n_lane);

  /* Locate the next buffer after the current buffer. */
  int cur_level = buf->level;
  struct autosa_io_buffer *cur_buf = buf;
  for (int i = cur_level - 1; i >= 1; i--)
  {
    buf = group->io_buffers[i - 1];
    if (buf->tile != NULL)
      break;
  }

  if (cur_level == 1 || !buf->tile)
  {
//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif
    node = insert_io_stmts_acc(node, group->n_lane, p, kernel, group, cur_buf, read, is_buffer);
    module->data_pack_intra = group->n_lane;                                  
  }
  else
  {
    /* Move the schedule node to the level of the next buffer. */
    node = autosa_tree_move_down_to_io_mark(node, group_core, buf->level);
    node = isl_schedule_node_child(node, 0);    
    node = insert_io_stmts_tile(
                node, buf->n_lane, p, kernel, group, 
                cur_buf, buf, !read, is_buffer, module, 1);
    module->data_pack_intra = buf->n_lane;    
  }

  free(fifo_suffix);
  free(buf_suffix);

  /* Insert the function mark. */  
  node = autosa_tree_move_up_to_kernel(node);
  if (gen->options->autosa->local_reduce && group->attached_drain_group) {
    node = autosa_tree_move_down_to_depth(
              node, 
              get_local_reduce_sched_depth(isl_schedule_node_copy(node), kernel), 
              kernel->core);    
  } else {
    if (io_level > space_dim) {
      node = autosa_tree_move_down_to_array(node, kernel->core);      
      node = isl_schedule_node_child(node, 0);  
    } else {
      //node = autosa_tree_move_down_to_io_mark(node, kernel->core, upper_io_level);        
      node = autosa_tree_move_down_to_io_mark(node, group_core, io_level);
      node = isl_schedule_node_parent(node);
      node = isl_schedule_node_parent(node);
    }    
  }

//#ifdef _DEBUG
//  if (!strcmp(module->name, "L_drain_IO_L1_out")) {
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//  }
//#endif

  id = isl_id_alloc(ctx, "io_module.intra_trans", NULL);  
  if (kernel->array_part_w == 0 && isl_schedule_node_get_schedule_depth(node) < group->io_level) {
    node = autosa_tree_move_up_to_kernel(node);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_insert_mark(node, id);  
  } else {
    node = isl_schedule_node_insert_mark(node, id);  
  }  

  /* Add the module mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  /* Make the node atomic */
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  node = autosa_atomic_ancestors(node);  
  new_sched = isl_schedule_node_get_schedule(node);
//#ifdef _DEBUG
//  if (!strcmp(module->name, "L_drain_IO_L1_out")) {
//    print_code(gen, isl_schedule_copy(new_sched), "L_drain_IO_L1_out.c");
//  }
//#endif

  isl_schedule_node_free(node);
  isl_id_list_free(io_ids);
  isl_union_set_free(group_core);

  return new_sched;
}

/* Create the local buffer variable for the "group".
 * Specifically, if "tile" is NULL, a register is created.
 * Otherwise, a local array is created. 
 * We will also update the last dimension of the array based on the 
 * data packing factor "n_lane".
 */
static void create_io_module_var(isl_ctx *ctx,
                                 struct autosa_array_ref_group *group,
                                 struct autosa_array_tile *tile, struct autosa_kernel_var *var, int n_lane)
{
  isl_printer *p;

  var->array = group->array;
  var->type = autosa_array_ref_group_type(group);
  var->n_lane = n_lane;
  var->n_part = 1;

  p = isl_printer_to_str(ctx);
  p = autosa_array_ref_group_print_name(group, p);
  var->name = isl_printer_get_str(p);
  isl_printer_free(p);

  if (tile == NULL)
  {
    /* Create a register. */
    var->size = isl_vec_alloc(ctx, 1);
    var->size = isl_vec_set_element_si(var->size, 0, 1);
  }
  else
  {
    var->size = isl_vec_alloc(ctx, group->array->n_index);
    for (int i = 0; i < group->array->n_index; ++i)
    {
      isl_val *size;

      size = isl_val_copy(tile->bound[i].size);
      if (n_lane > 1 && i == group->array->n_index - 1)
      {
        size = isl_val_div(size, isl_val_int_from_si(ctx, n_lane));
      }
      var->size = isl_vec_set_element_val(var->size, i, size);
    }
  }
}

/* Create the local buffers inside the I/O modules. */
static isl_stat create_io_module_vars(
    struct autosa_hw_module *module, struct autosa_kernel *kernel,
    struct autosa_array_tile *tile, int init_required)
{
  module->var = isl_calloc_array(kernel->ctx, struct autosa_kernel_var, 1);
  if (!module->var)
    return isl_stat_error;
  module->n_var = 1;
  module->var[0].init_required = init_required;

  create_io_module_var(kernel->ctx, module->io_groups[0],
                       tile, &module->var[0], module->data_pack_inter);

  return isl_stat_ok;
}

/* Generate the io_module for the outer loops that contain the 
 * inter_trans and intra_trans modules.
 */
static __isl_give isl_schedule *generate_io_module_outer(
    __isl_keep isl_schedule *sched, struct autosa_hw_module *module,
    struct autosa_array_ref_group *group,
    struct autosa_kernel *kernel, struct autosa_gen *gen,
    int io_level, int space_dim, int read, int boundary)
{
  isl_ctx *ctx;
  int n_io_ids;
  isl_id_list *io_ids;
  isl_id *id;
  isl_union_set *empty_filter = NULL;
  const char *stmt_name1, *stmt_name2, *stmt_name3, *stmt_name4, *stmt_name5;  
  isl_schedule_node *node, *graft1, *graft2, *graft3, *graft4, *graft5;
  isl_schedule *new_sched;
  int upper_io_level;
  isl_space *space;
  isl_union_set *domain;
  struct autosa_io_buffer *buf;
  isl_union_set *group_core = NULL;

  if (io_level > space_dim && boundary == 0) {
    return NULL;
  }

//#ifdef _DEBUG
//  std::cout << module->name << "_outer" << std::endl;
//#endif

  new_sched = isl_schedule_dup(sched);
  node = isl_schedule_get_root(new_sched);
  isl_schedule_free(new_sched);
  ctx = isl_schedule_node_get_ctx(node);
  n_io_ids = space_dim - io_level + 1;

  /* Compute the union of domains of all the array references in the group. */
  node = autosa_tree_move_down_to_kernel(node);
  node = isl_schedule_node_child(node, 0); // context
  node = isl_schedule_node_child(node, 0);
  if (gen->options->autosa->local_reduce && group->attached_drain_group)
    node = insert_io_group_access_domain_local_reduce(node, group, kernel, read, 1, 1);
  else
    node = insert_io_group_access_domain(node, group, kernel, read);
  node = isl_schedule_node_child(node, 0);
  group_core = isl_union_set_universe(isl_schedule_node_get_domain(node));
  node = autosa_tree_move_up_to_kernel(node);

  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");
  n_io_ids = 0;
  
  if (io_level > space_dim && boundary == 1) {    
    goto OUTER_INSERT_STMT;
  }

  upper_io_level = io_level + 1;
  /* Add the filters. */
  n_io_ids = 0;
  node = autosa_tree_move_down_to_array(node, kernel->core);
  while (!isl_schedule_node_is_io_mark(node, upper_io_level))
  {
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
      isl_id *id;
      isl_id_list *ids;
      isl_union_set *uset;

      ids = isl_id_list_from_id(isl_id_list_get_id(io_ids, n_io_ids));
      uset = set_schedule_eq(node, ids);
      n_io_ids++;
      node = isl_schedule_node_insert_filter(node, uset);
      isl_id_list_free(ids);
      node = isl_schedule_node_child(node, 0);
    }
    node = isl_schedule_node_child(node, 0);
  }

  node = autosa_tree_move_up_to_kernel(node);
OUTER_INSERT_STMT:  
  if (gen->options->autosa->local_reduce && group->attached_drain_group) {
    node = autosa_tree_move_down_to_depth(
              node, 
              get_local_reduce_sched_depth(isl_schedule_node_copy(node), kernel), 
              kernel->core);        
  } else {
    if (io_level > space_dim && boundary == 1) {
      node = autosa_tree_move_down_to_array(node, kernel->core);
      node = isl_schedule_node_child(node, 0);              
    } else {
      //node = autosa_tree_move_down_to_io_mark(node, kernel->core, upper_io_level);
      node = autosa_tree_move_down_to_io_mark(node, group_core, io_level);
      node = isl_schedule_node_parent(node);
      //node = isl_schedule_node_parent(node);
    }    
  }
  isl_union_set_free(group_core);

  /* Add the inter_trans and intra_trans function calls. */
  stmt_name1 = boundary == 0 ? "io_module.inter_trans.0" : "io_module.inter_trans.1";
  stmt_name2 = "io_module.intra_trans";
  stmt_name3 = boundary == 0 ? "io_module.inter_intra.0" : "io_module.inter_intra.1";
  stmt_name4 = boundary == 0 ? "io_module.intra_inter.0" : "io_module.intra_inter.1";
  stmt_name5 = "io_module.state_handle";
  
  node = isl_schedule_node_cut(node);

  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name1);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft1 = isl_schedule_node_from_domain(domain);

  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name2);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft2 = isl_schedule_node_from_domain(domain);

  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name3);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft3 = isl_schedule_node_from_domain(domain);

  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name4);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft4 = isl_schedule_node_from_domain(domain);

  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name5);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft5 = isl_schedule_node_from_domain(domain);

  if (read)
  {
    node = isl_schedule_node_graft_before(node, isl_schedule_node_copy(graft3));
  }
  else
  {
    node = isl_schedule_node_graft_before(node, isl_schedule_node_copy(graft4));
  }
  if (module->double_buffer)
  {
    /* Add misc statements for saving and switching states. */
    node = isl_schedule_node_graft_before(node, isl_schedule_node_copy(graft5));
  }
  node = isl_schedule_node_cut(node);
  /* Insert an empty filter */
  empty_filter = isl_union_set_from_set(isl_set_empty(
      isl_set_get_space(kernel->context)));
  node = isl_schedule_node_insert_filter(node, empty_filter);

  if (module->double_buffer)
  {
    /* Add the last function call. */
    node = autosa_tree_move_up_to_kernel(node);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_child(node, 0);
    if (read)
      node = isl_schedule_node_graft_after(node, isl_schedule_node_copy(graft2));
    else
      node = isl_schedule_node_graft_after(node, isl_schedule_node_copy(graft1));
  }
  isl_schedule_node_free(graft1);
  isl_schedule_node_free(graft2);
  isl_schedule_node_free(graft3);
  isl_schedule_node_free(graft4);
  isl_schedule_node_free(graft5);

  /* Add the module mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  new_sched = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  /* Update module information. */
  if (!boundary || (io_level > space_dim && boundary == 1))
  {
    module->type = (group->group_type == AUTOSA_DRAIN_GROUP) ? DRAIN_MODULE : IO_MODULE;
    module->level = io_level;
    module->n_io_group++;
    module->io_groups = (struct autosa_array_ref_group **)realloc(module->io_groups,
                                                                  module->n_io_group * sizeof(struct autosa_array_ref_group *));
    module->io_groups[module->n_io_group - 1] = group;
    module->inst_ids = io_ids;
    module->kernel = kernel;
    module->is_buffer = 1;
    module->is_filter = 1;
    /* Create IO module variables. */
    for (int i = io_level; i >= 1; i--)
    {
      buf = group->io_buffers[i - 1];
      if (buf->tile != NULL)
        break;
    }
    if (gen->options->autosa->local_reduce && group->attached_drain_group) {
      create_io_module_vars(module, kernel, buf->tile, 1);
    } else {
      create_io_module_vars(module, kernel, buf->tile, 0);
    }
  }
  else
  {
    isl_id_list_free(io_ids);
  }

  return new_sched;
}

/* We will generate five seperate schedules for this type of I/O module.
 * Schedule 1: Outer loops contains two marks for inter_transfer 
 *             and intra_transfer modules
 * Schedule 2: Inter_transfer function
 * Schedule 3: Intra_transfer function
 * Schedule 4: The boundary module for outer loops that is the last module
 *             in the chain.
 * Schedule 5: The boundary module for inter_transfer that is the last module
 *             in the chain.
 */
static __isl_give struct autosa_hw_module *generate_filter_buffer_io_module(
    __isl_take struct autosa_hw_module *module,
    __isl_keep isl_schedule_node *node,
    struct autosa_array_ref_group *group, struct autosa_kernel *kernel,
    struct autosa_gen *gen,
    int io_level, int space_dim, int is_filter, int is_buffer, int read)
{
  isl_schedule *sched;
  isl_schedule *sched1, *sched2, *sched3;
  isl_schedule *boundary_sched2, *boundary_sched1;

  sched = isl_schedule_node_get_schedule(node);

  /* We only enable double buffer for external array. 
   * TODO: Offer options to enable the selection of which arrays to be double buffered.
   */
  if (gen->options->autosa->double_buffer && kernel->array_part_w > 0)
  {
    if (group->local_array->array_type == AUTOSA_EXT_ARRAY && module->in) {
      module->double_buffer = 1;
    } else {
      if (gen->options->autosa->local_reduce)
        module->double_buffer = 1;
      else
        module->double_buffer = 0;    
    }
  }
  else
  {
    module->double_buffer = 0;
  }

  /* Inter transfer function. */
  sched2 = generate_io_module_inter_trans(sched, module, group, kernel, gen,
                                          io_level, space_dim, read, 0);
  if (is_filter)
  {
    /* Add the boundary module schedule. */
    module->boundary = 1;
    boundary_sched2 = generate_io_module_inter_trans(sched, module, group,
                                                     kernel, gen, io_level, space_dim, read, 1);
  }
  /* Intra transfer function. */
  sched3 = generate_io_module_intra_trans(sched, module, group, kernel, gen,
                                          io_level, space_dim, read, is_buffer);
  /* Outer loops. */
  sched1 = generate_io_module_outer(sched, module, group, kernel, gen,
                                    io_level, space_dim, read, 0);
  if (is_filter)
  {
    /* Add the boundary module schedule. */
    module->boundary = 1;
    boundary_sched1 = generate_io_module_outer(sched, module, group, kernel, gen,
                                               io_level, space_dim, read, 1);
  }

  isl_schedule_free(sched);

  module->sched = NULL;
  module->outer_sched = sched1;
  module->inter_sched = sched2;
  module->intra_sched = sched3;
  if (module->boundary)
  {
    module->boundary_outer_sched = boundary_sched1;
    module->boundary_inter_sched = boundary_sched2;
  }

  return module;
}

/* Internal struct for add_drain_merge_stmt_acc_single. */
struct drain_merge_stmt_acc_data
{
  struct autosa_kernel *kernel;
  struct autosa_array_ref_group *group;
  struct autosa_stmt_access *ref;
};

static __isl_give isl_multi_aff *autosa_create_drain_merge_stmt(
    isl_ctx *ctx,
    struct autosa_array_ref_group *io_group,
    isl_schedule_node *node,
    char *stmt_name)
{
  isl_space *space;
  int depth;
  char buf[100];
  isl_id *id;

  depth = isl_schedule_node_get_schedule_depth(node);
  space = isl_space_copy(io_group->array->space);
  space = isl_space_from_range(space);
  space = isl_space_add_dims(space, isl_dim_in, depth);
  space = isl_space_wrap(space);
  space = isl_space_map_from_set(space);

  sprintf(buf, "%s", stmt_name);

  id = isl_id_alloc(ctx, buf, NULL);
  space = isl_space_set_tuple_id(space, isl_dim_in, id);

  return isl_multi_aff_identity(space);
}

static __isl_give isl_schedule_node *add_drain_merge_stmt_acc_single(
    __isl_take isl_schedule_node *node, void *user)
{
  struct drain_merge_stmt_acc_data *data =
      (struct drain_merge_stmt_acc_data *)(user);
  struct autosa_array_ref_group *group = data->group;
  struct autosa_kernel *kernel = data->kernel;
  struct autosa_stmt_access *ref = data->ref;
  struct autosa_array_tile *tile;
  isl_union_set *uset, *empty_filter, *domain;
  isl_set *set;
  isl_space *space;
  isl_id *id, *id2;
  isl_ctx *ctx;
  isl_union_map *access;
  int empty;
  isl_printer *p_str;
  char *stmt_name;
  isl_multi_aff *from_access, *ma;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  isl_schedule_node *graft;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return node;

  /* Examine if the statement contains the access. */
  uset = isl_schedule_node_get_domain(node);
  set = isl_set_from_union_set(isl_union_set_copy(uset));
  space = isl_set_get_space(set);
  isl_set_free(set);
  id = isl_space_get_tuple_id(space, isl_dim_set);
  isl_space_free(space);
  space = isl_map_get_space(ref->access);
  id2 = isl_space_get_tuple_id(space, isl_dim_in);
  empty_filter = isl_union_set_empty(isl_union_set_get_space(uset));
  isl_union_set_free(uset);
  isl_space_free(space);

  if (id != id2)
  {
    isl_id_free(id);
    isl_id_free(id2);
    node = isl_schedule_node_insert_filter(node, empty_filter);
    return node;
  }
  isl_id_free(id);
  isl_id_free(id2);
  ctx = isl_schedule_node_get_ctx(node);

  access = io_comm_access_ref(kernel, node, group, ref, 0);
  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    isl_union_set_free(empty_filter);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return node;
  }

  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "drain_merge.");
  p_str = isl_printer_print_str(p_str, group->local_array->array->name);
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  from_access = autosa_create_drain_merge_stmt(ctx, group, node, stmt_name);
  free(stmt_name);

  /* Create a register tiling. */
  tile = create_register_tiling(node, group, ref);
  ma = isl_multi_aff_copy(tile->tiling);
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);

  domain = isl_union_map_range(access);
  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  access = isl_union_set_wrapped_domain_map(domain);
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

  graft = isl_schedule_node_from_extension(access);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);
  node = isl_schedule_node_insert_filter(node, empty_filter);
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_parent(node);

  autosa_array_tile_free(tile);

  return node;
}

static __isl_give isl_schedule_node *add_drain_merge_stmt_acc(
    __isl_take isl_schedule_node *node, struct autosa_array_ref_group *group,
    struct autosa_kernel *kernel)
{
  struct drain_merge_stmt_acc_data data = {kernel, group, NULL};
  for (int i = 0; i < group->n_ref; i++)
  {
    data.ref = group->refs[i];
    node = isl_schedule_node_map_descendant_bottom_up(
        node, &add_drain_merge_stmt_acc_single, &data);
  }
  return node;
}

/* This function generats code that merge all drained values from the drain group.
 */
static __isl_give struct autosa_drain_merge_func *generate_drain_merge_func(
    struct autosa_array_ref_group *group, struct autosa_kernel *kernel,
    struct autosa_gen *gen)
{
  isl_ctx *ctx;
  isl_schedule_node *node;
  int io_level;
  int space_dim;
  int n_io_ids;
  isl_id_list *io_ids = NULL;
  isl_union_map *group_access;
  isl_union_set *group_domain;
  isl_schedule *sched;
  isl_id *id;
  struct autosa_drain_merge_func *func = NULL;

  ctx = gen->ctx;
  node = isl_schedule_get_root(group->io_schedule);
  io_level = group->io_level;
  space_dim = group->space_dim;
  n_io_ids = space_dim - io_level + 1;
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");

  /* Add the filters. */
  n_io_ids = 0;
  node = autosa_tree_move_down_to_array(node, kernel->core);
  while (!isl_schedule_node_is_io_mark(node, io_level))
  {
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
      isl_id *id;
      isl_id_list *ids;
      isl_union_set *uset;

      ids = isl_id_list_from_id(isl_id_list_get_id(io_ids, n_io_ids));
      uset = set_schedule_eq(node, ids);
      n_io_ids++;
      node = isl_schedule_node_insert_filter(node, uset);
      isl_id_list_free(ids);
      node = isl_schedule_node_child(node, 0);
    }
    node = isl_schedule_node_child(node, 0);
  }
  node = autosa_tree_move_up_to_kernel(node);

  /* Add the data transfer statements. */
  node = autosa_tree_move_down_to_io_mark(node, kernel->core, io_level);
  node = add_drain_merge_stmt_acc(node, group, kernel);

  /* Compute the union of domains of all the array references in the group. */
  group_access = isl_union_map_empty(isl_map_get_space(group->access));
  for (int i = 0; i < group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = group->refs[i];
    group_access = isl_union_map_union(group_access,
                                       autosa_drain_group_ref_access_relation(group, ref, 0, 1,
                                                                              kernel->expanded_domain));
  }
  group_domain = isl_union_map_domain(group_access);
  group_domain = isl_union_set_coalesce(group_domain);
  /* Add the group domain as the filter. */
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0); // context
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_filter(node, group_domain);

  /* Add the func mark. */
  func = autosa_drain_merge_func_alloc(gen);
  id = isl_id_alloc(ctx, "drain_merge", func);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  sched = isl_schedule_node_get_schedule(node);
  func->sched = sched;
  func->group = group;
  func->kernel = kernel;
  func->inst_ids = io_ids;

  isl_schedule_node_free(node);

  return func;
}

struct add_serialize_stmt_acc_data
{
  struct autosa_array_ref_group *group;
  struct autosa_stmt_access *ref;
  struct autosa_kernel *kernel;
  struct autosa_array_tile *local_tile;
  char *stmt_name;
  int read;
};

static __isl_give isl_schedule_node *add_serialize_stmt_acc_single(
    __isl_take isl_schedule_node *node, void *user)
{
  struct add_serialize_stmt_acc_data *data =
      (struct add_serialize_stmt_acc_data *)user;
  struct autosa_array_ref_group *group = data->group;
  struct autosa_stmt_access *ref = data->ref;
  struct autosa_array_tile *tile;
  isl_union_set *uset, *empty_filter, *domain;
  isl_set *set;
  isl_space *space;
  isl_id *id, *id2;
  isl_ctx *ctx;
  isl_union_map *access;
  int empty;
  isl_multi_aff *from_access;
  isl_multi_aff *ma;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  isl_schedule_node *graft;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return node;

  /* Examine if the statement contains the access. */
  uset = isl_schedule_node_get_domain(node);
  set = isl_set_from_union_set(isl_union_set_copy(uset));
  space = isl_set_get_space(set);
  isl_set_free(set);
  id = isl_space_get_tuple_id(space, isl_dim_set);
  isl_space_free(space);
  space = isl_map_get_space(ref->access);
  id2 = isl_space_get_tuple_id(space, isl_dim_in);
  empty_filter = isl_union_set_empty(isl_union_set_get_space(uset));
  isl_union_set_free(uset);
  isl_space_free(space);
  if (id = id2)
  {
    isl_id_free(id);
    isl_id_free(id2);
    node = isl_schedule_node_insert_filter(node, empty_filter);
    return node;
  }
  isl_id_free(id);
  isl_id_free(id2);
  ctx = isl_schedule_node_get_ctx(node);

  /* S -> [D -> A] */
  access = io_comm_access_ref(data->kernel, node, group, ref, data->read);
  // TODO: understand the wrap relation */

  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    isl_union_set_free(empty_filter);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return node;
  }

  from_access = autosa_create_io_access_stmt(
      ctx, group, group, data->local_tile,
      isl_schedule_node_get_schedule_depth(node), data->stmt_name);

  /* Create a register tiling. */
  tile = create_register_tiling(node, group, ref);
  ma = isl_multi_aff_copy(tile->tiling);
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);

  domain = isl_union_map_range(access);
  /* Update the serialization bound. */
  group->local_array->serialize_bound = isl_set_card(isl_set_from_union_set(isl_union_set_copy(domain)));

  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  access = isl_union_set_wrapped_domain_map(domain);
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

  graft = isl_schedule_node_from_extension(access);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);
  node = isl_schedule_node_insert_filter(node, empty_filter);
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_parent(node);

  autosa_array_tile_free(tile);

  return node;
}

static __isl_give isl_schedule_node *add_serialize_stmt_acc(
    __isl_take isl_schedule_node *node,
    struct autosa_array_ref_group *group,
    struct autosa_kernel *kernel,
    struct autosa_array_tile *tile,
    char *stmt_name,
    int read)
{
  struct add_serialize_stmt_acc_data data = {
      group, NULL, kernel, tile, stmt_name, read};

  for (int i = 0; i < group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = group->refs[i];
    data.ref = ref;
    node = isl_schedule_node_map_descendant_bottom_up(
        node, &add_serialize_stmt_acc_single, &data);
  }

  return node;
}

static __isl_give isl_schedule_node *add_serialize_stmt_tile(
    __isl_take isl_schedule_node *node,
    struct autosa_array_ref_group *group,
    struct autosa_kernel *kernel,
    struct autosa_array_tile *local_tile, /* Local buffer */
    struct autosa_array_tile *tile,       /* Tile to be copied */
    char *stmt_name,
    int read)
{
  isl_union_map *access;
  int empty;
  isl_multi_aff *from_access;
  isl_multi_aff *ma;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  isl_union_set *domain;
  isl_schedule_node *graft;

  access = io_comm_access(kernel, node, group, read);
  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return node;
  }

  from_access = autosa_create_io_access_stmt(kernel->ctx, group, group,
                                             local_tile, isl_schedule_node_get_schedule_depth(node), stmt_name);

  ma = isl_multi_aff_copy(tile->tiling);
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);

  /* [D -> A] */
  domain = isl_union_map_range(access);
  /* Restrain the buffer to the local tile size. */
  if (!autosa_array_is_scalar(group->array))
  {
    isl_map *map;
    isl_set *set;
    set = isl_map_domain(isl_map_from_union_map(isl_union_set_unwrap(domain)));
    map = group_tile_buffer(group, tile);
    map = isl_map_intersect_domain(map, set);
    domain = isl_union_set_from_set(isl_map_wrap(map));
  }

#ifdef _DEBUG
  //DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
  //DBGUSET(stdout, domain, isl_schedule_node_get_ctx(node));
#endif
  /* Extract the serialization bound. */
  group->local_array->serialize_bound = isl_set_card(
      isl_set_from_union_set(isl_union_set_copy(domain)));  
#ifdef _DEBUG
  //DBGPWQPOLY(stdout, group->local_array->serialize_bound, isl_schedule_node_get_ctx(node));
#endif

  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  access = isl_union_set_wrapped_domain_map(domain);
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

  graft = isl_schedule_node_from_extension(access);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);

  return node;
}

/* Generate a schedule for serializing/deserializing the host data.
 */
static __isl_give isl_schedule *generate_serialize_schedule(
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group,
    struct autosa_hw_module *module,
    struct autosa_gen *gen,
    int in)
{
  isl_printer *p;
  isl_schedule_node *node;
  isl_ctx *ctx;
  struct autosa_io_buffer *buf;
  int io_level, i;
  char *stmt_name;
  isl_union_set *empty_filter;
  isl_union_map *group_access;
  isl_union_set *group_domain;
  isl_id *id;
  isl_schedule *sched;
  isl_union_set *group_core = NULL;

  ctx = gen->ctx;
  if (gen->options->autosa->lower_int_io_L1_buffer && group->io_L1_lower_schedule)
    node = isl_schedule_get_root(group->io_L1_lower_schedule);
  else
    node = isl_schedule_get_root(group->io_schedule);
  node = autosa_tree_move_down_to_kernel(node);

  /* Compute the union of domains of all the array references in the group. */
  node = isl_schedule_node_child(node, 0); // context
  node = isl_schedule_node_child(node, 0);
  if (gen->options->autosa->local_reduce && group->attached_drain_group)
    node = insert_io_group_access_domain_local_reduce(node, group, kernel, in, 0, 1);
  else
    node = insert_io_group_access_domain(node, group, kernel, in);
  node = isl_schedule_node_child(node, 0);
  group_core = isl_union_set_universe(isl_schedule_node_get_domain(node));
  node = autosa_tree_move_up_to_kernel(node);

  /* Generate the statement */
  p = isl_printer_to_str(ctx);
  p = isl_printer_print_str(p, in ? "serialize" : "deserialize");
  stmt_name = isl_printer_get_str(p);
  isl_printer_free(p);

  io_level = module->level;
  /* Locate the next buffer. */
  for (i = io_level; i >= 1; i--)
  {
    buf = group->io_buffers[i - 1];
    if (buf->tile != NULL)
      break;
  }
  /* Move the schedule node to the level of the buffer.
   * TODO: fix it when the buf->tile == NULL.
   */
  node = autosa_tree_move_down_to_depth(node, buf->tile->depth, group_core);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif
  if (!buf->tile)
  {
    /* If there is more than one reference in the I/O group to be serialized.
     * We will disable the serialization for this module.
     */
    if (group->n_ref > 1)
    {
      isl_schedule_node_free(node);
      return NULL;
    }
    else
    {
      node = add_serialize_stmt_acc(node, group, kernel, buf->tile, stmt_name, in);
    }
  }
  else
  {
    node = add_serialize_stmt_tile(node, group, kernel, buf->tile, buf->tile, stmt_name, in);
    node = isl_schedule_node_cut(node);
    empty_filter = isl_union_set_from_set(isl_set_empty(isl_set_get_space(kernel->context)));
    node = isl_schedule_node_insert_filter(node, empty_filter);
  }
  free(stmt_name);

  /* Add the host_serialize mark. */
  id = isl_id_alloc(ctx, "host_serialize", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  /* Update the array information */
  group->local_array->host_serialize = 1;

  sched = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);
  isl_union_set_free(group_core);

  return sched;
}

/* This function recalculates the bound of io module ids for the io module.
 * We will insert a filter that equals the io id to the 
 * sched dim at each dimension.
 * Then we will compute the domain of these io ids and use them to update the 
 * io schedule context.
 * The node points to "array".
 */
static __isl_give isl_schedule_node *update_io_module_context(
  __isl_take isl_schedule_node *node,
  struct autosa_gen *gen,
  int io_level, int n_io_ids)
{
  isl_union_set *domain;
  isl_ctx *ctx;
  isl_set *grid;
  isl_schedule_node *tmp_node;
  isl_id_list *io_ids;
  isl_set *context;

  ctx = isl_schedule_node_get_ctx(node);
  tmp_node = isl_schedule_node_copy(node);

  /* Add io ids filters down to the io_level */
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");
  tmp_node = add_io_ids_filter(tmp_node, io_ids, 1, n_io_ids, 0, 0, 0);
  
  /* Collect the domain down to the io_level */
  domain = isl_schedule_node_get_domain(tmp_node);
  grid = isl_union_set_params(domain);
  grid = isl_set_from_params(grid);

  isl_id_list_free(io_ids);
  isl_schedule_node_free(tmp_node);

  /* Update the context. */
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0); // context
  context = isl_schedule_node_context_get_context(node);  
  context = isl_set_intersect(context, grid);
  context = isl_set_coalesce(context);

  node = isl_schedule_node_delete(node);
  node = isl_schedule_node_insert_context(node, context);

  return node;
}

/* Generate the schedule for the I/O module.  
 * We will insert statements at the corresponding position in the schedule tree
 * to transfer the data.
 * The statement is in the format of:
 * in_trans/out_trans[_dram]/[_dram_serialize]/[_boundary].fifo_suffix[_local].
 * is_filter.is_buffer.filte_depth.filter_dim.buf_cur_lane.buf_nxt_lane.coalesce_depth.coalesce_ub
 * 
 * If is_buffer is disabled, we will insert one I/O statement for 
 * transferring the data between the same-level I/O modules and lower-level modules.
 * If is_buffer is enabled, we will insert two I/O statements:
 * - one for transaferring the data between the same-level I/O modules and store
 *   the data required for the lower-level I/O modules in the buffers.
 * - one for transaferring the data to/from the lower-level I/O modules from/to 
 *   the local buffers.
 * If host data serialization is enabled, we will generate a separate schedule 
 * for serializing/deserializing the host data.
 */
static isl_stat generate_default_io_module_schedule(
  __isl_take struct autosa_hw_module *module,
  __isl_keep isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_kernel *kernel,
  struct autosa_gen *gen,
  int io_level, int space_dim,
  int is_filter, int is_buffer,
  int read, int boundary)
{
  isl_schedule *sched1, *sched2;
  isl_ctx *ctx;
  isl_printer *p;
  char *io_mark;
  int n_io_ids = 0;
  isl_id_list *io_ids;
  isl_id *id;
  int is_mark;
  isl_set *context;
  char *fifo_suffix, *buf_suffix;
  isl_union_set *empty_filter = NULL;
  isl_union_set *eq_filter = NULL;
  isl_union_set *neq_filter = NULL;
  int depth;
  char *stmt_name;
  struct autosa_io_buffer *buf = NULL;
  int i;
  isl_union_set *id_filter;
  isl_union_set *group_core = NULL;

  ctx = isl_schedule_node_get_ctx(node);
  sched1 = isl_schedule_node_get_schedule(node);
  sched2 = isl_schedule_dup(sched1);
  isl_schedule_free(sched1);
  node = isl_schedule_get_root(sched2);
  isl_schedule_free(sched2);

  /* Compute the union of domains of all the array references in the group. */
  node = autosa_tree_move_down_to_kernel(node);
  node = isl_schedule_node_child(node, 0); // context
  node = isl_schedule_node_child(node, 0);
  if (gen->options->autosa->local_reduce && group->attached_drain_group)
    node = insert_io_group_access_domain_local_reduce(node, group, kernel, read, 0, 1);
  else
    node = insert_io_group_access_domain(node, group, kernel, read);  
  node = isl_schedule_node_child(node, 0);
  group_core = isl_union_set_universe(isl_schedule_node_get_domain(node));    
  node = autosa_tree_move_up_to_kernel(node);

  /* Add the module id filters. */
  n_io_ids = space_dim - io_level + 1;
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");  
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = add_io_ids_filter(node, io_ids, io_level, space_dim - io_level + 1, is_filter, module->to_pe, read);
  node = autosa_tree_move_up_to_kernel(node);  

  /* Add the data transfer statements. */  
  init_suffix(module, group, &fifo_suffix, &buf_suffix);  
  /* Locate the next buffer. */
  for (i = io_level; i >= 1; i--)
  {
    buf = group->io_buffers[i - 1];
    if (buf->tile != NULL)
      break;
  }
  if (is_buffer)
  {
    if (i != io_level)
    {
      /* The buffer is optimized out at this level. */
      is_buffer = 0;
    }
  }

  /* Move the schedule node to the level of the buffer. 
   * In the current implementation, there will also be a buffer at the 
   * innermost level.
   */
  if (is_filter) {
    module->data_pack_inter = buf->n_lane;
    module->data_pack_intra = buf->n_lane;
    node = insert_filter_trans_stmts(
              node, io_ids, space_dim - io_level, io_level, read,
              buf, module, kernel, gen, boundary, 1, is_buffer, fifo_suffix, group, group_core);
  } else {
    if (is_buffer) {
      /* Insert two statements:
       * - Load from upper stream I/O modules/DRAM to buffer
       * - Write to downstream I/O modules from buffer
       */
      module->data_pack_inter = buf->n_lane;
      /* Locate the next buffer after the current buffer. */
      int cur_level = buf->level;
      struct autosa_io_buffer *cur_buf = buf;
      for (int i = cur_level - 1; i >= 1; i--)
      {
        buf = group->io_buffers[i - 1];
        if (buf->tile != NULL)
          break;
      }

      if (!buf->tile) {
        module->data_pack_intra = group->n_lane;        
      } else {
        module->data_pack_intra = buf->n_lane;
      }
      
      /* Insert the first statement. */
      node = autosa_tree_move_down_to_depth(node, cur_buf->tile->depth, kernel->core);
      p = isl_printer_to_str(ctx);
      p = print_io_trans_stmt_prefix(
              p, read, module->to_mem, gen->options->autosa->host_serialize, boundary, 0, NULL,
              0, 0, is_buffer, fifo_suffix, cur_buf->n_lane);
      node = insert_io_stmts_tile(node, cur_buf->n_lane, p, kernel, group, 
              cur_buf, cur_buf, read, is_buffer, module, 0);
            
      /* Insert the second statement. */
      p = isl_printer_to_str(ctx);
      p = print_io_trans_stmt_prefix(
              p, !read, 0, gen->options->autosa->host_serialize, boundary, 0, NULL,
              !read, read, is_buffer, fifo_suffix, cur_buf->n_lane);
      if (module->to_pe || !buf->tile) {
        node = insert_io_stmts_acc(
                  node, group->n_lane, p, kernel, group, cur_buf, read, is_buffer);
      } else {
        node = autosa_tree_move_down_to_io_mark(node, group_core, buf->level);
        node = isl_schedule_node_child(node, 0);        
        node = insert_io_stmts_tile(node, buf->n_lane, p, kernel, group, 
                  cur_buf, buf, read, is_buffer, module, 1);
      }
    } else {
      /* Insert one statement.
       * Load from upper stream I/O modules/DRAM and write to
       * downstream I/O modules.
       */
      if (buf->tile) {
        module->data_pack_inter = buf->n_lane;
        module->data_pack_intra = buf->n_lane;

        node = autosa_tree_move_down_to_depth(node, buf->tile->depth, kernel->core);
        p = isl_printer_to_str(ctx);
        p = print_io_trans_stmt_prefix(
              p, read, module->to_mem, gen->options->autosa->host_serialize, boundary, 0, NULL,
              !read, read, is_buffer, fifo_suffix, buf->n_lane);
        node = insert_io_stmts_tile(node, buf->n_lane, p, kernel, group, 
                  NULL, buf, read, is_buffer, module, 1);
      } else {
        module->data_pack_inter = group->n_lane;
        module->data_pack_intra = group->n_lane;

        p = print_io_trans_stmt_prefix(
                p, read, module->to_mem, gen->options->autosa->host_serialize, boundary, 0, NULL,
                !read, read, is_buffer, fifo_suffix, group->n_lane);
        node = insert_io_stmts_acc(node, group->n_lane, p, kernel, group, NULL, read, is_buffer);               
      }
    }
  }

  free(fifo_suffix);
  free(buf_suffix);
  isl_union_set_free(group_core);

  /* Add the module mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  sched1 = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  if (!boundary)
  {
    module->sched = sched1;
    module->type = (group->group_type == AUTOSA_DRAIN_GROUP) ? DRAIN_MODULE : IO_MODULE;
    module->level = io_level;
    module->n_io_group++;
    module->io_groups = (struct autosa_array_ref_group **)realloc(module->io_groups,
                                                                  module->n_io_group * sizeof(struct autosa_array_ref_group *));
    module->io_groups[module->n_io_group - 1] = group;
    module->inst_ids = io_ids;
    module->kernel = kernel;
    module->is_buffer = is_buffer;
    module->is_filter = is_filter;
    /* Create IO module variables. */
    if (is_buffer)
    {
      for (int i = io_level; i >= 1; i--)
      {
        buf = group->io_buffers[i - 1];
        if (buf->tile != NULL)
          break;
      }
      create_io_module_vars(module, kernel, buf->tile, 0);
    }
  }
  else
  {
    isl_id_list_free(io_ids);
    module->boundary_sched = sched1;
  }

  return isl_stat_ok;
}

/* Generate the default I/O module when either is_filter or is_buffer is zero.
 */
static __isl_give struct autosa_hw_module *generate_default_io_module(
    __isl_take struct autosa_hw_module *module, __isl_keep isl_schedule_node *node,
    struct autosa_array_ref_group *group, struct autosa_kernel *kernel,
    struct autosa_gen *gen,
    int io_level, int space_dim, int is_filter, int is_buffer, int read)
{
  isl_ctx *ctx = gen->ctx;

  generate_default_io_module_schedule(module, node, group,
                                      kernel, gen, io_level, space_dim, is_filter, is_buffer, read, 0);

  if (is_filter)
  {
    /* Add the boundary module schedule. */
    module->boundary = 1;
    generate_default_io_module_schedule(module, node, group,
                                        kernel, gen, io_level, space_dim, is_filter, is_buffer, read, 1);
  }

  return module;
}

/* Generate the I/O modules for transffering the data.
 * The I/O module is decribed by two features:
 * - is_filter: If the module is a filter node, it will keep the data 
 *   that belongs to it and sends to the lower-level I/O modules or PEs. 
 *   Else, it will simply pass the data to downstream modules.
 * - is buffer: If the module is buffered. We will allocate a local buffer 
 *   inside the module.
 */
static __isl_give struct autosa_hw_module *generate_io_module_by_type(
    __isl_take struct autosa_hw_module *module, __isl_keep isl_schedule_node *node,
    struct autosa_array_ref_group *group, struct autosa_kernel *kernel,
    struct autosa_gen *gen, int io_level, int space_dim,
    int is_filter, int is_buffer, int read)
{
//#ifdef _DEBUG
//  printf("array_name: %s\n", group->array->name);
//  printf("module name: %s\n", module->name);
//  if (!strcmp(module->name, "A_IO_L3_in"))
//    printf("here\n");
//#endif

  if (is_filter && is_buffer)
  {
    module = generate_filter_buffer_io_module(module, node, group, kernel,
                                              gen, io_level, space_dim, is_filter, is_buffer, read);
  }
  else
  {
    module = generate_default_io_module(module, node, group, kernel,
                                        gen, io_level, space_dim, is_filter, is_buffer, read);
  }

  return module;
}

/* This function updates the data pack factors for I/O modules that access
 * the external DRAM. The module data should also be serialized.
 */
static int update_serialize_data_pack(struct autosa_gen *gen, struct autosa_hw_module *module)
{
  isl_union_map *sizes;
  int *data_pack_ubs = NULL;
  int dram_limit = 64; // bytes
  int ele_size = module->io_groups[0]->array->size;
  int n_lane = module->data_pack_inter;
  int host_pack = -1;

  sizes = extract_sizes_from_str(gen->ctx, module->options->autosa->data_pack_sizes);
  data_pack_ubs = read_data_pack_sizes(sizes, 3);
  if (data_pack_ubs) 
    dram_limit = data_pack_ubs[2];
  free(data_pack_ubs);
  isl_union_map_free(sizes);

  for (int limit = dram_limit; limit >= ele_size * n_lane; limit -= ele_size * n_lane) 
  {
    if (limit % (ele_size * n_lane) == 0 && module->coalesce_bound % (limit / (ele_size * n_lane)) == 0)
    {
      host_pack = limit / ele_size;
      break;
    }
  }

  return host_pack != -1? host_pack : module->data_pack_intra;
}

/* This function builds a set of I/O modules for each I/O group.
 * We will first examine if any flow dependence that is associated with the 
 * current group is carried by the array part loops. 
 * In that case, credit control should be added to force the dependece.
 * TODO: to be implemented.
 * Next, we will generate the copy-in set and copy-out set of I/O modules for 
 * the I/O groups. At each I/O level, we generate one I/O module.
 * We apply the I/O module pruning by default here.
 * Specifically, if the copy-out set at the current array_part loops equals 
 * the copy-in set at of the next array_part loops, there is no need to generate
 * to go off-chip, we will prune away such I/O modules.
 * If the I/O group has interior I/O at the PE level, the data required for the 
 * next iteration should reside in the PEs.
 * Otherwise, we will connect the copy-out I/O modules to the copy-in I/O modules,
 * and buffer the data on-chip. (TODO: not supported yet.)
 */
static __isl_give struct autosa_hw_module **sa_io_module_gen(
    struct autosa_array_ref_group *group,
    struct autosa_gen *gen, int *n_modules, int in, int out)
{
  // TODO: Add the support for manual tuning.
  isl_schedule_node *node;
  isl_ctx *ctx;
  struct autosa_kernel *kernel;
  int space_dim;
  int io_level;
  struct autosa_hw_module **modules = NULL;
  int module_cnt = 0;
  int credit = 0;

  ctx = gen->ctx;
  if (gen->options->autosa->lower_int_io_L1_buffer && group->io_L1_lower_schedule) 
    node = isl_schedule_get_root(group->io_L1_lower_schedule);
  else
    node = isl_schedule_get_root(group->io_schedule);
  
  io_level = group->io_level;
  space_dim = group->space_dim;
  kernel = gen->kernel;
  node = autosa_tree_move_down_to_kernel(node);

  /* Test if the deps in this I/O group are carried by array part loops.
   * If so, data hazards are possible, and we will set the credit as true
   * so that we could enable credit control between read and write I/O modules to 
   * prevent the data hazards. 
   * TODO: This is not supported yet.
   */
  if (gen->options->autosa->credit_control)
  {
    if (is_flow_dep_carried_by_array_part_loops(group->io_schedule, group, kernel))
      credit = 1;

    //    if (group->local_array->array_type == AUTOSA_INT_ARRAY) {
    //      isl_bool carried = isl_bool_false;
    //      isl_union_map *umap;
    //
    //      node = autosa_tree_move_down_to_array(node, kernel->core);
    //      node = isl_schedule_node_parent(node);
    //      umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
    //      for (int i = 0; i < group->n_ref; i++) {
    //        struct autosa_stmt_access *ref = group->refs[i];
    //        for (int j = 0; j < ref->n_io_info; j++) {
    //          struct autosa_io_info *io_info = ref->io_info[j];
    //          if (io_info->io_type == group->io_type &&
    //                !isl_vec_cmp(io_info->dir, group->dir)) {
    //            isl_map *test;
    //            isl_map *schedule_dep;
    //            int dim;
    //            int is_parallel;
    //            isl_union_map *dep = isl_union_map_from_map(
    //                isl_map_factor_domain(
    //                isl_map_from_basic_map(isl_basic_map_copy(io_info->dep->isl_dep))));
    //            dep = isl_union_map_apply_range(dep, isl_union_map_copy(umap));
    //            dep = isl_union_map_apply_domain(dep, isl_union_map_copy(umap));
    //            if (isl_union_map_is_empty(dep)) {
    //              isl_union_map_free(dep);
    //              break;
    //            }
    //            schedule_dep = isl_map_from_union_map(dep);
    //            test = isl_map_universe(isl_map_get_space(schedule_dep));
    //            dim = isl_schedule_node_band_n_member(node);
    //            for (int n = 0; n < dim; n++) {
    //              test = isl_map_equate(test, isl_dim_in, n, isl_dim_out, n);
    //            }
    //            is_parallel = isl_map_is_subset(schedule_dep, test);
    //            isl_map_free(schedule_dep);
    //            isl_map_free(test);
    //
    //            if (!is_parallel) {
    //              /* Dependence is carried by the array part loops. */
    //              carried = isl_bool_true;
    //              break;
    //            }
    //          }
    //        }
    //      }
    //      isl_union_map_free(umap);
    //      if (carried) {
    //        credit = 1;
    //      }
    //      node = autosa_tree_move_up_to_kernel(node);
    //    }
  }

  /* At each I/O level, generate one I/O module. */
  /* Copy-in group. */
  //if (in && is_io_module_valid(node, kernel, group, 1))
  if (in && group->copy_in)
  {    
    for (int i = io_level; i >= 1; i--)
    {
      struct autosa_hw_module *module;
      char *module_name = NULL;
      char *io_mark = NULL;
      isl_printer *p_str;
      int is_filter;
      int is_buffer;
      int innermost, outermost;

      /* Classify the module type. */
      outermost = io_level;
      if (group->io_type == AUTOSA_INT_IO)
        innermost = 1;
      else
        innermost = 2; // IO_L1 is integrated into PEs. No need to generate.

      /* Since we perform I/O clustering automatically, all the I/O modules
       * except the outermost level will be in the filter mode:
       * which means that they will pass data to downstreaming modules
       * and filter out the data that they need for the lower-level modules
       * they are connected to.
       */  
      if (i == outermost && outermost != innermost) {
        is_filter = 0;
        if (gen->options->autosa->lower_int_io_L1_buffer) {
          is_filter = 1;
        }
      } else
        is_filter = 1;
      
      if (group->group_type == AUTOSA_DRAIN_GROUP)
      {
        if (i == innermost)
          is_buffer = 1;
        else
          is_buffer = 0;
      }
      else if (group->group_type == AUTOSA_IO_GROUP)
      {
        if (group->local_array->array_type == AUTOSA_INT_ARRAY)
        {
          if (group->io_type == AUTOSA_EXT_IO)
          {
            if (i == innermost)
              is_buffer = 1;
            else
              is_buffer = 0;
          }
          else if (group->io_type == AUTOSA_INT_IO)
          {
            is_buffer = 0;
          }
        }
        else if (group->local_array->array_type == AUTOSA_EXT_ARRAY)
        {
          if (i == innermost)
            is_buffer = 1;
          else
            is_buffer = 0;
        }
      }

      if (gen->options->autosa->two_level_buffer)
      {
        /* When two-level buffering is enabled, 
         * we will implement a second-level buffe at the outermost I/O module.
         */
        if (i == outermost)
          is_buffer = 1;
      }
      if (gen->options->autosa->lower_int_io_L1_buffer)
      {
        if (i == outermost) 
          is_buffer = group->io_buffers[outermost - 1]->tile? 1 : 0;
      }      

      /* Generate the I/O module */
      if (i >= innermost && i <= outermost)
      {
        module = autosa_hw_module_alloc(gen);
        module_name = generate_io_module_name(ctx, group, i, 1);
        module->name = module_name;
        module->to_pe = (i == innermost) ? 1 : 0;
        module->to_mem = (i == outermost) ? 1 : 0;
        module->credit = (i == outermost) ? credit : 0;
        module->n_array_ref = group->local_array->n_io_group_refs;
        module->in = 1;
        module->is_serialized = (gen->options->autosa->host_serialize && module->to_mem) ? 1 : 0;
        if (module->to_mem)
        {
          /* Create the group_ref and mem_port mapping. */
          for (int p = 0; p < group->n_mem_ports; p++)
          {
            int group_ref_offset = group->local_array->n_io_group_refs;
            int mem_port_offset = group->mem_port_id;            
            group->local_array->group_ref_mem_port_map.emplace_back(
                std::make_pair(group_ref_offset + p, mem_port_offset + p));
          }
          group->local_array->n_io_group_refs += group->n_mem_ports;
        }

        module = generate_io_module_by_type(module, node, group, kernel,
                                            gen, i, space_dim, is_filter, is_buffer, 1);
        if (module->is_serialized)
        {
          /* Generate the schedule for serializing/deserializing the host data. */          
          module->serialize_sched = generate_serialize_schedule(
              kernel, group, module, gen, 1);
          if (module->serialize_sched) {
            /* Update the data packing factor. */            
            module->data_pack_serialize = update_serialize_data_pack(gen, module);            
            module->io_groups[0]->local_array->n_lane = module->data_pack_serialize;
            module->io_groups[0]->local_array->array->n_lane = module->data_pack_serialize;
          }
        } else {
          module->is_serialized = 0;
        }

        module_cnt++;
        modules = (struct autosa_hw_module **)realloc(modules,
                                                      module_cnt * sizeof(struct autosa_hw_module *));
        modules[module_cnt - 1] = module;
      }
    }
  }

  /* Copy-out group. */  
  if (out && group->copy_out)
  {    
    for (int i = 1; i <= io_level; i++)
    {
      struct autosa_hw_module *module;
      char *module_name = NULL;
      char *io_mark = NULL;
      isl_printer *p_str;
      int is_filter;
      int is_buffer;
      int innermost, outermost;

      /* Classify the module type. */
      outermost = io_level;
      if (group->io_type == AUTOSA_INT_IO)
        innermost = 1;
      else
        innermost = 2; // IO_L1 is integrated into PEs.

      if (i == outermost && outermost != innermost)
        is_filter = 0;
      else
        is_filter = 1;
      if (group->group_type == AUTOSA_DRAIN_GROUP)
      {
        if (i == innermost)
          is_buffer = 1;
        else
          is_buffer = 0;
      }
      else if (group->group_type == AUTOSA_IO_GROUP)
      {
        if (group->io_type == AUTOSA_INT_IO)
          is_buffer = 0;
        else
        {
          if (i == innermost)
            is_buffer = 1;
          else
            is_buffer = 0;
        }
      }

      if (gen->options->autosa->two_level_buffer)
      {
        /* When two-level buffering is enabled, 
         * we will implement a second-level buffer at the outermost I/O module.
         */
        if (i == outermost)
          is_buffer = 1;
      }

      /* Generate the I/O module. */
      if (i >= innermost && i <= outermost)
      {
        module = autosa_hw_module_alloc(gen);
        module_name = generate_io_module_name(ctx, group, i, 0);
        module->name = module_name;
        module->to_pe = (i == innermost) ? 1 : 0;
        module->to_mem = (i == outermost) ? 1 : 0;
        module->credit = (i == outermost) ? credit : 0;
        module->n_array_ref = group->local_array->n_io_group_refs;
        module->in = 0;
        module->is_serialized = (gen->options->autosa->host_serialize && module->to_mem) ? 1 : 0;
        if (module->to_mem)
        {
          /* Create the group_ref and mem_port mapping. */
          for (int p = 0; p < group->n_mem_ports; p++)
          {
            int group_ref_offset = group->local_array->n_io_group_refs;
            int mem_port_offset = group->mem_port_id;            
            group->local_array->group_ref_mem_port_map.emplace_back(
                std::make_pair(group_ref_offset + p, mem_port_offset + p));
          }
          group->local_array->n_io_group_refs += group->n_mem_ports;
        }

        module = generate_io_module_by_type(module, node, group, kernel,
                                            gen, i, space_dim, is_filter, is_buffer, 0);
        if (module->is_serialized)
        {
          /* Generate the schedule for serializing/deserializing the host data. */          
          module->serialize_sched = generate_serialize_schedule(
              kernel, group, module, gen, 0);
          if (module->serialize_sched) {
            /* Update the data packing factor. */
            module->data_pack_serialize = update_serialize_data_pack(gen, module);            
            module->io_groups[0]->local_array->n_lane = module->data_pack_serialize;
            module->io_groups[0]->local_array->array->n_lane = module->data_pack_serialize;
          }            
        } else {
          module->is_serialized = 0;
        }

        module_cnt++;
        modules = (struct autosa_hw_module **)realloc(modules,
                                                      module_cnt * sizeof(struct autosa_hw_module *));
        modules[module_cnt - 1] = module;
      }
    }
  }

  isl_schedule_node_free(node);
  *n_modules = module_cnt;
  return modules;
}

/* If the band node "node" has more than "n" members, then split off
 * the first "n" of them.
 */
static __isl_give isl_schedule_node *split_band(
    __isl_take isl_schedule_node *node, int n)
{
  int dim;

  dim = isl_schedule_node_band_n_member(node);
  if (n < dim)
    node = isl_schedule_node_band_split(node, n);

  return node;
}

/* Compute the effective sa size as a list of the sizes in each dimension.
 *
 * The sa size specified by the user or set by default
 * in read_array_part_tile_sizes() and applied by the PE filter,
 * may be too large for the given code in the sense that
 * it may contain PEs that don't need to execute anything.
 * We therefore don't return this sa size, but instead the
 * smallest grid size that ensures that all blocks that actually
 * execute code are included in the grid.
 *
 * We first extract a description of the grid, i.e., the possible values
 * of the PE ids, from the domain elements in "domain" and
 * kernel->pe_filter.
 * The PE ids are parameters in kernel->pe_filter.
 * We simply need to change them into set dimensions.
 *
 * Then, for each PE dimension, we compute the maximal value of the PE id
 * and add one.
 */
static __isl_give isl_multi_pw_aff *extract_sa_grid_size(
    struct autosa_kernel *kernel, __isl_take isl_union_set *domain)
{
  int i;
  isl_set *grid;
  isl_set *context;
  isl_multi_pw_aff *size;

  domain = isl_union_set_intersect(domain,
                                   isl_union_set_copy(kernel->pe_filter));

  grid = isl_union_set_params(domain);
  grid = isl_set_from_params(grid);
  grid = isl_set_add_dims(grid, isl_dim_set, kernel->n_sa_dim);

  for (i = 0; i < kernel->n_sa_dim; ++i)
  {
    int pos;
    isl_id *id;

    if (!grid)
      return NULL;

    id = isl_id_list_get_id(kernel->pe_ids, i);
    pos = isl_set_find_dim_by_id(grid, isl_dim_param, id);
    isl_id_free(id);
    if (pos < 0)
      isl_die(isl_set_get_ctx(grid), isl_error_internal,
              "missing constraints on PE identifier",
              grid = isl_set_free(grid));
    grid = isl_set_equate(grid, isl_dim_param, pos, isl_dim_set, i);
    grid = isl_set_project_out(grid, isl_dim_param, pos, 1);
  }

  grid = isl_set_coalesce(grid);
  size = ppcg_size_from_extent(grid);
  context = isl_set_params(isl_set_copy(kernel->context));
  return isl_multi_pw_aff_gist(size, context);
}

/* Internal struct for add_pe_ext_io_copies. */
struct autosa_add_pe_ext_io_copies_data
{
  struct autosa_kernel *kernel;
  struct autosa_array_ref_group *pe_group;
  struct autosa_array_ref_group *io_group;
  struct autosa_stmt_access *ref;
  int read;
  int in; /* I/O direction */
  int dummy;
  int reduce;
  isl_union_set *filter;
};

/* Find the PE group that contains the reference "ref" from the IO group.
 */
static struct autosa_array_ref_group *autosa_find_pe_group(
    struct autosa_local_array_info *local_array,
    struct autosa_array_ref_group *io_group,
    struct autosa_stmt_access *ref)
{
  /* As all accesses from the array are merged together for internal array,
   * simply return the first PE group. 
   */
  if (local_array->array_type == AUTOSA_INT_ARRAY)
    return local_array->pe_groups[0];

  for (int i = 0; i < local_array->n_pe_group; i++)
  {
    struct autosa_array_ref_group *pe_group = local_array->pe_groups[i];
    if (pe_group->refs[0] == ref)
      return pe_group;
  }

  return NULL;
}

/* Given a schedule node "node" of the type "isl_schedule_node_leaf", 
 * we will test if it is under any extension node.
 * If so, we will then test if the current node intersect with the extension domain. 
 */
static isl_bool leaf_node_is_extended(__isl_keep isl_schedule_node *node)
{
  isl_schedule_node *node_e;
  isl_schedule_node *node_f;
  isl_union_set *filter;
  isl_union_map *extension;
  isl_union_set *extension_range;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return isl_bool_error;

  node_e = isl_schedule_node_copy(node);
  node_f = isl_schedule_node_copy(node);

  while (node_e && isl_schedule_node_has_parent(node_e))
  {
    if (isl_schedule_node_get_type(node_e) == isl_schedule_node_extension)
      break;
    node_e = isl_schedule_node_parent(node_e);
  }

  if (node_e == NULL || isl_schedule_node_get_type(node_e) != isl_schedule_node_extension)
  {
    isl_schedule_node_free(node_e);
    isl_schedule_node_free(node_f);
    return isl_bool_false;
  }

  extension = isl_schedule_node_extension_get_extension(node_e);

  while (node_f && isl_schedule_node_has_parent(node_f))
  {
    if (isl_schedule_node_get_type(node_f) == isl_schedule_node_filter)
      break;
    node_f = isl_schedule_node_parent(node_f);
  }

  filter = isl_schedule_node_filter_get_filter(node_f);
  extension_range = isl_union_map_range(extension);
  filter = isl_union_set_intersect(filter, extension_range);
  isl_schedule_node_free(node_e);
  isl_schedule_node_free(node_f);
  if (isl_union_set_is_empty(filter))
  {
    isl_union_set_free(filter);
    return isl_bool_false;
  }

  isl_union_set_free(filter);
  return isl_bool_true;
}

/* Insert data transfer statements beside the program statements. 
 * If the statement is under the SIMD loop, the data transfer statements 
 * are inserted before/after the SIMD loop. 
 * Otherwise, it is inserted before/after the statement.
 */
__isl_give isl_schedule_node *add_pe_ext_io_copies_stmt(
    __isl_take isl_schedule_node *node, void *user)
{
  struct autosa_add_pe_ext_io_copies_data *data =
      (struct autosa_add_pe_ext_io_copies_data *)(user);
  isl_union_set *domain;
  isl_space *space;
  isl_space *acc_space;
  isl_id *id;
  isl_union_map *access;
  int empty;
  isl_multi_aff *from_access;
  isl_ctx *ctx;
  isl_schedule_node *graft;
  isl_multi_aff *ma;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  struct autosa_array_ref_group *pe_group = data->pe_group;
  struct autosa_array_ref_group *io_group = data->io_group;
  struct autosa_array_tile *tile;
  int read = data->read;
  isl_union_map *sched;
  isl_union_map *ref_access;
  isl_map *acc;
  isl_bool ok;
  int is_simd;
  isl_printer *p_str;
  char *stmt_name;
  isl_union_set *empty_filter;
  int n_lane = io_group->n_lane;

  /* Test if the current stmt contains the reference. */
  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return node;

  /* Test if the node is under any extension node and if the 
   * node is extended by the extension node. 
   */
  if (!leaf_node_is_extended(node))
  {
    isl_set *set;
    isl_id *new_id;
    domain = isl_schedule_node_get_domain(node);
    set = isl_set_from_union_set(domain);
    space = isl_set_get_space(set);
    isl_set_free(set);
    id = isl_space_get_tuple_id(space, isl_dim_set);
    isl_space_free(space);
    acc_space = isl_map_get_space(data->ref->access);
    new_id = isl_space_get_tuple_id(acc_space, isl_dim_in);
    if (id != new_id)
    {
      isl_space_free(acc_space);
      isl_id_free(id);
      isl_id_free(new_id);

      /* Insert empty filter for dummy module. */
      if (data->dummy)
      {
        empty_filter = isl_union_set_from_set(
            isl_set_empty(isl_set_get_space(data->kernel->context)));
        node = isl_schedule_node_insert_filter(node, empty_filter);
      }
      return node;
    }
    isl_id_free(id);
    isl_id_free(new_id);
    isl_space_free(acc_space);
  }
  else
  {
    /* Simply return for the extension nodes. */
    return node;
  }

  ctx = isl_schedule_node_get_ctx(node);
  tile = NULL;
  /* Examine if there is any SIMD mark above. */
  is_simd = is_node_under_simd(node);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, ctx);
//#endif

  /* Aggregate the copy-in/out access
   * S -> [D -> A]
   * S: statement domain elements
   * D: prefix schedule dimensions
   * A: access
   */
  if (is_simd)
  {
    /* We will insert the statements before/after the SIMD loop. */
    if (data->dummy)
    {
      isl_union_set *empty_filter;
      empty_filter = isl_union_set_from_set(isl_set_empty(
          isl_set_get_space(data->kernel->context)));
      node = isl_schedule_node_insert_filter(node, empty_filter);
    }
    node = autosa_tree_move_up_to_mark(node, "simd");
  }
  access = io_comm_access_ref(data->kernel, node, io_group, data->ref, read);
  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return autosa_tree_move_up_to_kernel(node);
  }

  if (data->dummy)
  {
    data->filter = isl_schedule_node_get_domain(node);
  }

  //pe_group->array->global = 1;
  //pe_group->local_array->global = 1;

  /* read.fifoX[D -> A] -> [D -> A] */
  p_str = isl_printer_to_str(ctx);
  if (data->dummy)
    p_str = print_io_stmt_prefix(p_str, data->in, data->dummy, data->reduce, io_group);  
  else
    p_str = print_io_stmt_prefix(p_str, read, data->dummy, 0, io_group);
  
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  from_access = autosa_create_io_access_stmt(ctx, pe_group, io_group,
                                             autosa_array_ref_group_tile(pe_group),
                                             isl_schedule_node_get_schedule_depth(node), stmt_name);
  free(stmt_name);

  /* Create a register tiling. */
  tile = create_register_tiling(node, pe_group, data->ref);
  /* [D -> A] -> T */
  ma = isl_multi_aff_copy(tile->tiling);
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  /* read.fifoX[D -> A] -> T */
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);
  /* [D -> A] */
  domain = isl_union_map_range(access);
  /* read.fifoX[D -> A] */
  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  /* read.fifoX[D -> A] -> D */
  access = isl_union_set_wrapped_domain_map(domain);
  /* D -> read.fifoX[D -> A] */
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

//#ifdef _DEBUG
//  DBGUMAP(stdout, access, ctx);
//#endif

  graft = isl_schedule_node_from_extension(access);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, graft, ctx);
//  DBGMUPA(stdout, mupa, ctx);
//#endif  
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  if (n_lane > 1)
  {
    /* Perform data packing. */
    int n_index;
    int tile_size[1];
    isl_id *id;
    isl_union_map *umap;
    isl_union_set *filter;

    n_index = isl_schedule_node_band_n_member(graft);
    /* Split off the last dimension. */
    if (n_index > 1)
    {
      graft = isl_schedule_node_band_split(graft, n_index - 1);
      graft = isl_schedule_node_child(graft, 0);
    }
    /* Tile the last dimension. */
    tile_size[0] = n_lane;
    graft = autosa_tile_band(graft, tile_size);
    graft = isl_schedule_node_child(graft, 0);
    /* Create a filter. */
    filter = schedule_eq_lb(graft);
    graft = isl_schedule_node_insert_filter(graft, filter);
  }

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  if (read) {
    node = isl_schedule_node_graft_before(node, graft);
  } else {
    node = isl_schedule_node_graft_after(node, graft);
  }

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  if (data->dummy) {
    /* insert an empty filter. */
    empty_filter = isl_union_set_from_set(isl_set_empty(
        isl_set_get_space(data->kernel->context)));
    node = isl_schedule_node_insert_filter(node, empty_filter);
  }

  node = isl_schedule_node_parent(node); // filter
  node = isl_schedule_node_parent(node); // sequence
  node = isl_schedule_node_parent(node); // extension

  autosa_array_tile_free(tile);

  return node;
}

/* The "node" is pointed to the "PE" mark.
 * Add data transfer statements for each array access in the group.
 */
static __isl_give isl_schedule_node *add_pe_ext_io_copies(
    struct autosa_kernel *kernel,
    struct autosa_local_array_info *local_array,
    struct autosa_array_ref_group *io_group,
    __isl_take isl_schedule_node *node, int read)
{
  for (int i = 0; i < io_group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = io_group->refs[i];

    if ((io_group->local_array->array_type == AUTOSA_EXT_ARRAY) ||
       ((io_group->local_array->array_type == AUTOSA_INT_ARRAY) && 
       (read && ref->read) || (!read && ref->write)))
    {
      struct autosa_array_ref_group *pe_group =
          autosa_find_pe_group(local_array, io_group, ref);
      struct autosa_add_pe_ext_io_copies_data data =
          {kernel, pe_group, io_group, ref, read, read, 0, 0, NULL};
      node = isl_schedule_node_map_descendant_bottom_up(node,
                                                        &add_pe_ext_io_copies_stmt, &data);
    }
  }

  return node;
}

/* Add the statements for copy-in/out the data for array references associated with
 * interior I/O.
 * The "node" is pointed to the "PE" mark.
 */
__isl_give isl_schedule_node *add_pe_int_io_copies(
    struct autosa_kernel *kernel,
    struct autosa_local_array_info *local_array,
    struct autosa_array_ref_group *io_group,
    __isl_take isl_schedule_node *node, int read)
{
  struct autosa_array_tile *tile;
  isl_union_map *access;
  isl_schedule_node *graft;
  int empty;
  isl_multi_aff *from_access;
  isl_multi_aff *ma;
  isl_multi_pw_aff *mpa;
  isl_multi_union_pw_aff *mupa;
  isl_union_set *domain;
  struct autosa_array_ref_group *pe_group;
  int n_lane = io_group->n_lane;
  isl_printer *p_str;
  char *stmt_name;
  isl_id *id;

  node = isl_schedule_node_child(node, 0);
  /* For array references with interior I/O, 
   * search for the corresponding PE group. */
  pe_group = autosa_find_pe_group(local_array, io_group, NULL);
  tile = autosa_array_ref_group_tile(pe_group);

  /* Aggregate the copy-in/out access 
   * S -> [D -> A] 
   * S: statement domain elements
   * D: prefix schedule dimensions 
   * A: access */
  access = io_comm_access(kernel, node, io_group, read);
  empty = isl_union_map_is_empty(access);
  if (empty < 0 || empty)
  {
    isl_union_map_free(access);
    if (empty < 0)
      return isl_schedule_node_free(node);
    return autosa_tree_move_up_to_pe(node);
  }

  //pe_group->array->global = 1;
  //pe_group->local_array->global = 1;

  /* read.fifoX[D -> A] -> [D -> A] */
  /* Generate statement name. */
  p_str = isl_printer_to_str(kernel->ctx);
  p_str = print_io_stmt_prefix(p_str, read, 0, 0, io_group);  
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  from_access = autosa_create_io_access_stmt(kernel->ctx, pe_group, io_group,
                                             autosa_array_ref_group_tile(pe_group),
                                             isl_schedule_node_get_schedule_depth(node), stmt_name);
  free(stmt_name);

  /* [D -> A] -> T */
  ma = isl_multi_aff_copy(tile->tiling);
  ma = isl_multi_aff_pullback_multi_aff(ma,
                                        isl_multi_aff_copy(from_access));
  mpa = isl_multi_pw_aff_from_multi_aff(ma);
  /* read.fifoX[D -> A] -> T */
  mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);
  /* [D -> A] */
  domain = isl_union_map_range(access);
  /* If the array is not a scalar, then we copy in/out the entire
   * tile to/from the local memory. 
   */
  if (read && !autosa_array_is_scalar(io_group->array))
  {
    isl_map *map;
    isl_set *set;
    set = isl_map_domain(isl_map_from_union_map(isl_union_set_unwrap(domain)));
    map = group_tile_buffer(io_group, io_group->pe_tile);
    map = isl_map_intersect_domain(map, set);
    domain = isl_union_set_from_set(isl_map_wrap(map));
  }

  /* read.fifoX[D -> A] */
  domain = isl_union_set_preimage_multi_aff(domain, from_access);
  access = isl_union_set_wrapped_domain_map(domain);
  access = isl_union_map_reverse(access);
  access = isl_union_map_coalesce(access);

  graft = isl_schedule_node_from_extension(access);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  if (n_lane > 1)
  {
    /* Perform data packing. */
    int n_index;
    int tile_size[1];
    isl_id *id;
    isl_union_map *umap;
    isl_union_set *filter;

    n_index = isl_schedule_node_band_n_member(graft);
    /* Split off the last dimension. */
    if (n_index > 1)
    {
      graft = isl_schedule_node_band_split(graft, n_index - 1);
      graft = isl_schedule_node_child(graft, 0);
    }
    /* Tile the last dimension. */
    tile_size[0] = n_lane;
    graft = autosa_tile_band(graft, tile_size);
    graft = isl_schedule_node_child(graft, 0);
    /* Create a filter. */
    filter = schedule_eq_lb(graft);
    graft = isl_schedule_node_insert_filter(graft, filter);
    /* Move to the tile loop. */
    graft = isl_schedule_node_parent(graft);
  }

  /* Insert a "pipeline" mark inside the band node. */
  id = isl_id_alloc(kernel->ctx, "hls_pipeline", NULL);
  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_mark(graft, id);
  graft = isl_schedule_node_parent(graft);

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  if (read)
  {
    node = isl_schedule_node_graft_before(node, graft);
  }
  else
  {
    node = isl_schedule_node_graft_after(node, graft);
  }

  node = autosa_tree_move_up_to_pe(node);

  return node;
}

static isl_bool find_latency_mark(__isl_keep isl_schedule_node *node, void *user)
{
  if (isl_schedule_node_get_type(node) == isl_schedule_node_mark)
  {
    isl_id *id;

    id = isl_schedule_node_mark_get_id(node);
    if (!strcmp(isl_id_get_name(id), "latency"))
    {
      isl_id_free(id);
      return isl_bool_false;
    }
    isl_id_free(id);
  }

  return isl_bool_true;
}

/* Insert a "hls_pipeline" mark after the innermost "latency" mark.
 * The loop will be eventually pipelined.
 * The "hls_pipeline" mark is placed under the band node.
 */
static __isl_give isl_schedule_node *insert_pipeline_mark(
    __isl_take isl_schedule_node *node, void *user)
{
  struct autosa_kernel *kernel = (struct autosa_kernel *)user;
  isl_ctx *ctx = kernel->ctx;

  if (isl_schedule_node_get_type(node) == isl_schedule_node_mark)
  {
    isl_id *id;

    id = isl_schedule_node_mark_get_id(node);
    if (!strcmp(isl_id_get_name(id), "latency"))
    {
      /* Examine if there is any latency mark inside the current mark. */
      isl_bool no_inner_latency;
      node = isl_schedule_node_child(node, 0);
      no_inner_latency = isl_schedule_node_every_descendant(node,
                                                            &find_latency_mark, NULL);
      node = isl_schedule_node_parent(node);
      if (no_inner_latency)
      {
        /* Insert the "hls_pipeline" mark below the band node. */
        isl_id *hls_id;
        hls_id = isl_id_alloc(ctx, "hls_pipeline", NULL);
        node = isl_schedule_node_child(node, 0);
        node = isl_schedule_node_child(node, 0);
        node = isl_schedule_node_insert_mark(node, hls_id);

        node = isl_schedule_node_parent(node);
        node = isl_schedule_node_parent(node);
      }
    }
    isl_id_free(id);
  }

  return node;
}

/* Insert a "hls_unroll" mark after the "simd" mark.
 * The loop will be eventually unrolled.
 * The "hls_unroll" mark is placed under the band node.
 */
static __isl_give isl_schedule_node *insert_unroll_mark(
    __isl_take isl_schedule_node *node, void *user)
{
  struct autosa_kernel *kernel = (struct autosa_kernel *)user;
  isl_ctx *ctx = kernel->ctx;

  if (isl_schedule_node_get_type(node) == isl_schedule_node_mark)
  {
    isl_id *id;

    id = isl_schedule_node_mark_get_id(node);
    if (!strcmp(isl_id_get_name(id), "simd"))
    {
      isl_id *hls_id;
      hls_id = isl_id_alloc(ctx, "hls_unroll", NULL);
      //if (kernel->options->target == AUTOSA_TARGET_XILINX_HLS_C)
      //{
      node = isl_schedule_node_child(node, 0);
      node = isl_schedule_node_child(node, 0);
      node = isl_schedule_node_insert_mark(node, hls_id);
      node = isl_schedule_node_parent(node);
      node = isl_schedule_node_parent(node);
      //}
      //else if (kernel->options->target == AUTOSA_TARGET_INTEL_OPENCL)
      //{
      //  node = isl_schedule_node_child(node, 0);
      //  node = isl_schedule_node_insert_mark(node, hls_id);
      //  node = isl_schedule_node_parent(node);
      //}
    }
    isl_id_free(id);
  }

  return node;
}

/* Insert a context node at "node" introducing the PE identifiers 
 * along with their bounds, which are stored in kernel->sa_grid_size.
 */
static __isl_give isl_schedule_node *insert_context(struct autosa_kernel *kernel,
                                                    __isl_take isl_schedule_node *node)
{
  isl_set *context;

  context = isl_set_universe(isl_set_get_space(kernel->context));
  context = add_bounded_parameters_dynamic(context,
                                           kernel->sa_grid_size, kernel->pe_ids);
  node = isl_schedule_node_insert_context(node, context);

  return node;
}

/* Create the local buffer variables inside the PE.
 * Specifically, we will also scan through all IO groups for the array,
 * find the lcm of all the data packing factors to set as the array partitioning
 * factor for the local buffer so that all I/O groups should be able to 
 * access the packed elements without any bank conflict.
 */
static void create_pe_module_var(isl_ctx *ctx,
                                 struct autosa_array_ref_group *group,
                                 struct autosa_kernel_var *var, struct autosa_local_array_info *local)
{
  struct autosa_array_tile *tile;
  isl_printer *p;
  isl_val *lcm = isl_val_int_from_si(ctx, 1);

  var->array = group->array;
  var->type = autosa_array_ref_group_type(group);
  var->n_lane = 1;
  /* Scan all the I/O groups, and compute the lcm of the group SIMD factors,
   * set it as the partition factor of the variable. */
  for (int i = 0; i < local->n_io_group; i++)
  {
    struct autosa_array_ref_group *io_group = local->io_groups[i];
    isl_val *val = isl_val_int_from_si(ctx, io_group->n_lane);
    isl_val *product = isl_val_mul(isl_val_copy(val), isl_val_copy(lcm));
    isl_val *gcd = isl_val_gcd(val, lcm);
    lcm = isl_val_div(product, gcd);
  }  
  var->n_part = isl_val_get_num_si(lcm);  
  isl_val_free(lcm);

  tile = autosa_array_ref_group_tile(group);

  p = isl_printer_to_str(ctx);
  p = autosa_array_ref_group_print_name(group, p);
  var->name = isl_printer_get_str(p);
  isl_printer_free(p);

  if (tile == NULL)
  {
    var->size = isl_vec_alloc(ctx, 1);
    var->size = isl_vec_set_element_si(var->size, 0, 1);
  }
  else
  {
    var->size = isl_vec_alloc(ctx, group->array->n_index);
    for (int i = 0; i < group->array->n_index; ++i)
    {
      var->size = isl_vec_set_element_val(var->size, i,
                                          isl_val_copy(tile->bound[i].size));
    }
  }
}

/* Create the local buffer variables inside the PE module. */
static isl_stat create_pe_module_vars(struct autosa_hw_module *module,
                                      struct autosa_kernel *kernel)
{
  int n = 0;
  for (int i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];

    for (int j = 0; j < array->n_pe_group; j++)
    {
      struct autosa_array_ref_group *group = array->pe_groups[j];
      enum autosa_group_access_type type;

      type = autosa_array_ref_group_type(group);
      if (type != AUTOSA_ACCESS_GLOBAL)
        n++;
    }
  }

  module->var = isl_calloc_array(kernel->ctx, struct autosa_kernel_var, n);
  if (!module->var)
    return isl_stat_error;
  module->n_var = n;

  n = 0;
  for (int i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];

    for (int j = 0; j < array->n_pe_group; j++)
    {
      struct autosa_array_ref_group *group = array->pe_groups[j];
      enum autosa_group_access_type type;

      type = autosa_array_ref_group_type(group);
      if (type == AUTOSA_ACCESS_GLOBAL)
        continue;
      create_pe_module_var(kernel->ctx, group, &module->var[n], array);
      n++;
    }
  }

  return isl_stat_ok;
}

/* The "node" is pointed to the "PE" mark.
 */
static __isl_give isl_schedule_node *add_pe_ext_io_copies_dummy(
    struct autosa_kernel *kernel,
    struct autosa_local_array_info *local_array,
    struct autosa_array_ref_group *io_group,
    __isl_take isl_schedule_node *node, int read, int in, int reduce)
{
  isl_union_set *filter = isl_union_set_from_set(isl_set_empty(
      isl_set_get_space(kernel->context)));
  for (int i = 0; i < io_group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = io_group->refs[i];

    if ((io_group->local_array->array_type == AUTOSA_EXT_ARRAY) ||
       ((io_group->local_array->array_type == AUTOSA_INT_ARRAY) && 
       (read && ref->read) || (!read && ref->write)))
    {
      struct autosa_array_ref_group *pe_group = autosa_find_pe_group(
          local_array, io_group, ref);
      struct autosa_add_pe_ext_io_copies_data data =
          {kernel, pe_group, io_group, ref, 1, in, 1, reduce, NULL};
      node = isl_schedule_node_map_descendant_bottom_up(node,
                                                        &add_pe_ext_io_copies_stmt, &data);
      filter = isl_union_set_union(filter, data.filter);
    }
  }

  filter = isl_union_set_coalesce(filter);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_filter(node, filter);
  node = isl_schedule_node_parent(node);
  return node;
}

/* Create the schedule for the PE dummy module that collects/sends the dummy data.
 * If "in" is 1, generate dummy module collects the dummy data.
 * Else, generate dummy module sends the dummy data.
 */
static __isl_give isl_schedule *pe_module_dummy_gen(struct autosa_gen *gen,
                                                    struct autosa_hw_module *module, 
                                                    struct autosa_array_ref_group *group,
                                                    int in)
{
  isl_schedule *schedule;
  isl_schedule_node *node;
  isl_id *id, *hw_id;
  struct autosa_kernel *kernel;

  schedule = gen->schedule;
  schedule = isl_schedule_dup(schedule);
  node = isl_schedule_get_root(schedule);
  isl_schedule_free(schedule);
  node = autosa_tree_move_down_to_kernel(node);

  id = isl_schedule_node_mark_get_id(node);
  kernel = (struct autosa_kernel *)isl_id_get_user(id);
  isl_id_free(id);

  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_child(node, 0);
  node = split_band(node, kernel->n_sa_dim);
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  node = add_pe_ext_io_copies_dummy(
            kernel, group->local_array, group, node, 1, in, 
            gen->options->autosa->local_reduce && group->attached_drain_group);

  /* Insert "pipeline" mark under the last "latency" mark. */
  node = isl_schedule_node_map_descendant_bottom_up(node,
                                                    &insert_pipeline_mark, kernel);

  /* Insert "unroll" mark under the last "simd" mark. */
  node = isl_schedule_node_map_descendant_bottom_up(node,
                                                    &insert_unroll_mark, kernel);

  /* Add module mark after the kernel mark. */
  hw_id = isl_id_alloc(gen->ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, hw_id);

  /* Add the PE id filter. */
  node = autosa_tree_move_up_to_kernel(node);
  isl_schedule_node_child(node, 0);
  node = insert_context(kernel, node);
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_filter(node,
                                         isl_union_set_copy(kernel->pe_filter));

  schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  return schedule;
}

/* Modify the input "schedule" to describe the PE module.
 * Set the schedule dimensions of space loops as parameters.
 *
 * For interior I/O groups
 * - add copy-in before PE computation (RAW, RAR)
 * - add copy-out after PE computation (RAW)
 *   - domain: S -> type[D -> access]
 *   - schedule: type[D -> access] -> tiling
 * For exterior I/O groups
 *   for each access in the group
 *   - add copy-in before user statement (RAW, RAR)
 *   - add copy-out after user statement (RAW, RAR)
 *     - domain: S -> type[D -> access]
 *     - schedule: type[D -> access] -> tiling 
 *       (if any, otherwise, create a register tiling)
 * For WAW group 
 * - for each access in the group
 *   - add write-out after user statement (WAW)
 *     - domain: S -> type[D -> access]
 *     - schedule: type[D -> access] -> tiling
 */
static __isl_give struct autosa_hw_module *sa_pe_module_gen(struct autosa_gen *gen)
{
  isl_schedule_node *node;
  isl_id *id;
  struct autosa_kernel *kernel;
  isl_schedule *schedule, *new_schedule;
  int single_statement;
  isl_union_set *domain;
  struct autosa_hw_module *module;
  isl_id *hw_id;

  module = autosa_hw_module_alloc(gen);

  /* Add the filters for PEs. */
  schedule = gen->schedule;
  schedule = isl_schedule_dup(schedule);
  node = isl_schedule_get_root(schedule);
  node = autosa_tree_move_down_to_kernel(node);

  id = isl_schedule_node_mark_get_id(node);
  kernel = (struct autosa_kernel *)isl_id_get_user(id);
  isl_id_free(id);
  single_statement = kernel->single_statement;
  domain = isl_schedule_node_get_domain(node);

  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_child(node, 0);
  node = split_band(node, kernel->n_sa_dim);
  kernel->pe_ids = ppcg_scop_generate_names(gen->prog->scop,
                                            kernel->n_sa_dim, "p");
  kernel->pe_filter = set_schedule_modulo(node, kernel->pe_ids,
                                          kernel->sa_dim);
  kernel->sa_grid_size = extract_sa_grid_size(kernel, domain);

  /* Add the statements for I/O groups with exterior I/O at the user 
   * statement level. 
   * Add the statements for I/O group with interior I/O at the PE level.
   */
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  /* Add copy-in/copy-out statements */
  for (int i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];
    for (int j = 0; j < array->n_io_group; j++)
    {
      struct autosa_array_ref_group *group = array->io_groups[j];      
      if (group->local_array->array_type == AUTOSA_EXT_ARRAY)
      {
        if (group->pe_io_dir == IO_IN || group->pe_io_dir == IO_INOUT)
          node = add_pe_ext_io_copies(kernel, array, group, node, 1);
        if (group->pe_io_dir == IO_OUT || group->pe_io_dir == IO_INOUT)
          node = add_pe_ext_io_copies(kernel, array, group, node, 0);        
      }
      else if (group->local_array->array_type == AUTOSA_INT_ARRAY)
      {
        if (group->io_type == AUTOSA_INT_IO)
        {
          if (group->pe_io_dir == IO_IN || group->pe_io_dir == IO_INOUT)
            node = add_pe_int_io_copies(kernel, array, group, node, 1);
          if (group->pe_io_dir == IO_OUT || group->pe_io_dir == IO_INOUT)
            node = add_pe_int_io_copies(kernel, array, group, node, 0);          
        }
        else
        {
          if (group->pe_io_dir == IO_IN || group->pe_io_dir == IO_INOUT)
            node = add_pe_ext_io_copies(kernel, array, group, node, 1);
          if (group->pe_io_dir == IO_OUT || group->pe_io_dir == IO_INOUT)
            node = add_pe_ext_io_copies(kernel, array, group, node, 0);          
        }
      }
      module->n_io_group++;
      module->io_groups = (struct autosa_array_ref_group **)realloc(
          module->io_groups,
          module->n_io_group * sizeof(struct autosa_array_ref_group *));
      module->io_groups[module->n_io_group - 1] = group;
    }
    if (array->drain_group && array->drain_group->array_io_dir != IO_NULL)
    {
      node = add_pe_ext_io_copies(kernel, array, array->drain_group, node, 0);

      module->n_io_group++;
      module->io_groups = (struct autosa_array_ref_group **)realloc(
          module->io_groups,
          module->n_io_group * sizeof(struct autosa_array_ref_group *));
      module->io_groups[module->n_io_group - 1] = array->drain_group;
    }
  }

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  /* Insert "pipeline" mark under the last "latency" mark. */
  node = isl_schedule_node_map_descendant_bottom_up(node,
                                                    &insert_pipeline_mark, kernel);

  /* Insert "unroll" mark under the last "simd" mark */
  node = isl_schedule_node_map_descendant_bottom_up(node,
                                                    &insert_unroll_mark, kernel);

  /* Add module mark after the kernel mark. */
  hw_id = isl_id_alloc(gen->ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, hw_id);

  /* Add the PE id filter. */
  node = autosa_tree_move_up_to_kernel(node);
  isl_schedule_node_child(node, 0);
  node = insert_context(kernel, node);
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_filter(node,
                                         isl_union_set_copy(kernel->pe_filter));

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  isl_schedule_free(schedule);
  new_schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

//#ifdef _DEBUG
//  DBGSCHD(stdout, new_schedule, isl_schedule_get_ctx(new_schedule));
//#endif

  module->sched = new_schedule;
  module->type = PE_MODULE;
  module->name = strdup("PE");
  module->inst_ids = isl_id_list_copy(kernel->pe_ids);
  create_pe_module_vars(module, kernel);
  module->kernel = kernel;

  /* For io group with exterior I/O, we create input and output ports for each
   * PE. However, for the first/last PE on the data transfer direction, 
   * the input/output port consumes/produces dummy data. 
   * We add dummy modules to handle these cases to consume the dummy data.
   * 
   * In addition, when local reduce is enabled, the boundary PEs should only take 
   * in init values (i.e., 0), we will also add dummy module for such a case.
   */
  module->n_pe_dummy_modules = 0;
  module->pe_dummy_modules = NULL;
  for (int i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];
    //if (array->array_type == AUTOSA_INT_ARRAY)
    //  continue;
    for (int j = 0; j < array->n_io_group; j++)
    {
      struct autosa_array_ref_group *group = array->io_groups[j];
      if (group->io_type == AUTOSA_INT_IO)
        continue;
      if (group->pe_io_dir != IO_INOUT)
        continue;
      if (group->copy_in == 0 && group->copy_out == 0)
        continue;
      ///* Generate the dummy module */       
      //if (group->copy_in == 0) {
      //  /* Generate the dummy module that produces the dummy data. */
      //  isl_schedule *sched;
      //  sched = pe_module_dummy_gen(gen, module, group, 0);
      //  module->n_pe_dummy_modules++;
      //  module->pe_dummy_modules = 
      //      (struct autosa_pe_dummy_module **)realloc(module->pe_dummy_modules,
      //                                                module->n_pe_dummy_modules * sizeof(struct autosa_pe_dummy_module *));
      //  struct autosa_pe_dummy_module *dummy_module = autosa_pe_dummy_module_alloc();
      //  dummy_module->module = module;
      //  dummy_module->io_group = group;
      //  dummy_module->sched = sched;
      //  dummy_module->in = 0;
      //  module->pe_dummy_modules[module->n_pe_dummy_modules - 1] = dummy_module;
      //}
      //if (group->copy_out == 0) {
      //  /* Generate the dummy module that consumes the dummy data. */ 
      //  isl_schedule *sched;
      //  sched = pe_module_dummy_gen(gen, module, group, 1);
      //  module->n_pe_dummy_modules++;
      //  module->pe_dummy_modules = 
      //      (struct autosa_pe_dummy_module **)realloc(module->pe_dummy_modules,
      //                                                module->n_pe_dummy_modules * sizeof(struct autosa_pe_dummy_module *));
      //  struct autosa_pe_dummy_module *dummy_module = autosa_pe_dummy_module_alloc();
      //  dummy_module->module = module;
      //  dummy_module->io_group = group;
      //  dummy_module->sched = sched;
      //  dummy_module->in = 1;
      //  module->pe_dummy_modules[module->n_pe_dummy_modules - 1] = dummy_module;
      //}

      /* Generate the dummy module. */
      isl_schedule *sched;
      int in = array->array_type == AUTOSA_INT_ARRAY? 0 : 1;

      sched = pe_module_dummy_gen(gen, module, group, in);
      module->n_pe_dummy_modules++;
      module->pe_dummy_modules =
          (struct autosa_pe_dummy_module **)realloc(module->pe_dummy_modules,
                                                    module->n_pe_dummy_modules * sizeof(struct autosa_pe_dummy_module *));
      struct autosa_pe_dummy_module *dummy_module = autosa_pe_dummy_module_alloc();
      dummy_module->module = module;
      dummy_module->io_group = group;
      dummy_module->sched = sched;
      dummy_module->in = in;
      module->pe_dummy_modules[module->n_pe_dummy_modules - 1] = dummy_module;
    }
  }

  return module;
}

/* The input modules are organized in the sequence of:
 * PE module
 * I/O module (copy-in and copy-out)
 * Drain module
 * We will reorder the modules following the below sequence:
 * I/O module (copy-in) 
 * PE module 
 * I/O module (copy-out)
 * Drain module
 * The reason for the re-ordering is for CSim to proceed in Xilinx environment.
 */
static __isl_give struct autosa_hw_module **hw_module_reorder(
    __isl_take struct autosa_hw_module **modules, int n_module)
{
  struct autosa_hw_module **modules_new = (struct autosa_hw_module **)
      malloc(n_module * sizeof(struct autosa_hw_module *));
  int pos = 0;

  /* I/O module (copy-in) */
  for (int i = 0; i < n_module; i++)
  {
    struct autosa_hw_module *module = modules[i];
    if (module->type == IO_MODULE && module->in)
    {
      modules_new[pos] = module;
      pos++;
    }
  }

  /* PE module */
  modules_new[pos] = modules[0];
  pos++;

  /* I/O module (copy-out) */
  for (int i = 0; i < n_module; i++)
  {
    struct autosa_hw_module *module = modules[i];
    if (module->type == IO_MODULE && !module->in)
    {
      modules_new[pos] = module;
      pos++;
    }
  }

  /* Drain module */
  for (int i = 0; i < n_module; i++)
  {
    struct autosa_hw_module *module = modules[i];
    if (module->type == DRAIN_MODULE)
    {
      modules_new[pos] = module;
      pos++;
    }
  }

  free(modules);
  return modules_new;
}

/* Create the schedule that calls all the PE dummy modules.
 * We will work on the transformed IO schedule for the io group.
 * We delete the schedule nodes above the array mark and below the PE mark,
 * add a filter to only consider the last module in the transfer chain.
 * Then insert the module call extension nodes right under the space bands.
 */
static __isl_give isl_schedule *pe_dummy_gen_module_call(struct autosa_gen *gen,
                                                         struct autosa_pe_dummy_module *pe_dummy_module)
{
  struct autosa_array_ref_group *group;
  isl_schedule *sched;
  isl_schedule_node *node;
  struct autosa_kernel *kernel;
  struct autosa_hw_module *module;
  int n_member;
  isl_union_set *L1_filter;
  isl_bool insert_L1 = isl_bool_false;
  isl_printer *p_str;
  isl_ctx *ctx;
  char *stmt_name;
  isl_id *id;
  isl_union_map *prefix, *extension;
  isl_union_set *domain, *range;

  module = pe_dummy_module->module;
  kernel = module->kernel;
  ctx = gen->ctx;
  group = pe_dummy_module->io_group;
  sched = isl_schedule_dup(group->io_L1_schedule);
  node = isl_schedule_get_root(sched);
  isl_schedule_free(sched);
  isl_space *space;
  isl_union_set *empty_filter;
  isl_schedule_node *graft;  
  int lower_band_num = -1;

  /* Delete the node above the array mark. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_parent(node);
  while (!(autosa_tree_node_is_kernel(node) || isl_schedule_node_get_type(node) == isl_schedule_node_context)) {
    node = isl_schedule_node_delete(node);
    node = isl_schedule_node_parent(node);
  }

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp") && pe_dummy_module->in == 0) {
//    printf("here\n");
//    printf("group id: %d\n", group->nr);
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//    isl_schedule *sched_tmp = isl_schedule_node_get_schedule(node);
//    print_code(gen, isl_schedule_copy(sched_tmp), "U_tmp_out.c");
//    isl_schedule_free(sched_tmp);
//  }
//#endif

  /* Insert a filter. */
  node = autosa_tree_move_down_to_mark(node, kernel->core, "io_L1");
  node = isl_schedule_node_parent(node);
  n_member = isl_schedule_node_band_n_member(node);
  if (n_member > 1)
  {
    node = isl_schedule_node_band_split(node, n_member - 1);
    node = isl_schedule_node_child(node, 0);
  }
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  {
    if (pe_dummy_module->in)
      L1_filter = schedule_eq_ub(node);
    else
      L1_filter = schedule_eq_lb(node);    
    insert_L1 = isl_bool_true;
  }

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp") && pe_dummy_module->in == 0) {
//    DBGUSET(stdout, L1_filter, gen->ctx);
//  }
//#endif

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp") && !pe_dummy_module->in)
//    DBGUSET(stdout, L1_filter, isl_schedule_node_get_ctx(node));
//#endif

  node = autosa_tree_move_down_to_mark(node, kernel->core, "io_L1");
  node = isl_schedule_node_child(node, 0);
  if (insert_L1)
  {
    node = isl_schedule_node_insert_filter(node, L1_filter);
  }

  /* Delete the node under the pe mark. */
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  node = isl_schedule_node_cut(node);

  /* Make the ancestors atomic */
  node = autosa_atomic_ancestors(node);

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp") && pe_dummy_module->in == 0) {
//    printf("here\n");
//    printf("group id: %d\n", group->nr);
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//    isl_schedule *sched_tmp = isl_schedule_node_get_schedule(node);
//    print_code(gen, isl_schedule_copy(sched_tmp), "U_tmp_out2.c");
//    isl_schedule_free(sched_tmp);
//  }
//#endif

  /* Test if the range of the last dimension contains single element */
  lower_band_num = get_last_sched_dim_val(node);

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp") && pe_dummy_module->in) {
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//  }
//#endif

  /* Graft an extension node. */
  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  domain = isl_union_map_range(prefix);

  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "module_call.");
  p_str = autosa_array_ref_group_print_prefix(group, p_str);
  p_str = isl_printer_print_str(p_str, "_PE_dummy");
  p_str = isl_printer_print_str(p_str, pe_dummy_module->in? "_in" : "_out");
  p_str = isl_printer_print_str(p_str, ".0.0");
  if (lower_band_num != -1) {
    p_str = isl_printer_print_str(p_str, ".");
    p_str = isl_printer_print_int(p_str, lower_band_num);
  }
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  space = isl_space_set_alloc(ctx, 0, 1);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name);
  free(stmt_name);

  isl_point *pnt = isl_point_zero(space);
  isl_set *set = isl_set_from_point(pnt);
  range = isl_union_set_from_set(isl_set_copy(set));
  extension = isl_union_map_from_domain_and_range(domain, range);
  graft = isl_schedule_node_from_extension(extension);

  isl_map *map = isl_set_identity(set);
  map = isl_map_reset_tuple_id(map, isl_dim_out);
  isl_union_map *umap = isl_union_map_from_map(map);
  isl_multi_union_pw_aff *mupa = isl_multi_union_pw_aff_from_union_map(umap);

  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);
  graft = ppcg_set_schedule_node_type(graft, isl_ast_loop_atomic);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, graft, isl_schedule_node_get_ctx(node));
//#endif

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);

  /* Insert an empty filter. */
  empty_filter = isl_union_set_from_set(isl_set_empty(
      isl_set_get_space(kernel->context)));
  node = isl_schedule_node_insert_filter(node, empty_filter);

  /* Add module mark after the kernel mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  /* Add pe_dummy module mark after the module mark. */
  id = isl_id_alloc(ctx, "pe_dummy_module", pe_dummy_module);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  sched = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  return sched;
}

/* Create the schedule that calls all the PE modules.
 * We delete the schedule nodes above the array mark and below the PE mark,
 * then insert the module call extension nodes right under the space bands.
 */
static isl_stat top_module_pe_gen_module_call(struct autosa_gen *gen,
                                              struct autosa_hw_top_module *top, struct autosa_hw_module *module)
{
  isl_schedule *schedule;
  isl_schedule_node *node, *graft;
  isl_id *id;
  struct autosa_kernel *kernel = gen->kernel;
  isl_space *space;
  isl_ctx *ctx;
  isl_union_set *domain;
  isl_union_set *empty_filter;
  isl_printer *p_str;
  char *stmt_name;

  schedule = gen->schedule;
  schedule = isl_schedule_dup(schedule);
  node = isl_schedule_get_root(schedule);
  isl_schedule_free(schedule);
  ctx = isl_schedule_node_get_ctx(node);

  /* Delete the node above the array mark. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_parent(node);
  while (!autosa_tree_node_is_kernel(node))
  {
    node = isl_schedule_node_delete(node);
    node = isl_schedule_node_parent(node);
  }

  /* Delete the node under the pe mark. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_child(node, 0);
  node = split_band(node, kernel->n_sa_dim);

  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_cut(node);

  /* Graft an extension node. */
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "module_call.");
  p_str = isl_printer_print_str(p_str, module->name);
  p_str = isl_printer_print_str(p_str, ".0.0");
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name);
  free(stmt_name);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft = isl_schedule_node_from_domain(domain);

  node = isl_schedule_node_graft_before(node, graft);

  /* Insert an empty filter */
  empty_filter = isl_union_set_from_set(isl_set_empty(
      isl_set_get_space(kernel->context)));
  node = isl_schedule_node_insert_filter(node, empty_filter);

  /* Add module mark after the kernel mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  top->n_module_calls++;
  top->module_call_scheds = (isl_schedule **)realloc(top->module_call_scheds,
                                                     top->n_module_calls * sizeof(isl_schedule *));
  top->module_call_scheds[top->n_module_calls - 1] = schedule;

  if (module->n_pe_dummy_modules > 0)
  {
    int inserted = 0;
    /* Generate dummy module calls. */
    for (int i = 0; i < module->n_pe_dummy_modules; i++)
    {
      struct autosa_pe_dummy_module *pe_dummy_module;
      isl_schedule *sched;

      pe_dummy_module = module->pe_dummy_modules[i];
      sched = pe_dummy_gen_module_call(gen, pe_dummy_module);

      top->n_module_calls++;
      top->module_call_scheds = (isl_schedule **)realloc(top->module_call_scheds,
                                                         top->n_module_calls * sizeof(isl_schedule *));
      /* If the module is out, we need to place it before the PE module call. */
      if (!pe_dummy_module->in) {        
        for (int j = top->n_module_calls - 2; j >= top->n_module_calls - 1 - inserted - 1; j--)
          top->module_call_scheds[j + 1] = top->module_call_scheds[j];
        top->module_call_scheds[top->n_module_calls - 1 - inserted - 1] = sched;
      } else {
        top->module_call_scheds[top->n_module_calls - 1] = sched;
      }
      inserted++;
    }
  }

  return isl_stat_ok;
}

/* Generate the schedule that declares the fifos used in PEs. 
 * If the io group data transfer direciton at the PE level is INOUT,
 * we will add another extension node at the boundary of the transfer chain
 * to declare one more fifo.
 */
static isl_stat top_module_pe_gen_fifo_decl(struct autosa_gen *gen,
                                            struct autosa_hw_top_module *top, struct autosa_hw_module *module)
{
  isl_schedule *schedule;
  isl_schedule_node *node, *graft;
  isl_id *id;
  struct autosa_kernel *kernel = gen->kernel;
  isl_space *space;
  isl_ctx *ctx = gen->ctx;
  isl_union_set *domain;
  isl_union_set *empty_filter;
  isl_printer *p_str;
  char *stmt_name;

  for (int i = 0; i < module->n_io_group; i++)
  {
    struct autosa_array_ref_group *group = module->io_groups[i];
    isl_multi_aff *io_trans;
    isl_mat *io_trans_mat;
    isl_id *id;
    isl_union_set *L1_filter = NULL;
    bool insert_L1 = isl_bool_false;
    if (group->pe_io_dir == IO_NULL)
      continue;

    schedule = isl_schedule_dup(group->io_L1_schedule);
    node = isl_schedule_get_root(schedule);
    isl_schedule_free(schedule);

    /* Delete the node above the array mark. */
    node = autosa_tree_move_down_to_array(node, kernel->core);
    node = isl_schedule_node_parent(node);
    while (!autosa_tree_node_is_kernel(node))
    {
      node = isl_schedule_node_delete(node);
      node = isl_schedule_node_parent(node);
    }

    if (group->pe_io_dir == IO_INOUT)
    {
      int n_member;
      node = autosa_tree_move_down_to_mark(node, kernel->core, "io_L1");
      node = isl_schedule_node_parent(node);
      n_member = isl_schedule_node_band_n_member(node);
      node = isl_schedule_node_band_split(node, n_member - 1);
      node = isl_schedule_node_child(node, 0);
      if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
      {
        L1_filter = schedule_eq_ub(node);
        insert_L1 = isl_bool_true;
      }
      node = autosa_tree_move_up_to_array(node);
    }

    /* Delete the node under the pe mark. */
    node = autosa_tree_move_down_to_pe(node, kernel->core);
    node = isl_schedule_node_cut(node);

    /* Graft an extension node. */
    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "fifo_decl.");
    p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
    stmt_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    space = isl_space_set_alloc(ctx, 0, 0);
    id = isl_id_alloc(ctx, stmt_name, group);
    space = isl_space_set_tuple_id(space, isl_dim_set, id);
    free(stmt_name);
    domain = isl_union_set_from_set(isl_set_universe(space));
    graft = isl_schedule_node_from_domain(domain);

    node = isl_schedule_node_graft_before(node, graft);

    if (insert_L1)
    {
      isl_set *set;
      isl_multi_union_pw_aff *mupa;
      isl_union_map *prefix;
      isl_union_set *domain;
      isl_union_set *range;
      isl_union_map *extension;
      isl_map *map;
      isl_union_map *umap;

      /* Graft an extension node for boundary PE. */
      node = isl_schedule_node_insert_filter(node, L1_filter);
      node = isl_schedule_node_child(node, 0);
      prefix = isl_schedule_node_get_prefix_schedule_relation(node);
      prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                                isl_union_pw_multi_aff_copy(kernel->contraction));
      domain = isl_union_map_range(prefix);

      p_str = isl_printer_to_str(ctx);
      p_str = isl_printer_print_str(p_str, "fifo_decl_boundary.");
      p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
      stmt_name = isl_printer_get_str(p_str);
      isl_printer_free(p_str);
      space = isl_space_set_alloc(ctx, 0, 1);
      id = isl_id_alloc(ctx, stmt_name, group);
      space = isl_space_set_tuple_id(space, isl_dim_set, id);
      free(stmt_name);

      isl_point *pnt = isl_point_zero(space);
      set = isl_set_from_point(pnt);
      range = isl_union_set_from_set(isl_set_copy(set));

      extension = isl_union_map_from_domain_and_range(domain, range);
      graft = isl_schedule_node_from_extension(extension);

      map = isl_set_identity(set);
      map = isl_map_reset_tuple_id(map, isl_dim_out);
      umap = isl_union_map_from_map(map);
      mupa = isl_multi_union_pw_aff_from_union_map(umap);

      graft = isl_schedule_node_child(graft, 0);
      graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

      while (graft && isl_schedule_node_has_parent(graft))
        graft = isl_schedule_node_parent(graft);

      node = isl_schedule_node_graft_before(node, graft);
    }
    else
    {
      isl_union_set_free(L1_filter);
    }

    /* Insert an empty filter. */
    empty_filter = isl_union_set_from_set(isl_set_empty(
        isl_set_get_space(kernel->context)));
    node = isl_schedule_node_insert_filter(node, empty_filter);

    /* Add module mark after the kernel mark. */
    id = isl_id_alloc(ctx, "module", module);
    node = autosa_tree_move_up_to_kernel(node);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_insert_mark(node, id);

    schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);

    top->n_fifo_decls++;
    top->fifo_decl_scheds = (isl_schedule **)realloc(top->fifo_decl_scheds,
                                                     top->n_fifo_decls * sizeof(isl_schedule *));
    top->fifo_decl_scheds[top->n_fifo_decls - 1] = schedule;
    top->fifo_decl_names = (char **)realloc(top->fifo_decl_names,
                                            top->n_fifo_decls * sizeof(char *));
    /* Generate fifo_decl name in the format of 
     * [fifo_name].[fifo_width] 
     */
    p_str = isl_printer_to_str(ctx);
    p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
    p_str = isl_printer_print_str(p_str, "_");
    p_str = isl_printer_print_str(p_str, module->name);
    p_str = isl_printer_print_str(p_str, ".");
    int n_lane = get_io_group_n_lane(module, NULL, group);
    int data_size = group->array->size;
    int width = data_size * n_lane; // in bytes
    p_str = isl_printer_print_int(p_str, width);
    top->fifo_decl_names[top->n_fifo_decls - 1] = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
  }

  return isl_stat_ok;
}

/* Generate module calls and fifo decls for the PE module. 
 */
static isl_stat top_module_pe_gen(struct autosa_gen *gen,
                                  struct autosa_hw_top_module *top, struct autosa_hw_module *module)
{
  /* Generate the function call schedule. */
  top_module_pe_gen_module_call(gen, top, module);

  /* Generate the fifo declaration schedule. */
  top_module_pe_gen_fifo_decl(gen, top, module);

  return isl_stat_ok;
}

/* The input "node" points to the node below io_[module->level] mark.
 * Return the node points to the "kernel" mark.
 * We will insert two module call extension nodes: 
 * module_call_upper: which contains the module name and arguments for the 
 * inter-module transfer
 * module_call_lower: which contains arguments for the intra-module transfer
 * (i.e., transfer to the lower-level modules)
 */
static __isl_give isl_schedule_node *io_gen_module_call(
    __isl_take isl_schedule_node *node, struct autosa_hw_module *module,
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    int boundary, int serialize,
    __isl_take isl_union_set *filter_domain)
{
  isl_printer *p_str;
  char *stmt_name;
  isl_space *space;
  isl_union_set *domain, *empty_filter, *lower_level_filter;
  isl_schedule_node *graft;
  isl_bool insert_lower = isl_bool_false;
  isl_ctx *ctx = isl_schedule_node_get_ctx(node);
  isl_id *id;
  isl_union_map *prefix, *extension, *umap;
  isl_union_set *range;
  isl_set *set;
  isl_map *map;
  isl_multi_union_pw_aff *mupa;
  int lower_band_num = -1;  
  isl_union_set *filter_range;
  isl_bool upper_inserted;

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  /* Collect the filter for the lower I/O module. */
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  {
    if (module->level > 1)
    {
      if (module->to_pe) {
        if (module->in)
          lower_level_filter = schedule_eq_lb(node);
        else
          lower_level_filter = schedule_eq_ub(node);
      } else {
        lower_level_filter = schedule_eq_lb(node);
      }
      
      insert_lower = isl_bool_true;
    }
  }

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  //node = isl_schedule_node_child(node, 0);
  //node = isl_schedule_node_insert_filter(node, filter_domain);
  //node = isl_schedule_node_child(node, 0);

  /* Graft an extension node for module call. */
  prefix = isl_schedule_node_get_prefix_schedule_relation(node);  
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  //filter_domain = isl_union_set_apply(filter_domain, isl_union_map_copy(prefix));  
  domain = isl_union_map_range(isl_union_map_copy(prefix));
//#ifdef _DEBUG
//  DBGUSET(stdout, filter_domain, ctx);
//  DBGUMAP(stdout, prefix, ctx);
//#endif
  if (filter_domain) {
    filter_range = isl_union_set_apply(isl_union_set_copy(filter_domain), isl_union_map_copy(prefix));
//#ifdef _DEBUG
//    DBGUSET(stdout, filter_range, ctx);
//    DBGUSET(stdout, domain, ctx);
//#endif
    domain = isl_union_set_intersect(domain, filter_range);
  }
  isl_union_map_free(prefix);

//#ifdef _DEBUG
//  std::cout << module->name << std::endl;
//  DBGUSET(stdout, domain, isl_union_set_get_ctx(domain));
//#endif

  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "module_call_upper.");
  p_str = isl_printer_print_str(p_str, module->name);
  //if (boundary)
  //  p_str = isl_printer_print_str(p_str, ".boundary");
  if (boundary) 
    p_str = isl_printer_print_str(p_str, ".1");
  else
    p_str = isl_printer_print_str(p_str, ".0");
  if (serialize)
    p_str = isl_printer_print_str(p_str, ".1");
  else
    p_str = isl_printer_print_str(p_str, ".0");

  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  space = isl_space_set_alloc(ctx, 0, 1);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name);
  free(stmt_name);

  isl_point *pnt = isl_point_zero(space);
  set = isl_set_from_point(pnt);
  range = isl_union_set_from_set(isl_set_copy(set));

  extension = isl_union_map_from_domain_and_range(domain, range);
  graft = isl_schedule_node_from_extension(extension);

  map = isl_set_identity(set);
  map = isl_map_reset_tuple_id(map, isl_dim_out);
  umap = isl_union_map_from_map(map);
  mupa = isl_multi_union_pw_aff_from_union_map(umap);

  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);

  if (module->level > 1)
  {
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level - 1);
  }
  node = isl_schedule_node_cut(node);

  /* Graft an extension node for lower level transfer. */
  if (insert_lower)
  {    
    if (module->to_pe) {
      node = isl_schedule_node_insert_filter(node, lower_level_filter);
      node = isl_schedule_node_child(node, 0);
    } else {
      /* In case the lower band only contains one element, we will compute the 
       * value and append to the module_call name.
       */
      isl_schedule_node *node_tmp;
      node_tmp = isl_schedule_node_copy(node);
      node_tmp = isl_schedule_node_parent(node_tmp); // band
      node_tmp = isl_schedule_node_insert_filter(node_tmp, isl_union_set_copy(lower_level_filter));
      node_tmp = isl_schedule_node_child(node_tmp, 0);
      lower_band_num = get_band_single_schedule_val(node_tmp);
      isl_schedule_node_free(node_tmp);

//#ifdef _DEBUG
//      if (!strcmp(module->name, "U_drain_IO_L2_out")) {
//        printf("test %d\n", lower_band_num);
//      }
//#endif 

      node = isl_schedule_node_insert_filter(node, lower_level_filter);
      node = isl_schedule_node_child(node, 0);

      //node = isl_schedule_node_parent(node); // band
      //node = isl_schedule_node_insert_filter(node, lower_level_filter);
      //node = isl_schedule_node_child(node, 0);      
      //lower_band_num = get_band_single_schedule_val(node);     
      //node = isl_schedule_node_child(node, 0);
    }
  }
  {
    isl_union_map *prefix;
    isl_union_set *domain, *range;
    isl_point *pnt;
    isl_set *set;
    isl_union_map *extension, *umap;
    isl_map *map;
    isl_multi_union_pw_aff *mupa;

    prefix = isl_schedule_node_get_prefix_schedule_relation(node);
    prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                              isl_union_pw_multi_aff_copy(kernel->contraction));
    domain = isl_union_map_range(isl_union_map_copy(prefix));
    if (filter_domain) {
      filter_range = isl_union_set_apply(isl_union_set_copy(filter_domain), isl_union_map_copy(prefix));    
      domain = isl_union_set_intersect(domain, filter_range);     
    }
    isl_union_map_free(prefix);
//#ifdef _DEBUG
//    std::cout << module->name << std::endl;
//    std::cout << lower_band_num << std::endl;
//    DBGUSET(stdout, domain, isl_union_set_get_ctx(domain));
//#endif    

    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "module_call_lower.");
    p_str = isl_printer_print_str(p_str, module->name);
    //if (boundary)
    //  p_str = isl_printer_print_str(p_str, ".boundary");
    if (boundary) 
      p_str = isl_printer_print_str(p_str, ".1");
    else
      p_str = isl_printer_print_str(p_str, ".0");
    if (serialize)
      p_str = isl_printer_print_str(p_str, ".1");
    else
      p_str = isl_printer_print_str(p_str, ".0");

    if (lower_band_num != -1) {
      p_str = isl_printer_print_str(p_str, ".");
      p_str = isl_printer_print_int(p_str, lower_band_num);
    }

    stmt_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    space = isl_space_set_alloc(ctx, 0, 1);
    id = isl_id_alloc(ctx, stmt_name, group);
    space = isl_space_set_tuple_id(space, isl_dim_set, id);
    free(stmt_name);

    pnt = isl_point_zero(space);
    set = isl_set_from_point(pnt);
    range = isl_union_set_from_set(isl_set_copy(set));

    /* Build an identical union map from domain.
     * Project out the range dims and only keep the last dim.
     * Set the range name as stmt_name. */    
    extension = isl_union_map_from_domain_and_range(domain, range);
    graft = isl_schedule_node_from_extension(extension);

    map = isl_set_identity(set);
    map = isl_map_reset_tuple_id(map, isl_dim_out);
    umap = isl_union_map_from_map(map);
    mupa = isl_multi_union_pw_aff_from_union_map(umap);

    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

    while (graft && isl_schedule_node_has_parent(graft))
      graft = isl_schedule_node_parent(graft);

    node = isl_schedule_node_graft_after(node, graft);

//    if (!strcmp(module->name, "U_drain_IO_L2_out")) {
//      DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//    }    
  }

  /* Insert an empty filter. */
  empty_filter = isl_union_set_from_set(isl_set_empty(isl_set_get_space(kernel->context)));
  node = isl_schedule_node_insert_filter(node, empty_filter);

  node = autosa_tree_move_up_to_kernel(node);
  isl_union_set_free(filter_domain);

  return node;
}

/* The input "node" points to the node below io_[module->level] mark.
 * Return the node points to the "kernel" mark.
 * We will insert one module call extension node: 
 * module_call_upper: which contains the module name and arguments for the 
 * inter-module transfer
 * This function is used for Intel OpenCL only. We will not generate 
 * the module_call_lower, which is define as below:
 * module_call_lower: which contains arguments for the intra-module transfer
 * (i.e., transfer to the lower-level modules)
 */
static __isl_give isl_schedule_node *io_gen_ext_module(
    __isl_take isl_schedule_node *node, struct autosa_hw_module *module,
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    int boundary)
{
  isl_printer *p_str;
  char *stmt_name;
  isl_space *space;
  isl_union_set *domain, *empty_filter, *lower_level_filter;
  isl_schedule_node *graft;
  isl_bool insert_lower = isl_bool_false;
  isl_ctx *ctx = isl_schedule_node_get_ctx(node);
  isl_id *id;
  isl_union_map *prefix, *extension, *umap;
  isl_union_set *range;
  isl_set *set;
  isl_map *map;
  isl_multi_union_pw_aff *mupa;

  /* Graft an extension node for module call. */
  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  domain = isl_union_map_range(prefix);

  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "ext_module_upper.");
  p_str = isl_printer_print_str(p_str, module->name);
  if (boundary)
    p_str = isl_printer_print_str(p_str, ".boundary");
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  space = isl_space_set_alloc(ctx, 0, 0);
  space = isl_space_set_tuple_name(space, isl_dim_set, stmt_name);
  free(stmt_name);

  isl_point *pnt = isl_point_zero(space);
  set = isl_set_from_point(pnt);
  range = isl_union_set_from_set(isl_set_copy(set));

  extension = isl_union_map_from_domain_and_range(domain, range);
  graft = isl_schedule_node_from_extension(extension);

  map = isl_set_identity(set);
  map = isl_map_reset_tuple_id(map, isl_dim_out);
  umap = isl_union_map_from_map(map);
  mupa = isl_multi_union_pw_aff_from_union_map(umap);

  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

  while (graft && isl_schedule_node_has_parent(graft))
    graft = isl_schedule_node_parent(graft);

  node = isl_schedule_node_graft_before(node, graft);
  node = isl_schedule_node_cut(node);

  /* Insert an empty filter. */
  empty_filter = isl_union_set_from_set(isl_set_empty(isl_set_get_space(kernel->context)));
  node = isl_schedule_node_insert_filter(node, empty_filter);

  node = autosa_tree_move_up_to_kernel(node);

  return node;
}

/* Generate the calls for the io module connected to the external memory. 
 * This function is used for Intel OpenCL only.
 * Since all fifos will be replaced with channels later, this function only 
 * generates the upper module calls, ignoring the lower module call.
 */
static isl_stat top_module_io_gen_ext_module(
    struct autosa_gen *gen, struct autosa_hw_top_module *top,
    struct autosa_hw_module *module,
    struct autosa_array_ref_group *group)
{
  isl_schedule *schedule;
  isl_ctx *ctx = gen->ctx;
  isl_schedule_node *node, *graft;
  isl_id *id;
  struct autosa_kernel *kernel = gen->kernel;
  isl_printer *p_str;
  char *stmt_name;
  isl_space *space;
  isl_union_set *domain, *empty_filter, *lower_level_filter;
  isl_bool insert_lower = isl_bool_false;
  int boundary = module->boundary;
  isl_union_set *boundary_filter, *non_boundary_filter;
  isl_union_set_list *boundary_filters;

  /* Only the top-level io module connected to the external memory is handled.
   */
  if (module->type == PE_MODULE || module->to_mem == 0)
    return isl_stat_ok;

  /* Transform the schedule. */
  schedule = isl_schedule_dup(group->io_schedule);
  node = isl_schedule_get_root(schedule);
  isl_schedule_free(schedule);

  /* Delete the node above the array mark. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_parent(node);
  while (!autosa_tree_node_is_kernel(node))
  {
    node = isl_schedule_node_delete(node);
    node = isl_schedule_node_parent(node);
  }

  /* Collect the filter for the boundary and non-boundary I/O module. */
  if (boundary)
  {
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
    node = isl_schedule_node_parent(node);
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
      boundary_filter = schedule_eq_ub(node);
      non_boundary_filter = schedule_neq_ub(node);
      boundary_filters = isl_union_set_list_from_union_set(non_boundary_filter);
      boundary_filters = isl_union_set_list_add(boundary_filters, boundary_filter);

      node = isl_schedule_node_child(node, 0); // io_mark
      node = isl_schedule_node_child(node, 0); // band
      node = isl_schedule_node_insert_sequence(node, boundary_filters);
      /* The node now is right below the io_[module->level] mark. */
    }
  }
  else
  {
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
    node = isl_schedule_node_child(node, 0);
  }

  if (boundary)
  {
    node = isl_schedule_node_child(node, 0); // filter
    node = isl_schedule_node_child(node, 0); // band
    /* non-boundary */
    node = io_gen_ext_module(node, module, kernel, group, 0);
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
    node = isl_schedule_node_child(node, 0); // sequence
    node = isl_schedule_node_child(node, 1); // filter
    node = isl_schedule_node_child(node, 0); // band
    /* boundary */
    node = io_gen_ext_module(node, module, kernel, group, 1);
  }
  else
  {
    node = io_gen_ext_module(node, module, kernel, group, 0);
  }

  /* Cleanup the schedule tree. Remove "array" and "io_LX" mark.
   */
  node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
  node = isl_schedule_node_delete(node);
  node = autosa_tree_move_up_to_array(node);
  node = isl_schedule_node_delete(node);
  node = autosa_tree_move_up_to_kernel(node);

  /* Add module mark after the kernel mark.auto */
  id = isl_id_alloc(ctx, "module", module);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);  

  schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  top->n_ext_module++;
  top->ext_module_scheds = (isl_schedule **)realloc(top->ext_module_scheds,
                                                    top->n_ext_module * sizeof(isl_schedule *));
  top->ext_module_scheds[top->n_ext_module - 1] = schedule;

  return isl_stat_ok;
}

/* Generate the module calls for the io module. 
 * If serialize is set as 1, we are generating the extra serialization module.
 */
static isl_stat top_module_io_gen_module_call(
    struct autosa_gen *gen, struct autosa_hw_top_module *top,
    struct autosa_hw_module *module,
    struct autosa_array_ref_group *group,
    int serialize)
{
  isl_schedule *schedule;
  isl_ctx *ctx = gen->ctx;
  isl_schedule_node *node, *graft;
  isl_id *id;
  struct autosa_kernel *kernel = gen->kernel;
  isl_printer *p_str;
  char *stmt_name;
  isl_space *space;
  isl_union_set *domain, *empty_filter, *lower_level_filter;
  isl_bool insert_lower = isl_bool_false;
  int boundary = module->boundary;
  isl_union_set *boundary_filter, *non_boundary_filter;
  isl_union_set_list *boundary_filters;
  isl_union_set *group_domain_filter;
  int single_ele = -1;
  isl_union_set *group_domain_filter_level;

//#ifdef _DEBUG
//  std::cout << "module name: " << module->name << std::endl;
//  if (!strcmp(module->name, "A_IO_L2_in")) 
//    printf("here\n");
//#endif

  /* Transform the schedule. */
  schedule = isl_schedule_dup(group->io_schedule);
  node = isl_schedule_get_root(schedule);
  isl_schedule_free(schedule);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  /* Compute the union of domains of all the array references in the group. */
  group_domain_filter = compute_io_group_domain(node, group, kernel, gen, module->in);  
  group_domain_filter = extend_io_group_domain(group_domain_filter, node, group, kernel, module->level);  
  group_domain_filter_level = compute_io_group_domain_at_level(group_domain_filter, node, group, kernel, module->level);    

  /* Delete the node above the array mark. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_parent(node);  
  while (!(autosa_tree_node_is_kernel(node) || isl_schedule_node_get_type(node) == isl_schedule_node_context)) {
    node = isl_schedule_node_delete(node);
    node = isl_schedule_node_parent(node);
  }

  node = autosa_tree_move_up_to_kernel(node);

  /* Collect the filter for the boundary and non-boundary I/O module. */
  if (boundary && (module->level <= group->space_dim))
  {
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
    node = isl_schedule_node_parent(node);
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
      /* Test if the band only contains one elmenet */
      isl_schedule_node *node_tmp;      
      node_tmp = isl_schedule_node_copy(node);
      if (group_domain_filter_level) {
        node_tmp = isl_schedule_node_insert_filter(node_tmp, isl_union_set_copy(group_domain_filter_level));
        node_tmp = isl_schedule_node_child(node_tmp, 0);
      }
      single_ele = get_band_single_schedule_val(node_tmp);
      if (single_ele == -1) {
        boundary_filter = schedule_eq_ub(node_tmp);
        non_boundary_filter = schedule_neq_ub(node_tmp);
      }
      isl_schedule_node_free(node_tmp);

//#ifdef _DEBUG
//      if (!strcmp(module->name, "U_drain_IO_L2_out")) {
//        printf("single ele: %d\n", single_ele);
//        DBGUSET(stdout, boundary_filter, ctx);
//        DBGUSET(stdout, non_boundary_filter, ctx);
//      }
//#endif

      if (single_ele == -1) {
        //boundary_filter = schedule_eq_ub(node);
        //non_boundary_filter = schedule_neq_ub(node);
//#ifdef _DEBUG
//        if (!strcmp(module->name, "A_IO_L2_in")) {
//          printf("single ele: %d\n", single_ele);
//          DBGUSET(stdout, boundary_filter, ctx);
//          DBGUSET(stdout, non_boundary_filter, ctx);
//        }
//#endif
        boundary_filters = isl_union_set_list_from_union_set(non_boundary_filter);
        boundary_filters = isl_union_set_list_add(boundary_filters, boundary_filter);        

        node = isl_schedule_node_child(node, 0); // io_mark
        node = isl_schedule_node_child(node, 0); // band      
        node = isl_schedule_node_insert_sequence(node, boundary_filters);
        /* The node now is right below the io_[module->level] mark. */      
      } else {
        node = isl_schedule_node_child(node, 0); // io_mark
        node = isl_schedule_node_child(node, 0); // band
        node = isl_schedule_node_insert_filter(node, isl_union_set_copy(group_domain_filter_level));
        node = isl_schedule_node_child(node, 0); // band
      }
    }
  }
  else
  {
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
    node = isl_schedule_node_child(node, 0);
  }

  if (boundary && (module->level <= group->space_dim))
  {
//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, ctx);
//#endif
    if (single_ele == -1) {
      node = isl_schedule_node_child(node, 0); // filter
      node = isl_schedule_node_child(node, 0); // band
      
      //if (single_ele != -1) {
      //  /* boundary */
      //  node = io_gen_module_call(node, module, kernel, group, 1, serialize, isl_union_set_copy(group_domain_filter));  
      //} else {
      //  /* non-boundary */
      //  node = io_gen_module_call(node, module, kernel, group, 0, serialize, isl_union_set_copy(group_domain_filter));
      //}
      /* non-boundary */
      node = io_gen_module_call(node, module, kernel, group, 0, serialize, isl_union_set_copy(group_domain_filter));
      node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
      node = isl_schedule_node_child(node, 0); // sequence
      node = isl_schedule_node_child(node, 1); // filter
      node = isl_schedule_node_child(node, 0); // band
  
      /* boundary */
      node = io_gen_module_call(node, module, kernel, group, 1, serialize, isl_union_set_copy(group_domain_filter));
    } else {
      /* boundary */
      node = io_gen_module_call(node, module, kernel, group, 1, serialize, isl_union_set_copy(group_domain_filter));
    }
  }
  else 
  {
    node = io_gen_module_call(node, module, kernel, group, boundary, serialize, isl_union_set_copy(group_domain_filter));
  }

  /* Add module mark after the kernel mark.auto */
  id = isl_id_alloc(ctx, "module", module);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);
  isl_union_set_free(group_domain_filter);
  isl_union_set_free(group_domain_filter_level);

  top->n_module_calls++;
  top->module_call_scheds = (isl_schedule **)realloc(top->module_call_scheds,
                                                     top->n_module_calls * sizeof(isl_schedule *));
  top->module_call_scheds[top->n_module_calls - 1] = schedule;

  return isl_stat_ok;
}

/* Generate fifo decls for the I/O module.
 * Currently only works for filter I/O modules.
 */
static isl_stat top_module_io_gen_fifo_decl(struct autosa_gen *gen,
                                            struct autosa_hw_top_module *top,
                                            struct autosa_hw_module *module, struct autosa_array_ref_group *group)
{
  isl_schedule *schedule;
  isl_schedule_node *node, *graft;
  isl_union_set *filter = NULL, *empty_filter;
  struct autosa_kernel *kernel = gen->kernel;
  bool insert_filter = isl_bool_false;
  char *stmt_name;
  isl_space *space;
  isl_union_set *domain;
  isl_printer *p_str;
  isl_id *id;
  isl_ctx *ctx = gen->ctx;

  if (module->to_mem)
    return isl_stat_ok;

  schedule = isl_schedule_dup(group->io_schedule);
  node = isl_schedule_get_root(schedule);
  isl_schedule_free(schedule);

  /* Delete the node above the array mark. */
  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_parent(node);
  while (!autosa_tree_node_is_kernel(node))
  {
    node = isl_schedule_node_delete(node);
    node = isl_schedule_node_parent(node);
  }

  node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
  node = isl_schedule_node_parent(node);
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  {
    filter = schedule_eq_ub(node);
    insert_filter = isl_bool_true;
  }
  node = autosa_tree_move_up_to_array(node);
  node = autosa_tree_move_down_to_io_mark(node, kernel->core, module->level);
  node = isl_schedule_node_cut(node);

  /* Graft an extension node. */
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "fifo_decl.");
  p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
  stmt_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  space = isl_space_set_alloc(ctx, 0, 0);
  id = isl_id_alloc(ctx, stmt_name, group);
  space = isl_space_set_tuple_id(space, isl_dim_set, id);
  free(stmt_name);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft = isl_schedule_node_from_domain(domain);

  node = isl_schedule_node_graft_before(node, graft);

  if (insert_filter)
  {
    isl_union_map *prefix, *extension, *umap;
    isl_union_set *domain, *range;
    isl_point *pnt;
    isl_set *set;
    isl_map *map;
    isl_multi_union_pw_aff *mupa;

    node = isl_schedule_node_insert_filter(node, filter);
    node = isl_schedule_node_child(node, 0);

    prefix = isl_schedule_node_get_prefix_schedule_relation(node);
    prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                              isl_union_pw_multi_aff_copy(kernel->contraction));
    domain = isl_union_map_range(prefix);

    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "fifo_decl_boundary.");
    p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
    stmt_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    space = isl_space_set_alloc(ctx, 0, 1);
    id = isl_id_alloc(ctx, stmt_name, group);
    space = isl_space_set_tuple_id(space, isl_dim_set, id);
    free(stmt_name);

    pnt = isl_point_zero(space);
    set = isl_set_from_point(pnt);
    range = isl_union_set_from_set(isl_set_copy(set));

    extension = isl_union_map_from_domain_and_range(domain, range);
    graft = isl_schedule_node_from_extension(extension);
    map = isl_set_identity(set);
    map = isl_map_reset_tuple_id(map, isl_dim_out);
    umap = isl_union_map_from_map(map);
    mupa = isl_multi_union_pw_aff_from_union_map(umap);

    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_insert_partial_schedule(graft, mupa);

    while (graft && isl_schedule_node_has_parent(graft))
      graft = isl_schedule_node_parent(graft);

    node = isl_schedule_node_graft_before(node, graft);
  }

  /* Insert an empty filter. */
  empty_filter = isl_union_set_from_set(isl_set_empty(
      isl_set_get_space(kernel->context)));
  node = isl_schedule_node_insert_filter(node, empty_filter);

  /* Add module mark after the kernel mark. */
  id = isl_id_alloc(ctx, "module", module);
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_mark(node, id);

  schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);

  top->n_fifo_decls++;
  top->fifo_decl_scheds = (isl_schedule **)realloc(top->fifo_decl_scheds,
                                                   top->n_fifo_decls * sizeof(isl_schedule *));
  top->fifo_decl_scheds[top->n_fifo_decls - 1] = schedule;
  top->fifo_decl_names = (char **)realloc(top->fifo_decl_names,
                                          top->n_fifo_decls * sizeof(char *));
  /* Generate fifo_decl name in the format of
   * [fifo_name].[fifo_width]
   */
  p_str = isl_printer_to_str(ctx);
  p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
  p_str = isl_printer_print_str(p_str, "_");
  p_str = isl_printer_print_str(p_str, module->name);
  p_str = isl_printer_print_str(p_str, ".");
  int n_lane = get_io_group_n_lane(module, NULL, group);
  int data_size = group->array->size;
  int width = data_size * n_lane; // in bytes
  p_str = isl_printer_print_int(p_str, width);
  top->fifo_decl_names[top->n_fifo_decls - 1] = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  return isl_stat_ok;
}

/* Generate the module calls and fifo decls for the io group. */
static isl_stat top_module_io_gen(struct autosa_gen *gen,
                                  struct autosa_hw_top_module *top,
                                  struct autosa_hw_module *module)
{
  struct autosa_array_ref_group *group;
  assert(module->n_io_group == 1);
  group = module->io_groups[0];

//#ifdef _DEBUG
//  if (!strcmp(module->name, "L_drain_IO_L1_out"))
//    printf("debug here!\n");
//    printf("module_call_id: %d\n", top->n_module_calls);
//#endif

  /* Generate the function call schedule. */
  if (module->is_serialized && module->in) {
    /* Generate an axtra function call schedule for the serialize module. */
    top_module_io_gen_module_call(gen, top, module, group, 1);
  }
  top_module_io_gen_module_call(gen, top, module, group, 0);
  if (module->is_serialized && !module->in) {
    /* Generate an axtra function call schedule for the serialize module. */
    top_module_io_gen_module_call(gen, top, module, group, 1);
  }

  /* Generate the fifo declaration schedule. */
  top_module_io_gen_fifo_decl(gen, top, module, group);

  /* Generate the external memory module arguments setting schedule. */
  if (gen->options->target == AUTOSA_TARGET_INTEL_OPENCL)
  {
    top_module_io_gen_ext_module(gen, top, module, group);
  }

  return isl_stat_ok;
}

/* Generate the top module that contains module calls and fifo declarations. */
__isl_give struct autosa_hw_top_module *sa_top_module_gen(struct autosa_gen *gen)
{
  struct autosa_hw_top_module *top_module;

  top_module = autosa_hw_top_module_alloc();
  top_module->hw_modules = gen->hw_modules;
  top_module->kernel = gen->kernel;
  top_module->n_hw_modules = gen->n_hw_modules;

  for (int i = 0; i < gen->n_hw_modules; i++)
  {
    struct autosa_hw_module *module = gen->hw_modules[i];
    if (module->type == PE_MODULE)
    {
      top_module_pe_gen(gen, top_module, gen->hw_modules[i]);
    }
    else
    {
      top_module_io_gen(gen, top_module, gen->hw_modules[i]);
    }
  }

  return top_module;
}

/* Build new schedules for each hardware components.
 * The total number of schedules = 
 * [1. the default schedule (CPU code)]
 * 2. PE schedule
 * 3. I/O module schedule
 * 4. drain module schedule
 * 5. top module schedule
 */
void generate_hw_modules(__isl_take isl_schedule *schedule,
                         struct autosa_gen *gen, struct autosa_kernel *kernel)
{
  gen->schedule = schedule;
  gen->n_hw_modules = 1;
  gen->hw_modules = isl_calloc_array(gen->ctx,
                                     struct autosa_hw_module *, gen->n_hw_modules);
  gen->hw_modules[0] = NULL;
  /* IO module */
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *info = &kernel->array[i];    
    info->n_io_group_refs = 0;
    for (int j = 0; j < info->n_io_group; j++)
    {
      int n_hw_modules = 0;
      struct autosa_hw_module **hw_modules;
      hw_modules = sa_io_module_gen(info->io_groups[j], gen, &n_hw_modules, 1, 1);

      gen->hw_modules = (struct autosa_hw_module **)realloc(gen->hw_modules,
                                                            (gen->n_hw_modules + n_hw_modules) * sizeof(struct polysa_hw_module *));
      for (int k = 0; k < n_hw_modules; k++)
      {
        gen->hw_modules[gen->n_hw_modules + k] = hw_modules[k];
      }
      gen->n_hw_modules += n_hw_modules;
      if (hw_modules)
        free(hw_modules);
    }
  }
  /* Drain module */
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *info = &kernel->array[i];
    if (!info->drain_group)
      continue;
    int n_hw_modules = 0;
    struct autosa_hw_module **hw_modules;
    hw_modules = sa_io_module_gen(info->drain_group, gen, &n_hw_modules, 0, 1);

    if (n_hw_modules > 0)
    {
      gen->hw_modules = (struct autosa_hw_module **)realloc(gen->hw_modules,
                                                            (gen->n_hw_modules + n_hw_modules) * sizeof(struct polysa_hw_module *));
      for (int j = 0; j < n_hw_modules; j++)
      {
        gen->hw_modules[gen->n_hw_modules + j] = hw_modules[j];
      }
      gen->n_hw_modules += n_hw_modules;
    }
    if (hw_modules)
      free(hw_modules);
  }
  /* PE module */
  gen->hw_modules[0] = sa_pe_module_gen(gen);

  /* Reorder the sequence of the modules. */
  gen->hw_modules = hw_module_reorder(gen->hw_modules, gen->n_hw_modules);

  /* top module */
  struct autosa_hw_top_module *top_module = sa_top_module_gen(gen);
  gen->hw_top_module = top_module;

  /* Generate drain merge functions. */
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *info = &kernel->array[i];
    if (!info->drain_group)
      continue;
    if (info->n_mem_ports == 1)
      continue;
    struct autosa_drain_merge_func *func =
        generate_drain_merge_func(info->drain_group, kernel, gen);
    gen->drain_merge_funcs = (struct autosa_drain_merge_func **)realloc(
        gen->drain_merge_funcs, (gen->n_drain_merge_funcs + 1) *
                                    sizeof(struct autosa_drain_merge_func *));
    gen->drain_merge_funcs[gen->n_drain_merge_funcs] = func;
    gen->n_drain_merge_funcs++;
  }
}

/* Replace any reference to an array element in the range of "copy"
 * by a reference to all array elements (defined by the extent of the array).
 */
static __isl_give isl_union_map *approximate_copy_out(
    __isl_take isl_union_map *copy, struct autosa_prog *prog)
{
  int i;
  isl_union_map *res;

  res = isl_union_map_empty(isl_union_map_get_space(copy));

  for (i = 0; i < prog->n_array; ++i)
  {
    isl_space *space;
    isl_set *set;
    isl_union_map *copy_i;
    isl_union_set *extent, *domain;

    space = isl_space_copy(prog->array[i].space);
    extent = isl_union_set_from_set(isl_set_universe(space));
    copy_i = isl_union_map_copy(copy);
    copy_i = isl_union_map_intersect_range(copy_i, extent);
    set = isl_set_copy(prog->array[i].extent);
    extent = isl_union_set_from_set(set);
    domain = isl_union_map_domain(copy_i);
    copy_i = isl_union_map_from_domain_and_range(domain, extent);
    res = isl_union_map_union(res, copy_i);
  }

  isl_union_map_free(copy);

  return res;
}

/* Internal data structure for node_may_persist.
 *
 * "tagger" maps tagged iteration domains to the corresponding untagged
 *	iteration domain.
 *
 * "may_persist_flow" is the set of all tagged dataflow dependences
 * with those dependences removed that either precede or follow
 * the kernel launch in a sequence.
 * "inner_band_flow" is the set of all tagged dataflow dependences
 * that are local to a given iteration of the outer band nodes
 * with respect to the current node.
 * "local_flow" is equal to "inner_band_flow", except that the domain
 * and the range have been intersected with intermediate filters
 * on children of sets or sequences.
 */
struct ppcg_may_persist_data
{
  isl_union_pw_multi_aff *tagger;

  isl_union_map *local_flow;
  isl_union_map *inner_band_flow;
  isl_union_map *may_persist_flow;
};

/* Update the information in "data" based on the band ancestor "node".
 *
 * In particular, we restrict the dependences in data->local_flow
 * to those dependence where the source and the sink occur in
 * the same iteration of the given band node.
 * We also update data->inner_band_flow to the new value of
 * data->local_flow.
 */
static int update_may_persist_at_band(__isl_keep isl_schedule_node *node,
                                      struct ppcg_may_persist_data *data)
{
  isl_multi_union_pw_aff *partial;
  isl_union_pw_multi_aff *contraction;
  isl_union_map *flow;

  if (isl_schedule_node_band_n_member(node) == 0)
    return 0;

  partial = isl_schedule_node_band_get_partial_schedule(node);
  contraction = isl_schedule_node_get_subtree_contraction(node);
  partial = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(partial,
                                                               contraction);
  partial = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(partial,
                                                               isl_union_pw_multi_aff_copy(data->tagger));

  flow = data->local_flow;
  flow = isl_union_map_eq_at_multi_union_pw_aff(flow, partial);
  data->local_flow = flow;

  isl_union_map_free(data->inner_band_flow);
  data->inner_band_flow = isl_union_map_copy(data->local_flow);

  return 0;
}

/* Given a set of local reaching domain elements "domain",
 * expand them to the corresponding leaf domain elements using "contraction"
 * and insert the array references tags using data->tagger.
 */
static __isl_give isl_union_set *expand_and_tag(
    __isl_take isl_union_set *domain,
    __isl_take isl_union_pw_multi_aff *contraction,
    struct ppcg_may_persist_data *data)
{
  domain = isl_union_set_preimage_union_pw_multi_aff(domain,
                                                     contraction);
  domain = isl_union_set_preimage_union_pw_multi_aff(domain,
                                                     isl_union_pw_multi_aff_copy(data->tagger));
  return domain;
}

/* Given a filter node that is the child of a set or sequence node,
 * restrict data->local_flow to refer only to those elements
 * in the filter of the node.
 * "contraction" maps the leaf domain elements of the schedule tree
 * to the corresponding domain elements at (the parent of) "node".
 */
static int filter_flow(__isl_keep isl_schedule_node *node,
                       struct ppcg_may_persist_data *data,
                       __isl_take isl_union_pw_multi_aff *contraction)
{
  isl_union_set *filter;
  isl_union_map *flow;

  flow = data->local_flow;
  filter = isl_schedule_node_filter_get_filter(node);
  filter = expand_and_tag(filter, contraction, data);
  flow = isl_union_map_intersect_domain(flow, isl_union_set_copy(filter));
  flow = isl_union_map_intersect_range(flow, filter);
  data->local_flow = flow;

  return 0;
}

/* Given a filter node "node", collect the filters on all preceding siblings
 * (which are also filter nodes), add them to "filters" and return the result.
 */
static __isl_give isl_union_set *add_previous_filters(
    __isl_take isl_union_set *filters, __isl_keep isl_schedule_node *node)
{
  isl_schedule_node *sibling;

  sibling = isl_schedule_node_copy(node);
  while (sibling && isl_schedule_node_has_previous_sibling(sibling))
  {
    isl_union_set *filter;

    sibling = isl_schedule_node_previous_sibling(sibling);
    filter = isl_schedule_node_filter_get_filter(sibling);
    filters = isl_union_set_union(filters, filter);
  }
  isl_schedule_node_free(sibling);
  if (!sibling)
    return isl_union_set_free(filters);

  return filters;
}

/* Given a filter node "node", collect the filters on all following siblings
 * (which are also filter nodes), add them to "filters" and return the result.
 */
static __isl_give isl_union_set *add_next_filters(
    __isl_take isl_union_set *filters, __isl_keep isl_schedule_node *node)
{
  isl_schedule_node *sibling;

  sibling = isl_schedule_node_copy(node);
  while (sibling && isl_schedule_node_has_next_sibling(sibling))
  {
    isl_union_set *filter;

    sibling = isl_schedule_node_next_sibling(sibling);
    filter = isl_schedule_node_filter_get_filter(sibling);
    filters = isl_union_set_union(filters, filter);
  }
  isl_schedule_node_free(sibling);
  if (!sibling)
    return isl_union_set_free(filters);

  return filters;
}

/* Remove those flow dependences from data->may_persist_flow
 * that flow between elements of "domain" within the same iteration
 * of all outer band nodes.
 * "contraction" maps the leaf domain elements of the schedule tree
 * to the corresponding elements "domain".
 */
static void remove_external_flow(struct ppcg_may_persist_data *data,
                                 __isl_take isl_union_set *domain,
                                 __isl_keep isl_union_pw_multi_aff *contraction)
{
  isl_union_map *flow;

  contraction = isl_union_pw_multi_aff_copy(contraction);
  domain = expand_and_tag(domain, contraction, data);
  flow = isl_union_map_copy(data->local_flow);
  flow = isl_union_map_intersect_domain(flow, isl_union_set_copy(domain));
  flow = isl_union_map_intersect_range(flow, domain);

  data->may_persist_flow = isl_union_map_subtract(data->may_persist_flow,
                                                  flow);
}

/* Update the information in "data" based on the filter ancestor "node".
 * We only need to modify anything if the filter is the child
 * of a set or sequence node.
 *
 * In the case of a sequence, we remove the dependences between
 * statement instances that are both executed either before or
 * after the subtree that will be mapped to a kernel, within
 * the same iteration of outer bands.
 *
 * In both cases, we restrict data->local_flow to the current child.
 */
static int update_may_persist_at_filter(__isl_keep isl_schedule_node *node,
                                        struct ppcg_may_persist_data *data)
{
  enum isl_schedule_node_type type;
  isl_schedule_node *parent;
  isl_space *space;
  isl_union_pw_multi_aff *contraction;
  isl_union_set *before, *after, *filter;

  type = isl_schedule_node_get_parent_type(node);
  if (type != isl_schedule_node_sequence && type != isl_schedule_node_set)
    return 0;

  parent = isl_schedule_node_copy(node);
  parent = isl_schedule_node_parent(parent);
  contraction = isl_schedule_node_get_subtree_contraction(parent);
  isl_schedule_node_free(parent);

  if (type == isl_schedule_node_set)
    return filter_flow(node, data, contraction);

  filter = isl_schedule_node_filter_get_filter(node);
  space = isl_union_set_get_space(filter);
  isl_union_set_free(filter);
  before = isl_union_set_empty(space);
  after = isl_union_set_copy(before);
  before = add_previous_filters(before, node);
  after = add_next_filters(after, node);

  remove_external_flow(data, before, contraction);
  remove_external_flow(data, after, contraction);

  return filter_flow(node, data, contraction);
}

/* Update the information in "data" based on the ancestor "node".
 */
static isl_stat update_may_persist_at(__isl_keep isl_schedule_node *node,
                                      void *user)
{
  struct ppcg_may_persist_data *data = (struct ppcg_may_persist_data *)user;

  switch (isl_schedule_node_get_type(node))
  {
  case isl_schedule_node_error:
    return isl_stat_error;
  case isl_schedule_node_context:
  case isl_schedule_node_domain:
  case isl_schedule_node_expansion:
  case isl_schedule_node_extension:
  case isl_schedule_node_guard:
  case isl_schedule_node_leaf:
  case isl_schedule_node_mark:
  case isl_schedule_node_sequence:
  case isl_schedule_node_set:
    break;
  case isl_schedule_node_band:
    if (update_may_persist_at_band(node, data) < 0)
      return isl_stat_error;
    break;
  case isl_schedule_node_filter:
    if (update_may_persist_at_filter(node, data) < 0)
      return isl_stat_error;
    break;
  }

  return isl_stat_ok;
}

/* Determine the set of array elements that may need to be perserved
 * by a kernel constructed from the subtree at "node".
 * This includes the set of array elements that may need to be preserved
 * by the entire scop (prog->may_persist) and the elements for which
 * there is a potential flow dependence that may cross a kernel launch.
 *
 * To determine the second set, we start from all flow dependences.
 * From this set of dependences, we remove those that cannot possibly
 * require data to be preserved by a kernel launch.
 * In particular, we consider the following sets of dependences.
 * - dependences of which the write occurs inside the kernel.
 *   If the data is needed outside the kernel, then it will
 *   be copied out immediately after the kernel launch, so there
 *   is no need for any special care.
 * - dependences of which the read occurs inside the kernel and the
 *   corresponding write occurs inside the same iteration of the
 *   outer band nodes.  This means that the data is needed in
 *   the first kernel launch after the write, which is already
 *   taken care of by the standard copy-in.  That is, the data
 *   do not need to be preserved by any intermediate call to
 *   the same kernel.
 * - dependences of which the write and the read either both occur
 *   before the kernel launch or both occur after the kernel launch,
 *   within the same iteration of the outer band nodes with respect
 *   to the sequence that determines the ordering of the dependence
 *   and the kernel launch.  Such flow dependences cannot cross
 *   any kernel launch.
 *
 * For the remaining (tagged) dependences, we take the domain
 * (i.e., the tagged writes) and apply the tagged access relation
 * to obtain the accessed data elements.
 * These are then combined with the elements that may need to be
 * preserved by the entire scop.
 */
static __isl_give isl_union_set *node_may_persist(
    __isl_keep isl_schedule_node *node, struct autosa_prog *prog)
{
  struct ppcg_may_persist_data data;
  isl_union_pw_multi_aff *contraction;
  isl_union_set *domain;
  isl_union_set *persist;
  isl_union_map *flow, *local_flow;

  data.tagger = prog->scop->tagger;

  flow = isl_union_map_copy(prog->scop->tagged_dep_flow);
  data.local_flow = isl_union_map_copy(flow);
  data.inner_band_flow = isl_union_map_copy(flow);
  data.may_persist_flow = flow;
  if (isl_schedule_node_foreach_ancestor_top_down(node,
                                                  &update_may_persist_at, &data) < 0)
    data.may_persist_flow =
        isl_union_map_free(data.may_persist_flow);
  flow = data.may_persist_flow;
  isl_union_map_free(data.local_flow);

  domain = isl_schedule_node_get_domain(node);
  contraction = isl_schedule_node_get_subtree_contraction(node);
  domain = isl_union_set_preimage_union_pw_multi_aff(domain,
                                                     contraction);
  domain = isl_union_set_preimage_union_pw_multi_aff(domain,
                                                     isl_union_pw_multi_aff_copy(data.tagger));
  /* Substract the case 1. */
  flow = isl_union_map_subtract_domain(flow, isl_union_set_copy(domain));
  local_flow = data.inner_band_flow;
  local_flow = isl_union_map_intersect_range(local_flow, domain);
  /* Substract the case 2. */
  flow = isl_union_map_subtract(flow, local_flow);

  persist = isl_union_map_domain(flow);
  persist = isl_union_set_apply(persist,
                                isl_union_map_copy(prog->scop->tagged_may_writes));
  persist = isl_union_set_union(persist,
                                isl_union_set_copy(prog->may_persist));

  return persist;
}

/* Return (the universe spaces of) the arrays that are declared
 * inside the scop corresponding to "prog" and for which all
 * potential writes inside the scop form a subset of "domain".
 */
static __isl_give isl_union_set *extract_local_accesses(struct autosa_prog *prog,
                                                        __isl_keep isl_union_set *domain)
{
  int i;
  isl_union_set *local;

  local = isl_union_set_empty(isl_union_set_get_space(domain));

  for (i = 0; i < prog->n_array; ++i)
  {
    isl_set *set;
    isl_union_map *to_outer;
    isl_union_map *may_write;
    isl_union_set *write_domain;
    isl_union_set *fields;
    int subset;

    if (!prog->array[i].local)
      continue;

    set = isl_set_universe(isl_space_copy(prog->array[i].space));
    to_outer = isl_union_map_copy(prog->to_outer);
    to_outer = isl_union_map_intersect_range(to_outer,
                                             isl_union_set_from_set(isl_set_copy(set)));
    fields = isl_union_map_domain(to_outer);
    may_write = isl_union_map_copy(prog->may_write);
    may_write = isl_union_map_intersect_range(may_write, fields);
    write_domain = isl_union_map_domain(may_write);
    subset = isl_union_set_is_subset(write_domain, domain);
    isl_union_set_free(write_domain);

    if (subset < 0)
    {
      isl_set_free(set);
      return isl_union_set_free(local);
    }
    else if (subset)
    {
      local = isl_union_set_add_set(local, set);
    }
    else
    {
      isl_set_free(set);
    }
  }

  return local;
}

/* For each array in "prog" of which an element appears in "accessed" and
 * that is not a read only scalar, create a zero-dimensional universe set
 * of which the tuple id has name "<prefix>_<name of array>" and a user
 * pointer pointing to the array (autosa_array_info).
 *
 * If the array is local to "prog", then make sure it will be declared
 * in the host code.
 *
 * Return the list of these universe sets.
 */
static __isl_give isl_union_set_list *create_copy_filters(struct autosa_prog *prog,
                                                          const char *prefix, __isl_take isl_union_set *accessed)
{
  int i;
  isl_ctx *ctx;
  isl_union_set_list *filters;

  ctx = prog->ctx;
  filters = isl_union_set_list_alloc(ctx, 0);
  for (i = 0; i < prog->n_array; ++i)
  {
    struct autosa_array_info *array = &prog->array[i];
    isl_space *space;
    isl_set *accessed_i;
    int empty;
    char *name;
    isl_id *id;
    isl_union_set *uset;

    if (autosa_array_is_read_only_scalar(array))
      continue;

    space = isl_space_copy(array->space);
    accessed_i = isl_union_set_extract_set(accessed, space);
    empty = isl_set_plain_is_empty(accessed_i);
    isl_set_free(accessed_i);
    if (empty < 0)
    {
      filters = isl_union_set_list_free(filters);
      break;
    }
    if (empty)
      continue;

    array->global = 1;
    array->local_array->global = 1;
    if (array->local)
      array->declare_local = 1;
    if (!strcmp(prefix, "to_device"))
      array->copy_in = 1;
    if (!strcmp(prefix, "from_device"))
      array->copy_out = 1;

    name = concat(ctx, prefix, array->name);
    id = name ? isl_id_alloc(ctx, name, array) : NULL;
    free(name);
    space = isl_space_set_alloc(ctx, 0, 0);
    space = isl_space_set_tuple_id(space, isl_dim_set, id);
    uset = isl_union_set_from_set(isl_set_universe(space));

    filters = isl_union_set_list_add(filters, uset);
  }
  isl_union_set_free(accessed);

  return filters;
}

/* Return the set of parameter values for which the array has a positive
 * size in all dimensions.
 * If the sizes are only valid for some parameter values, then those
 * constraints are also taken into account.
 */
__isl_give isl_set *autosa_array_positive_size_guard(struct autosa_array_info *array)
{
  int i;
  isl_space *space;
  isl_set *guard;

  if (!array)
    return NULL;

  space = isl_space_params(isl_space_copy(array->space));
  guard = isl_set_universe(space);

  for (i = 0; i < array->n_index; ++i)
  {
    isl_pw_aff *bound;
    isl_set *guard_i, *zero;

    bound = isl_multi_pw_aff_get_pw_aff(array->bound, i);
    guard_i = isl_pw_aff_nonneg_set(isl_pw_aff_copy(bound));
    zero = isl_pw_aff_zero_set(bound);
    guard_i = isl_set_subtract(guard_i, zero);
    guard = isl_set_intersect(guard, guard_i);
  }

  return guard;
}

/* Make sure that code for the statements in "filters" that
 * copy arrays to or from the device is only generated when
 * the size of the corresponding array is positive.
 * That is, add a set node underneath "graft" with "filters" as children
 * and for each child add a guard that the selects the parameter
 * values for which the corresponding array has a positive size.
 * The array is available in the user pointer of the statement identifier.
 * "depth" is the schedule depth of the position where "graft"
 * will be added.
 */
static __isl_give isl_schedule_node *insert_positive_size_guards(
    __isl_take isl_schedule_node *graft,
    __isl_take isl_union_set_list *filters, int depth)
{
  int i, n;

  graft = isl_schedule_node_child(graft, 0);
  graft = isl_schedule_node_insert_set(graft, filters);
  n = isl_schedule_node_n_children(graft);
  for (i = 0; i < n; ++i)
  {
    isl_union_set *filter;
    isl_set *domain, *guard;
    isl_id *id;
    struct autosa_array_info *array;

    graft = isl_schedule_node_child(graft, i);
    filter = isl_schedule_node_filter_get_filter(graft);
    domain = isl_set_from_union_set(filter);
    id = isl_set_get_tuple_id(domain);
    array = (struct autosa_array_info *)isl_id_get_user(id);
    isl_id_free(id);
    isl_set_free(domain);
    guard = autosa_array_positive_size_guard(array);
    guard = isl_set_from_params(guard);
    guard = isl_set_add_dims(guard, isl_dim_set, depth);
    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_insert_guard(graft, guard);
    graft = isl_schedule_node_parent(graft);
    graft = isl_schedule_node_parent(graft);
  }
  graft = isl_schedule_node_parent(graft);

  return graft;
}

/* Create a graft for copying arrays to or from the device,
 * whenever the size of the array is strictly positive.
 * Each statement is called "<prefix>_<name of array>" and
 * the identifier has a user pointer pointing to the array.
 * The graft will be added at the position specified by "node".
 * "copy" contains the array elements that need to be copied.
 * Only arrays of which some elements need to be copied
 * will have a corresponding statement in the graph.
 * Note though that each such statement will copy the entire array.
 */
static __isl_give isl_schedule_node *create_copy_device(struct autosa_prog *prog,
                                                        __isl_keep isl_schedule_node *node, const char *prefix,
                                                        __isl_take isl_union_set *copy)
{
  int depth;
  isl_ctx *ctx;
  isl_space *space;
  isl_union_set *all, *domain;
  isl_union_set_list *filters;
  isl_union_map *extension;
  isl_schedule_node *graft;

  ctx = prog->ctx;
  depth = isl_schedule_node_get_schedule_depth(node);
  filters = create_copy_filters(prog, prefix, copy);
  all = isl_union_set_list_union(isl_union_set_list_copy(filters));

  space = depth < 0 ? NULL : isl_space_set_alloc(ctx, 0, depth);
  domain = isl_union_set_from_set(isl_set_universe(space));
  extension = isl_union_map_from_domain_and_range(domain, all);
  graft = isl_schedule_node_from_extension(extension);

  if (!filters)
    return isl_schedule_node_free(graft);
  if (isl_union_set_list_n_union_set(filters) == 0)
  {
    isl_union_set_list_free(filters);
    return graft;
  }

  return insert_positive_size_guards(graft, filters, depth);
}

/* Add nodes for copying outer arrays in and out of the device
 * before and after the subtree "node", which contains one or more kernels.
 * "domain" contains the original statement instances, i.e.,
 * those that correspond to the domains of the access relations in "prog".
 * In particular, the domain has not been contracted in any way.
 * "prefix" contains the prefix schedule at that point, in terms
 * of the same original statement instances.
 *
 * We first compute the sets of outer array elements that need
 * to be copied in and out and then graft in the nodes for
 * performing this copying.
 *
 * In particular, for each array that is possibly written anywhere in
 * the subtree "node" and that may be used after "node"
 * or that may be visible outside the corresponding scop,
 * we copy out its entire extent.
 *
 * Any array elements that is read without first being written inside
 * the subtree "node" needs to be copied in.
 * Furthermore, if there are any array elements that
 * are copied out, but that may not be written inside "node", then
 * they also need to be copied in to ensure that the value after execution
 * is the same as the value before execution, at least for those array
 * elements that may have their values preserved by the scop or that
 * may be written before "node" and read after "node".
 * In case the array elements are structures, we need to take into
 * account that all members of the structures need to be written
 * by "node" before we can avoid copying the data structure in.
 *
 * Note that the may_write relation is intersected with the domain,
 * which has been intersected with the context.
 * This helps in those cases where the arrays are declared with a fixed size,
 * while the accesses are parametric and the context assigns a fixed value
 * to the parameters.
 *
 * If an element from a local array is read without first being written,
 * then there is no point in copying it in since it cannot have been
 * written prior to the scop. Warn about the uninitialized read instead.
 */
__isl_give isl_schedule_node *sa_add_to_from_device(
    __isl_take isl_schedule_node *node, __isl_take isl_union_set *domain,
    __isl_take isl_union_map *prefix, struct autosa_prog *prog)
{
  isl_union_set *local;
  isl_union_set *may_persist;
  isl_union_map *may_write, *must_write, *copy_out, *not_written;
  isl_union_map *read, *copy_in;
  isl_union_map *tagged;
  isl_union_map *local_uninitialized;
  isl_schedule_node *graft;

  /* Compute the copy-out that contains the live-out union
   * domain of non-local flow dep. 
   */
  tagged = isl_union_map_copy(prog->scop->tagged_reads);
  tagged = isl_union_map_union(tagged,
                               isl_union_map_copy(prog->scop->tagged_may_writes));
  may_write = isl_union_map_copy(prog->may_write);
  may_write = isl_union_map_intersect_domain(may_write,
                                             isl_union_set_copy(domain));
  /* Keep only the live-out union domain of non-local flow. */
  may_write = remove_local_accesses(prog,
                                    isl_union_map_copy(tagged), may_write,
                                    isl_union_map_copy(prefix), 0);
  may_write = isl_union_map_apply_range(may_write,
                                        isl_union_map_copy(prog->to_outer));
  may_write = isl_union_map_apply_domain(may_write,
                                         isl_union_map_copy(prefix));
  may_write = approximate_copy_out(may_write, prog);
  copy_out = isl_union_map_copy(may_write);

  /* Compute the copy-in. */
  may_write = isl_union_map_apply_range(may_write,
                                        isl_union_map_copy(prog->to_inner));
  must_write = isl_union_map_copy(prog->must_write);
  must_write = isl_union_map_apply_domain(must_write,
                                          isl_union_map_copy(prefix));

  may_persist = node_may_persist(node, prog);
  may_write = isl_union_map_intersect_range(may_write, may_persist);
  not_written = isl_union_map_subtract(may_write, must_write);

  /* Detect the unitialized reads. */
  /* "local" contains (universal space) of arrays that are declared locally and 
   * written by "domain". */
  local = extract_local_accesses(prog, domain);
  local = isl_union_set_apply(local, isl_union_map_copy(prog->to_inner));
  local_uninitialized = isl_union_map_copy(prog->scop->live_in);
  /* The local unitialized is defined as a read of a local array without 
   * first being written. */
  local_uninitialized = isl_union_map_intersect_range(local_uninitialized,
                                                      local);
  read = isl_union_map_copy(prog->read);
  read = isl_union_map_intersect_domain(read, domain);
  read = remove_local_accesses(prog, tagged, read,
                               isl_union_map_copy(prefix), 1);
  local_uninitialized = isl_union_map_intersect(local_uninitialized,
                                                isl_union_map_copy(read));
  if (!isl_union_map_is_empty(local_uninitialized))
  {
    fprintf(stderr,
            "possibly uninitialized reads (not copied in):\n");
    isl_union_map_dump(local_uninitialized);
  }
  read = isl_union_map_subtract(read, local_uninitialized);
  read = isl_union_map_apply_domain(read, prefix);
  copy_in = isl_union_map_union(read, not_written);
  copy_in = isl_union_map_apply_range(copy_in,
                                      isl_union_map_copy(prog->to_outer));

  /* Add in the copy-in/copy-out nodes. */
  graft = create_copy_device(prog, node, "to_device",
                             isl_union_map_range(copy_in));
  node = isl_schedule_node_graft_before(node, graft);
  graft = create_copy_device(prog, node, "from_device",
                             isl_union_map_range(copy_out));
  node = isl_schedule_node_graft_after(node, graft);

  return node;
}

/* Add nodes for initializing ("init_device") and clearing ("clear_device")
 * the device before and after "node".
 */
__isl_give isl_schedule_node *sa_add_init_clear_device(
    __isl_take isl_schedule_node *node, struct autosa_kernel *kernel)
{
  isl_ctx *ctx;
  isl_space *space;
  isl_union_set *domain;
  isl_schedule_node *graft;
  isl_id *id;

  ctx = isl_schedule_node_get_ctx(node);

  space = isl_space_set_alloc(ctx, 0, 0);
  id = isl_id_alloc(ctx, "init_device", kernel);
  //space = isl_space_set_tuple_name(space, isl_dim_set, "init_device");
  space = isl_space_set_tuple_id(space, isl_dim_set, id);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft = isl_schedule_node_from_domain(domain);

  node = isl_schedule_node_graft_before(node, graft);

  space = isl_space_set_alloc(ctx, 0, 0);
  id = isl_id_alloc(ctx, "clear_device", kernel);
  //space = isl_space_set_tuple_name(space, isl_dim_set, "clear_device");
  space = isl_space_set_tuple_id(space, isl_dim_set, id);
  domain = isl_union_set_from_set(isl_set_universe(space));
  graft = isl_schedule_node_from_domain(domain);

  node = isl_schedule_node_graft_after(node, graft);

  return node;
}

__isl_give isl_schedule_node *sa_add_drain_merge(
    __isl_take isl_schedule_node *node, struct autosa_gen *gen)
{
  isl_ctx *ctx;

  ctx = isl_schedule_node_get_ctx(node);
  for (int i = 0; i < gen->n_drain_merge_funcs; i++)
  {
    isl_id *id;
    isl_space *space;
    isl_union_set *domain;
    isl_schedule_node *graft;
    struct autosa_drain_merge_func *func = gen->drain_merge_funcs[i];
    struct autosa_array_ref_group *group = func->group;
    if (group->local_array->n_mem_ports == 1)
      continue;
    space = isl_space_set_alloc(ctx, 0, 0);
    id = isl_id_alloc(ctx, "drain_merge", func);
    space = isl_space_set_tuple_id(space, isl_dim_set, id);
    domain = isl_union_set_from_set(isl_set_universe(space));
    graft = isl_schedule_node_from_domain(domain);
    node = isl_schedule_node_graft_after(node, graft);
  }

  return node;
}

/***************************************************************
 * AST Codegen
 ***************************************************************/
/* Internal data structure for at_domain.
 * "prog" represents the entire scop.
 * "kernel" points to the kernel to which the current schedule node
 * belongs. It is set by before_mark and reset by after_mark.
 * It may be NULL if we are outside any kernel.
 */
struct autosa_at_domain_data
{
  struct autosa_prog *prog;
  struct autosa_kernel *kernel;
  struct autosa_hw_module *module;
  struct autosa_hw_top_module *top;
  struct autosa_pe_dummy_module *pe_dummy_module;
  struct autosa_drain_merge_func *drain_merge_func;
  int filter_buffer;
  int boundary;
  int pe_dummy;

  /* Under a "pipeline" mark */
  int under_pipeline;
  /* Under a "unroll" mark */
  int under_unroll;
  /* Inside a "pipeline" for loop */
  int in_pipeline_for;
  /* Inside a "unroll" for loop */
  int in_unroll_for;
  /* Inside a for loop */
  int in_for;
};

/* Internal data structure for the index and AST expression transformation
 * callbacks for pet_stmt_build_ast_exprs.
 *
 * "kernel" is the kernel for which are computing AST expressions and
 * may be NULL if we are not inside a kernel.
 * "accesses" is the list of polysa_stmt_access in the statement.
 * "iterator_map" expresses the statement iterators in terms of
 * the AST loop iterators.
 * "sched2copy" expresses the outer copy_schedule_dim dimensions of
 * the kernel schedule in terms of the AST loop iterators and
 * may be NULL if we are not inside a kernel.
 *
 * The following fields are set in transform_index and used in transform_expr.
 * "array" is the array that is being accessed.
 * "global" is set if the global array is accessed (rather than
 * shared/private memory).
 * "local_array" refers to information on the array specialized
 * to the current kernel.
 */
struct autosa_transform_data
{
  struct autosa_kernel *kernel;
  struct autosa_stmt_access *accesses;
  isl_pw_multi_aff *iterator_map;
  isl_pw_multi_aff *sched2copy;

  struct autosa_array_info *array;
  int global;
  int reg;
  struct autosa_local_array_info *local_array;
  struct autosa_array_ref_group *group;
};

/* Set *depth (initialized to 0 by the caller) to the maximum
 * of the schedule depths of the leaf nodes for which this function is called.
 */
static isl_bool update_depth(__isl_keep isl_schedule_node *node, void *user)
{
  int *depth = (int *)user;
  int node_depth;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return isl_bool_true;
  node_depth = isl_schedule_node_get_schedule_depth(node);
  if (node_depth > *depth)
    *depth = node_depth;

  return isl_bool_false;
}

/* Given a mapping "iterator_map" from the AST schedule to a domain,
 * return the corresponding mapping from the AST schedule
 * to the outer kernel->copy_schedule_dim dimensions of
 * the schedule computed by AutoSA for this kernel.
 *
 * Note that kernel->copy_schedule_dim is at least as large as
 * the largest depth of any array reference group associated to the kernel.
 * This is needed as the returned schedule is used to extract a mapping
 * to the outer tile->depth dimensions in transform_index.
 */
static __isl_give isl_pw_multi_aff *compute_sched_to_copy(
    struct autosa_kernel *kernel, __isl_take isl_pw_multi_aff *iterator_map)
{
  isl_union_pw_multi_aff *upma;
  isl_pw_multi_aff *pma;
  isl_space *space;

  space = isl_space_range(isl_pw_multi_aff_get_space(iterator_map));
  space = isl_space_from_domain(space);
  space = isl_space_add_dims(space, isl_dim_out,
                             kernel->copy_schedule_dim);

  upma = isl_union_pw_multi_aff_copy(kernel->copy_schedule);
  pma = isl_union_pw_multi_aff_extract_pw_multi_aff(upma, space);
  isl_union_pw_multi_aff_free(upma);

  return isl_pw_multi_aff_pullback_pw_multi_aff(pma, iterator_map);
}

/* Return the autosa_stmt_access in the list "accesses" that corresponds
 * to "ref_id".
 */
static struct autosa_stmt_access *find_access(struct autosa_stmt_access *accesses,
                                              __isl_keep isl_id *ref_id)
{
  struct autosa_stmt_access *access;

  for (access = accesses; access; access = access->next)
    if (access->ref_id == ref_id)
      return access;

  return NULL;
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

/* Return the index of the array called "name" in the list of arrays.
 */
static int find_array_index(struct autosa_kernel *kernel, const char *name)
{
  int i;

  for (i = 0; i < kernel->n_array; ++i)
    if (!strcmp(name, kernel->array[i].array->name))
      return i;

  return -1;
}

/* Return a pointer to the autosa_array_ref_group in "local"
 * that contains the reference "access".
 * Return NULL if no such group can be found.
 */
static struct autosa_array_ref_group *find_ref_group(
    struct autosa_local_array_info *local, struct autosa_stmt_access *access)
{
  int i, j;

  for (i = 0; i < local->n_group; ++i)
  {
    struct autosa_array_ref_group *group = local->groups[i];

    for (j = 0; j < group->n_ref; ++j)
      if (group->refs[j] == access)
        return group;
  }

  return NULL;
}

/* Given a mapping "iterator_map" from the AST schedule to a domain,
 * return the corresponding mapping from the AST schedule
 * to the outer group->copy_schedule_dim dimensions of
 * the schedule computed by AutoSA for this kernel.
 *
 * Note that group->copy_schedule_dim is at least as large as
 * the largest depth of any array references associated to the group.
 * This is needed as the returned schedule is used to extract a mapping
 * to the outer tile->depth dimensions in transform_index.
 */
static __isl_give isl_pw_multi_aff *compute_sched_to_copy_group(
    __isl_take isl_pw_multi_aff *iterator_map,
    struct autosa_array_ref_group *group)
{
  isl_union_pw_multi_aff *upma;
  isl_pw_multi_aff *pma;
  isl_space *space;

  space = isl_space_range(isl_pw_multi_aff_get_space(iterator_map));
  space = isl_space_from_domain(space);
  space = isl_space_add_dims(space, isl_dim_out,
                             group->copy_schedule_dim);

  upma = isl_union_pw_multi_aff_copy(group->copy_schedule);
  pma = isl_union_pw_multi_aff_extract_pw_multi_aff(upma, space);
  isl_union_pw_multi_aff_free(upma);

  return isl_pw_multi_aff_pullback_pw_multi_aff(pma, iterator_map);
}

/* Given an index expression "index" of the form
 *
 *	L -> F(A),
 *
 * with F(A) either A or some subfield of A and L the AST loop iterators,
 * and a tiling "tiling" of the form
 *
 *	[L -> A] -> T
 *
 * apply the tiling to the outer array in the index expression to obtain
 *
 *	L -> T(A)
 *
 * If F(A) is some subfield of A, then separate the member access
 * into the base index expression and the field index expression,
 * apply the tiling to the base index expression and combine the result
 * with the field index expression.
 *
 * If F(A) is A, then modify index to keep track of the iterators
 *
 *	L -> [L -> A]
 *
 * and combine the result with the tiling to obtain a tiled index expression
 * in terms of the AST loop iterators
 *
 *	L -> T
 */
static __isl_give isl_multi_pw_aff *tile_outer(
    __isl_take isl_multi_pw_aff *index, __isl_take isl_multi_pw_aff *tiling)
{
  isl_bool is_wrapping;
  isl_space *space;
  isl_multi_pw_aff *mpa;

  is_wrapping = isl_multi_pw_aff_range_is_wrapping(index);
  if (is_wrapping < 0)
    goto error;
  if (is_wrapping)
  {
    isl_multi_pw_aff *field;

    field = isl_multi_pw_aff_copy(index);
    field = isl_multi_pw_aff_range_factor_range(field);
    index = isl_multi_pw_aff_range_factor_domain(index);
    index = tile_outer(index, tiling);
    return isl_multi_pw_aff_range_product(index, field);
  }

  space = isl_space_domain(isl_multi_pw_aff_get_space(index));
  space = isl_space_map_from_set(space);
  mpa = isl_multi_pw_aff_identity(space);
  index = isl_multi_pw_aff_range_product(mpa, index);
  index = isl_multi_pw_aff_pullback_multi_pw_aff(tiling, index);

  return index;
error:
  isl_multi_pw_aff_free(index);
  isl_multi_pw_aff_free(tiling);
  return NULL;
}

/* Index transformation callback for pet_stmt_build_ast_exprs.
 *
 * "index" expresses the array indices in terms of statement iterators
 *
 * We first reformulate "index" in terms of the AST loop iterators.
 * Then we check if we are accessing the global array or
 * a shared/private copy.  In particular, if we are not inside a kernel
 * then we must be accessing a global array.
 * In the former case, we simply return
 * the updated index.  If "index" is an affine expression rather
 * than an array access, then we also return the updated index here.
 *
 * If no reference groups have been computed for the array,
 * then we can only be accessing the global array.
 *
 * Otherwise, we apply the tiling to the index.
 * This tiling is of the form
 *
 *	[D -> A] -> T
 *
 * where D corresponds to the outer tile->depth dimensions of
 * the kernel schedule.
 * The index is of the form
 *
 *	L -> A
 *
 * We update the tiling to refer to the AST loop iterators
 *
 *	[L -> A] -> T
 *
 * and combine it with the index to obtain a tiled index expression in terms
 * of the AST loop iterators
 *
 *	L -> T
 *
 * Note that while the tiling applies directly to an outer array.
 * the index may refer to some subfield of this outer array.
 * In such cases, the result will refer to the same subfield of the tile.
 * That is, an index expression of the form  L -> F(A) will be transformed
 * into an index expression of the form L -> F(T).
 */
static __isl_give isl_multi_pw_aff *transform_index(
    __isl_take isl_multi_pw_aff *index, __isl_keep isl_id *ref_id,
    void *user)
{
  struct autosa_transform_data *data = (struct autosa_transform_data *)user;
  struct autosa_stmt_access *access;
  struct autosa_array_ref_group *group;
  struct autosa_array_tile *tile;
  isl_pw_multi_aff *iterator_map;
  int i;
  int dim;
  const char *name;
  isl_space *space;
  isl_multi_pw_aff *tiling;
  isl_pw_multi_aff *pma;
  isl_pw_multi_aff *sched2depth;
  isl_pw_multi_aff *sched2copy;

  data->array = NULL;

  iterator_map = isl_pw_multi_aff_copy(data->iterator_map);
  index = isl_multi_pw_aff_pullback_pw_multi_aff(index, iterator_map);

  if (!data->kernel)
    return index;

  access = find_access(data->accesses, ref_id);
  if (!access)
    return index;
  if (!isl_map_has_tuple_name(access->access, isl_dim_out))
    return index;

  name = get_outer_array_name(access->access);
  if (!name)
    return isl_multi_pw_aff_free(index);
  i = find_array_index(data->kernel, name);
  if (i < 0)
    isl_die(isl_multi_pw_aff_get_ctx(index), isl_error_internal,
            "cannot find array",
            return isl_multi_pw_aff_free(index));
  data->local_array = &data->kernel->array[i];
  data->array = data->local_array->array;
  group = find_ref_group(data->local_array, access);
  data->group = group;
  if (!group)
  {
    data->global = 1;
    data->reg = 1;
    return index;
  }

  tile = autosa_array_ref_group_tile(group);
  data->global = !tile;
  data->reg = !tile;
  if (!tile)
    return index;

  /* recompute the sched2copy for each index. */
  if (group->group_type == AUTOSA_PE_GROUP)
  {
    sched2copy = compute_sched_to_copy_group(isl_pw_multi_aff_copy(
                                                 data->iterator_map),
                                             group);
  }

  space = isl_space_domain(isl_multi_aff_get_space(tile->tiling));
  space = isl_space_range(isl_space_unwrap(space));
  space = isl_space_map_from_set(space);
  pma = isl_pw_multi_aff_identity(space);
  if (group->group_type == AUTOSA_PE_GROUP)
  {
    sched2depth = sched2copy;
  }
  else
  {
    sched2depth = isl_pw_multi_aff_copy(data->sched2copy);
  }
  dim = isl_pw_multi_aff_dim(sched2depth, isl_dim_out);
  sched2depth = isl_pw_multi_aff_drop_dims(sched2depth, isl_dim_out,
                                           tile->depth, dim - tile->depth);
  pma = isl_pw_multi_aff_product(sched2depth, pma);
  tiling = isl_multi_pw_aff_from_multi_aff(
      isl_multi_aff_copy(tile->tiling));
  tiling = isl_multi_pw_aff_pullback_pw_multi_aff(tiling, pma);

  index = tile_outer(index, tiling);

  return index;
}

/* Dereference "expr" by adding an index [0].
 * The original "expr" is assumed not to have any indices.
 *
 * If "expr" is a member access, then the dereferencing needs
 * to be applied to the structure argument of this member access.
 */
static __isl_give isl_ast_expr *dereference(__isl_take isl_ast_expr *expr)
{
  isl_ctx *ctx;
  isl_ast_expr *arg0, *res;
  isl_ast_expr_list *list;

  arg0 = isl_ast_expr_get_op_arg(expr, 0);
  if (!arg0)
    return isl_ast_expr_free(expr);
  if (isl_ast_expr_get_type(arg0) == isl_ast_expr_op &&
      isl_ast_expr_get_op_type(arg0) == isl_ast_op_member)
  {
    isl_ast_expr *arg;

    arg = isl_ast_expr_get_op_arg(arg0, 0);
    arg = dereference(arg);
    arg0 = isl_ast_expr_set_op_arg(arg0, 0, arg);
    expr = isl_ast_expr_set_op_arg(expr, 0, arg0);

    return expr;
  }
  isl_ast_expr_free(arg0);

  ctx = isl_ast_expr_get_ctx(expr);
  res = isl_ast_expr_from_val(isl_val_zero(ctx));
  list = isl_ast_expr_list_from_ast_expr(res);
  res = isl_ast_expr_get_op_arg(expr, 0);
  res = isl_ast_expr_access(res, list);
  isl_ast_expr_free(expr);

  return res;
}

/* Linearize the index expression "expr" based on the array bounds
 * of "array".
 *
 * That is, transform expression
 *
 *	A[i_0][i_1]...[i_n]
 *
 * to
 *
 *	A[(..((i_0 * b_1 + i_1) ... ) * b_n + i_n]
 *
 * where b_0, b_1, ..., b_n are the bounds on the array.
 *
 * If the base of "expr" is a member access, then the linearization needs
 * to be applied to the structure argument of this member access.
 *
 * In the base case, if "expr" has no arguments (other than the name of
 * the array), then we are passing an entire array to a function.
 * In this case, there is nothing to linearize.
 * Note that at this point an expression with no arguments can
 * only be an entire array because the scalar case and
 * the case of single struct are handled by the caller.
 *
 * If the number of specified index expressions in "expr"
 * is smaller than the dimension of the accessed array,
 * then the missing i_j also do not appear in the linearized expression.
 * Furthermore, since such an expression does not refer to a single
 * element while the default linearized expression would refer to
 * a single element, we return the expression
 *
 *	A + (..((i_0 * b_1 + i_1) ... ) * b_l + i_l)
 *
 * instead.  Note that because of the special case handling above,
 * we can assume here that there is at least one index expression.
 */
__isl_give isl_ast_expr *autosa_local_array_info_linearize_index(
    struct autosa_local_array_info *array, __isl_take isl_ast_expr *expr)
{
  int i, n;
  isl_ast_expr *arg0;
  isl_ast_expr *res;
  isl_ast_expr_list *list;

  arg0 = isl_ast_expr_get_op_arg(expr, 0);
  if (isl_ast_expr_get_type(arg0) == isl_ast_expr_op &&
      isl_ast_expr_get_op_type(arg0) == isl_ast_op_member)
  {
    isl_ast_expr *arg;

    arg = isl_ast_expr_get_op_arg(arg0, 0);
    arg = autosa_local_array_info_linearize_index(array, arg);
    arg0 = isl_ast_expr_set_op_arg(arg0, 0, arg);
    expr = isl_ast_expr_set_op_arg(expr, 0, arg0);

    return expr;
  }
  isl_ast_expr_free(arg0);

  if (isl_ast_expr_get_op_n_arg(expr) == 1)
    return expr;

  n = isl_ast_expr_get_op_n_arg(expr);
  res = isl_ast_expr_get_op_arg(expr, 1);
  for (i = 1; i < array->n_index; ++i)
  {
    isl_ast_expr *expr_i;

    expr_i = isl_ast_expr_get_op_arg(array->bound_expr, 1 + i);
    res = isl_ast_expr_mul(res, expr_i);

    if (i + 1 >= n)
      continue;
    expr_i = isl_ast_expr_get_op_arg(expr, i + 1);
    res = isl_ast_expr_add(res, expr_i);
  }

  if (1 + array->n_index > n)
  {
    res = isl_ast_expr_add(isl_ast_expr_get_op_arg(expr, 0), res);
  }
  else
  {
    list = isl_ast_expr_list_from_ast_expr(res);
    res = isl_ast_expr_get_op_arg(expr, 0);
    res = isl_ast_expr_access(res, list);
  }

  isl_ast_expr_free(expr);

  return res;
}

/* AST expression transformation callback for pet_stmt_build_ast_exprs.
 *
 * If the AST expression refers to an array that is not accessed
 * at all, then this means the value of the expression is not used,
 * so we might as well print zero (NULL pointer) instead.
 *
 * If the AST expression refers to a global scalar that is not
 * a read-only scalar, then its address was passed to the kernel and
 * we need to dereference it.
 *
 * If the AST expression refers to an access to a global array,
 * then we linearize the access exploiting the bounds in data->local_array.
 */
static __isl_give isl_ast_expr *transform_expr(__isl_take isl_ast_expr *expr,
                                               __isl_keep isl_id *id, void *user)
{
  struct autosa_transform_data *data = (struct autosa_transform_data *)user;

  if (!data->array)
    return expr;

  if (!data->array->accessed)
  {
    isl_ctx *ctx;

    ctx = isl_ast_expr_get_ctx(expr);
    isl_ast_expr_free(expr);
    return isl_ast_expr_from_val(isl_val_zero(ctx));
  }
  if (autosa_array_is_read_only_scalar(data->array))
    return expr;
  if (!data->global)
    return expr;
  if (data->array->n_index == 0)
    return dereference(expr);
  if (!data->array->linearize)
    return expr;

  return autosa_local_array_info_linearize_index(data->local_array, expr);
}

/* This function is called for each instance of a user statement
 * in the kernel "kernel", identified by "autosa_stmt".
 * "kernel" may be NULL if we are not inside a kernel.
 *
 * We attach a struct autosa_kernel_stmt to the "node", containing
 * a computed AST expression for each access, through an annotation
 * with name "user".
 * These AST expressions are computed from iterator_map,
 * which expresses the domain elements in terms of the generated loops, 
 * and sched2copy, which expresses the outer copy_schedule_dim dimensions of
 * the kernel schedule computed by AutoSA in terms of the generated loops.
 */
static __isl_give isl_ast_node *create_domain_leaf(
    struct autosa_kernel *kernel, __isl_take isl_ast_node *node,
    __isl_keep isl_ast_build *build, struct autosa_stmt *autosa_stmt)
{
  struct autosa_transform_data data;
  struct autosa_kernel_stmt *stmt;
  isl_ctx *ctx;
  isl_id *id;
  isl_pw_multi_aff *sched2copy;
  isl_map *map;
  isl_pw_multi_aff *iterator_map;
  isl_union_map *schedule;

  if (!node)
    return NULL;
  ctx = isl_ast_node_get_ctx(node);

  stmt = isl_calloc_type(ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

  schedule = isl_ast_build_get_schedule(build);
  map = isl_map_reverse(isl_map_from_union_map(schedule));
  iterator_map = isl_pw_multi_aff_from_map(map);
  if (kernel)
    sched2copy = compute_sched_to_copy(kernel,
                                       isl_pw_multi_aff_copy(iterator_map));
  else
    sched2copy = NULL;

  stmt->type = AUTOSA_KERNEL_STMT_DOMAIN;
  stmt->u.d.stmt = autosa_stmt;

  data.kernel = kernel;
  data.accesses = stmt->u.d.stmt->accesses;
  data.iterator_map = iterator_map;
  data.sched2copy = sched2copy;
  stmt->u.d.ref2expr = pet_stmt_build_ast_exprs(stmt->u.d.stmt->stmt,
                                                build, &transform_index, &data,
                                                &transform_expr, &data);
  isl_pw_multi_aff_free(iterator_map);
  isl_pw_multi_aff_free(sched2copy);

  id = isl_id_alloc(ctx, "user", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* Does "array" need to be allocated on the device?
 * If it is a read-only scalar, then it will be passed as an argument
 * to the kernel and therefore does not require any allocation.
 * If this device memory is not accessed at all, then it does not
 * need to be allocated either.
 */
int autosa_array_requires_device_allocation(struct autosa_array_info *array)
{
  if (autosa_array_is_read_only_scalar(array))
    return 0;
  if (!array->global)
    return 0;
  return 1;
}

/* Build AST expressions for the device array sizes of all arrays in "prog"
 * that require allocation on the device using "build", as well as
 * for the original array sizes of all arrays that need to be declared
 * on the host.
 * "node" is freed in case of error.
 */
static __isl_give isl_ast_node *build_array_bounds(
    __isl_take isl_ast_node *node, struct autosa_prog *prog,
    __isl_keep isl_ast_build *build)
{
  int i;

  for (i = 0; i < prog->n_array; ++i)
  {
    struct autosa_array_info *array = &prog->array[i];
    isl_multi_pw_aff *size;
    isl_ast_expr *expr;

    if (!autosa_array_requires_device_allocation(array))
      continue;

    size = isl_multi_pw_aff_copy(array->bound);
    expr = ppcg_build_size_expr(size, build);
    array->bound_expr = expr;
    if (!expr)
      return isl_ast_node_free(node);
  }

  for (i = 0; i < prog->n_array; ++i)
  {
    struct autosa_array_info *array = &prog->array[i];
    isl_set *extent;
    isl_multi_pw_aff *size;
    isl_ast_expr *expr;

    if (!array->declare_local)
      continue;
    extent = isl_set_copy(array->declared_extent);
    size = ppcg_size_from_extent(extent);
    expr = ppcg_build_size_expr(size, build);
    array->declared_size = expr;
    if (!expr)
      return isl_ast_node_free(node);
  }

  return node;
}

/* This function is called for each statement node in the AST
 * for copying to or from local memory.
 * Attach a pointer to a polysa_kernel_stmt representing the copy
 * statement to the node.
 * The statement name is "read" or "write", depending on whether we are
 * reading from global memory or writing to global memory.
 *
 * The schedule is of the form
 *
 *	type[D -> A] -> L
 *
 * where D corresponds to the outer tile->depth dimensions of
 * the kernel schedule, A to the global array and L to the outer
 * generated AST schedule.
 * We compute the inverse and strip off the type, resulting in
 *
 *	L -> [D -> A]
 *
 * We combine this mapping with on the one hand the projection
 *
 *	[D -> A] -> A
 *
 * and on the other hand the group tiling
 *
 *	[D -> A] -> T
 *
 * resulting in
 *
 *	L -> A		and 	L -> T
 *
 * and store the corresponding expressions in stmt->index and stmt->local_index,
 * where stmt points to the ppcg_kernel_stmt that is attached to the node.
 * stmt->index is linearized if the global memory array is linearized.
 */
static __isl_give isl_ast_node *create_access_leaf(struct autosa_kernel *kernel,
                                                   struct autosa_array_ref_group *group, __isl_take isl_ast_node *node,
                                                   __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  struct autosa_array_tile *tile;
  isl_id *id;
  isl_ast_expr *expr;
  isl_space *space;
  isl_map *access;
  isl_pw_multi_aff *pma, *pma2;
  const char *type;

  stmt = isl_calloc_type(kernel->ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

  /* type[D -> A] -> L */
  access = isl_map_from_union_map(isl_ast_build_get_schedule(build));
  type = isl_map_get_tuple_name(access, isl_dim_in);
  stmt->u.c.read = type && !strcmp(type, "read");
  /* L -> type[D -> A] */
  access = isl_map_reverse(access);
  pma = isl_pw_multi_aff_from_map(access);
  pma = isl_pw_multi_aff_reset_tuple_id(pma, isl_dim_out);
  space = isl_space_range(isl_pw_multi_aff_get_space(pma));
  space = isl_space_unwrap(space);
  /* [D -> A] -> A */
  pma2 = isl_pw_multi_aff_range_map(space);
  /* L -> A */
  pma2 = isl_pw_multi_aff_pullback_pw_multi_aff(pma2,
                                                isl_pw_multi_aff_copy(pma));
  expr = isl_ast_build_access_from_pw_multi_aff(build, pma2);
  if (group->array->linearize)
    expr = autosa_local_array_info_linearize_index(group->local_array,
                                                   expr);
  stmt->u.c.index = expr;

  tile = autosa_array_ref_group_tile(group);
  /* [D -> A] -> T */
  pma2 = isl_pw_multi_aff_from_multi_aff(
      isl_multi_aff_copy(tile->tiling));
  /* L -> T */
  pma2 = isl_pw_multi_aff_pullback_pw_multi_aff(pma2, pma);
  expr = isl_ast_build_access_from_pw_multi_aff(build, pma2);
  stmt->u.c.local_index = expr;

  stmt->u.c.array = group->array;
  stmt->u.c.local_array = group->local_array;
  stmt->type = AUTOSA_KERNEL_STMT_COPY;

  id = isl_id_alloc(kernel->ctx, "copy", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* This function is called for each instance of a user statement
 * in the kernel. This may be one of the original user statements
 * or a statement introduced by AutoSA.
 *
 * We first check if the statement id corresponds to a autosa statement,
 * which indicates the statement is an original user statement. Any statement
 * that is not an original user statement has been introduced by AutoSA and
 * requires special handling.
 *
 * If the user statement is one of the original user statements, then we call
 * create_domain_leaf.  
 * If it is "init_device", then we call build_array_bounds.  
 * Otherwise, we check if it is a copy statement and call the appropriate 
 * functions.  
 * Statements that copy an array to/from the device do not need any 
 * further treatment. Neither does "clear_device".
 */
static __isl_give isl_ast_node *at_domain(__isl_take isl_ast_node *node,
                                          __isl_keep isl_ast_build *build, void *user)
{
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_stmt *device_stmt;
  isl_ast_expr *expr, *arg;
  isl_id *id;
  int is_sync;
  const char *name;
  void *p;

  expr = isl_ast_node_user_get_expr(node);
  arg = isl_ast_expr_get_op_arg(expr, 0);
  id = isl_ast_expr_get_id(arg);
  name = isl_id_get_name(id);
  p = isl_id_get_user(id);
  isl_ast_expr_free(expr);
  isl_ast_expr_free(arg);

  device_stmt = find_stmt(data->prog, id);
  isl_id_free(id);

  if (device_stmt)
    return create_domain_leaf(data->kernel, node, build, device_stmt);
  if (!prefixcmp(name, "to_device_") || !prefixcmp(name, "from_device_"))
    return node;
  if (!strcmp(name, "init_device"))
    return build_array_bounds(node, data->prog, build);
  if (!strcmp(name, "clear_device"))
    return node;
  if (!strcmp(name, "drain_merge"))
    return node;
  if (!strcmp(name, "read") || !strcmp(name, "write"))
  {
    struct autosa_array_ref_group *group = (struct autosa_array_ref_group *)p;
    return create_access_leaf(data->kernel, group, node, build);
  }

  return node;
}

/* Build an access AST expression for the effective grid size using "build".
 * Store the result in kernel->grid_size_expr.
 */
static isl_stat build_grid_size(struct autosa_kernel *kernel,
                                __isl_keep isl_ast_build *build)
{
  isl_multi_pw_aff *size;

  size = isl_multi_pw_aff_copy(kernel->grid_size);
  size = isl_multi_pw_aff_set_tuple_name(size, isl_dim_out, "grid");
  kernel->grid_size_expr = ppcg_build_size_expr(size, build);

  if (!kernel->grid_size_expr)
    return isl_stat_error;
  return isl_stat_ok;
}

/* Build access AST expressions for the localized array sizes using "build".
 * Store the result in local->bound_expr.
 * Only do this for arrays for which localized bounds have been computed.
 */
static isl_stat build_local_array_sizes(struct autosa_kernel *kernel,
                                        __isl_keep isl_ast_build *build)
{
  int i;

  for (i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *local = &kernel->array[i];
    isl_multi_pw_aff *size;

    if (local->n_group == 0)
      continue;
    size = isl_multi_pw_aff_copy(local->bound);
    local->bound_expr = ppcg_build_size_expr(size, build);
    if (!local->bound_expr)
      return isl_stat_error;
  }

  return isl_stat_ok;
}

/* Build access AST expressions for the effective grid size and
 * the localized array sizes using "build".
 */
static isl_stat build_grid_and_local_array_sizes(struct autosa_kernel *kernel,
                                                 __isl_keep isl_ast_build *build)
{
  if (build_grid_size(kernel, build) < 0)
    return isl_stat_error;
  if (build_local_array_sizes(kernel, build) < 0)
    return isl_stat_error;
  return isl_stat_ok;
}

/* This function is called before the AST generator starts traversing
 * the schedule subtree of a node with mark "mark".
 *
 * If the mark is called "kernel", store the kernel pointer in data->kernel
 * for use in at_domain and build AST expressions for the grid size and
 * the localized array sizes.
 */
static isl_stat before_mark(__isl_keep isl_id *mark,
                            __isl_keep isl_ast_build *build, void *user)
{
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;

  if (!mark)
    return isl_stat_error;
  if (!strcmp(isl_id_get_name(mark), "kernel"))
  {
    data->kernel = (struct autosa_kernel *)isl_id_get_user(mark);
    if (build_grid_and_local_array_sizes(data->kernel, build) < 0)
      return isl_stat_error;
  }
  return isl_stat_ok;
}

/* This function is called after the AST generator has finished traversing
 * the schedule subtree of a mark node. "node" points to the corresponding
 * mark AST node.
 *
 * If the mark is called "kernel", then replace "node" by a user node
 * that "calls" the kernel, representing the launch of the kernel.
 * The original "node" is stored inside the kernel object so that
 * it can be used to print the device code.
 * Note that this assumes that a kernel is only launched once.
 * Also clear data->kernel.
 */
static __isl_give isl_ast_node *after_mark(__isl_take isl_ast_node *node,
                                           __isl_keep isl_ast_build *build, void *user)
{
  isl_ctx *ctx;
  isl_id *id;
  isl_ast_expr *expr;
  isl_ast_expr_list *list;
  struct autosa_kernel *kernel;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;

  ctx = isl_ast_node_get_ctx(node);
  id = isl_ast_node_mark_get_id(node);
  if (!id)
    return isl_ast_node_free(node);
  if (strcmp(isl_id_get_name(id), "kernel") || !data->kernel)
  {
    isl_id_free(id);
    return node;
  }
  kernel = data->kernel;
  data->kernel = NULL;
  kernel->space = isl_ast_build_get_schedule_space(build);
  kernel->tree = isl_ast_node_mark_get_node(node);
  isl_ast_node_free(node);
  expr = isl_ast_expr_from_id(isl_id_copy(id));
  list = isl_ast_expr_list_alloc(ctx, 0);
  expr = isl_ast_expr_call(expr, list);
  node = isl_ast_node_alloc_user(expr);
  node = isl_ast_node_set_annotation(node, id);

  return node;
}

/* Use isl to generate code for both the host and the device
 * from "schedule".
 * The device code is marked by "kernel" mark nodes in the schedule tree,
 * containing a pointer to a polysa_kernel object.
 * The returned AST only contains the AST for the host code.
 * The ASTs for the device code are embedded in polysa_kernel objects
 * attached to the leaf nodes that call "kernel".
 */
__isl_give isl_ast_node *sa_generate_code(struct autosa_gen *gen,
                                          __isl_take isl_schedule *schedule)
{
  struct autosa_at_domain_data data;
  isl_ast_build *build;
  isl_ast_node *tree;
  isl_id_list *iterators;
  int depth;

  if (schedule == NULL)
    return NULL;

  data.prog = gen->prog;
  data.kernel = NULL;

  depth = 0;
  if (isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth,
                                                  &depth) < 0)
    schedule = isl_schedule_free(schedule);
  build = isl_ast_build_alloc(gen->prog->ctx);
  iterators = ppcg_scop_generate_names(gen->prog->scop, depth, "c");
  build = isl_ast_build_set_iterators(build, iterators);
  build = isl_ast_build_set_at_each_domain(build, &at_domain, &data);
  build = isl_ast_build_set_before_each_mark(build, &before_mark, &data);
  build = isl_ast_build_set_after_each_mark(build, &after_mark, &data);
  if (gen->prog->scop->options->debug->dump_final_schedule)
    isl_schedule_dump(schedule);
  tree = isl_ast_build_node_from_schedule(build, schedule);
  isl_ast_build_free(build);

  return tree;
}

/* Initialize the autosa_at_domain_data struct. */
static void autosa_at_domain_data_init(
    struct autosa_at_domain_data *data, struct autosa_gen *gen)
{
  data->prog = gen->prog;
  data->kernel = NULL;
  data->module = NULL;
  data->filter_buffer = 0;
  data->under_unroll = 0;
  data->under_pipeline = 0;
  data->in_unroll_for = 0;
  data->in_pipeline_for = 0;
  data->in_for = 0;
  data->boundary = 0;
  data->pe_dummy = 0;
  data->pe_dummy_module = NULL;
  data->drain_merge_func = NULL;
}

/* Return a pointer to the autosa_array_ref_group in "local"
 * that contains the reference "access".
 * Return NULL if no such group can be found.
 */
static struct autosa_array_ref_group *find_ref_group_module(
    struct autosa_local_array_info *local, struct autosa_stmt_access *access)
{
  int i, j;

  for (i = 0; i < local->n_pe_group; ++i)
  {
    struct autosa_array_ref_group *group = local->pe_groups[i];

    for (j = 0; j < group->n_ref; ++j)
      if (group->refs[j] == access)
        return group;
  }

  return NULL;
}

/* Index transformation callback for pet_stmt_build_ast_exprs.
 *
 * "index" expresses the array indices in terms of statement iterators
 *
 * We first reformulate "index" in terms of the AST loop iterators.
 * Then we check if we are accessing the global array or
 * a shared/private copy.  In particular, if we are not inside a kernel
 * then we must be accessing a global array.
 * In the former case, we simply return
 * the updated index.  If "index" is an affine expression rather
 * than an array access, then we also return the updated index here.
 *
 * If no reference groups have been computed for the array,
 * then we can only be accessing the global array.
 *
 * Otherwise, we apply the tiling to the index.
 * This tiling is of the form
 *
 *	[D -> A] -> T
 *
 * where D corresponds to the outer tile->depth dimensions of
 * the kernel schedule.
 * The index is of the form
 *
 *	L -> A
 *
 * We update the tiling to refer to the AST loop iterators
 *
 *	[L -> A] -> T
 *
 * and combine it with the index to obtain a tiled index expression in terms
 * of the AST loop iterators
 *
 *	L -> T
 *
 * Note that while the tiling applies directly to an outer array.
 * the index may refer to some subfield of this outer array.
 * In such cases, the result will refer to the same subfield of the tile.
 * That is, an index expression of the form  L -> F(A) will be transformed
 * into an index expression of the form L -> F(T).
 */
static __isl_give isl_multi_pw_aff *transform_index_module(
    __isl_take isl_multi_pw_aff *index, __isl_keep isl_id *ref_id,
    void *user)
{
  struct autosa_transform_data *data = (struct autosa_transform_data *)user;
  struct autosa_stmt_access *access;
  struct autosa_array_ref_group *group;
  struct autosa_array_tile *tile;
  isl_pw_multi_aff *iterator_map;
  int i;
  int dim;
  const char *name;
  isl_space *space;
  isl_multi_pw_aff *tiling;
  isl_pw_multi_aff *pma;
  isl_pw_multi_aff *sched2depth;
  isl_pw_multi_aff *sched2copy;

  data->array = NULL;

  iterator_map = isl_pw_multi_aff_copy(data->iterator_map);
  index = isl_multi_pw_aff_pullback_pw_multi_aff(index, iterator_map);

  if (!data->kernel)
    return index;

  access = find_access(data->accesses, ref_id);
  if (!access)
    return index;
  if (!isl_map_has_tuple_name(access->access, isl_dim_out))
    return index;

  name = get_outer_array_name(access->access);
  if (!name)
    return isl_multi_pw_aff_free(index);
  i = find_array_index(data->kernel, name);
  if (i < 0)
    isl_die(isl_multi_pw_aff_get_ctx(index), isl_error_internal,
            "cannot find array",
            return isl_multi_pw_aff_free(index));
  data->local_array = &data->kernel->array[i];
  data->array = data->local_array->array;

  group = find_ref_group_module(data->local_array, access);
  data->group = group;
  if (!group)
  {
    data->global = 1;
    data->reg = 1;
    return index;
  }

  tile = autosa_array_ref_group_tile(group);
  data->global = !tile;
  data->reg = !tile;
  if (!tile)
    return index;

  /* recompute the sched2copy for each index. */
  if (group->group_type == AUTOSA_PE_GROUP)
  {
    sched2copy = compute_sched_to_copy_group(
        isl_pw_multi_aff_copy(data->iterator_map), group);
  }

  space = isl_space_domain(isl_multi_aff_get_space(tile->tiling));
  space = isl_space_range(isl_space_unwrap(space));
  space = isl_space_map_from_set(space);
  pma = isl_pw_multi_aff_identity(space);
  if (group->group_type == AUTOSA_PE_GROUP)
  {
    sched2depth = sched2copy;
  }
  else
  {
    sched2depth = isl_pw_multi_aff_copy(data->sched2copy);
  }
  dim = isl_pw_multi_aff_dim(sched2depth, isl_dim_out);
  sched2depth = isl_pw_multi_aff_drop_dims(sched2depth, isl_dim_out,
                                           tile->depth, dim - tile->depth);
  pma = isl_pw_multi_aff_product(sched2depth, pma);
  tiling = isl_multi_pw_aff_from_multi_aff(
      isl_multi_aff_copy(tile->tiling));
  tiling = isl_multi_pw_aff_pullback_pw_multi_aff(tiling, pma);
  index = tile_outer(index, tiling);

  return index;
}

/* AST expression transformation callback for pet_stmt_build_ast_exprs.
 *
 * If the AST expression refers to an array that is not accessed
 * at all, then this means the value of the expression is not used,
 * so we might as well print zero (NULL pointer) instead.
 *
 * If the AST expression refers to a global scalar that is not
 * a read-only scalar, then its address was passed to the kernel and
 * we need to dereference it.
 *
 * If the AST expression refers to an array reference that is put in 
 * the registers. We will modify the expr to a register access.
 *
 * If the AST expression refers to an access to a global array,
 * then we linearize the access exploiting the bounds in data->local_array.
 */
static __isl_give isl_ast_expr *transform_expr_module(__isl_take isl_ast_expr *expr,
                                                      __isl_keep isl_id *id, void *user)
{
  struct autosa_transform_data *data = (struct autosa_transform_data *)user;

  if (!data->array)
    return expr;

  if (!data->array->accessed)
  {
    isl_ctx *ctx;

    ctx = isl_ast_expr_get_ctx(expr);
    isl_ast_expr_free(expr);
    return isl_ast_expr_from_val(isl_val_zero(ctx));
  }
  if (autosa_array_is_read_only_scalar(data->array))
    return expr;
  if (!data->reg)
    return expr;
  if (data->reg)
  {
    isl_ctx *ctx;
    char *local_name;
    char buf[50];
    isl_id *id;
    isl_ast_expr *array;
    isl_ast_expr_list *indices;
    isl_ast_expr *indice;

    ctx = isl_ast_expr_get_ctx(expr);
    isl_ast_expr_free(expr);

    /* Create a register access. */
    isl_printer *p_str = isl_printer_to_str(ctx);
    p_str = autosa_array_ref_group_print_name(data->group, p_str);
    local_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    sprintf(buf, "%s", local_name);
    free(local_name);

    id = isl_id_alloc(ctx, buf, NULL);
    array = isl_ast_expr_from_id(id);
    indice = isl_ast_expr_from_val(isl_val_zero(ctx));
    indices = isl_ast_expr_list_from_ast_expr(indice);
    expr = isl_ast_expr_access(array, indices);

    return expr;
  }
  if (data->array->n_index == 0)
    return dereference(expr);
  if (!data->array->linearize)
    return expr;

  return autosa_local_array_info_linearize_index(data->local_array, expr);
}

/* This function is called for each instance of a user statement
 * in the kernel "kernel", identified by "autosa_stmt".
 * "kernel" may be NULL if we are not inside a kernel.
 *
 * We attach a struct autosa_kernel_stmt to the "node", containing
 * a computed AST expression for each access, through an annotation
 * with name "user".
 * These AST expressions are computed from iterator_map,
 * which expresses the domain
 * elements in terms of the generated loops, and sched2copy,
 * which expresses the outer copy_schedule_dim dimensions of
 * the kernel schedule computed by PPCG in terms of the generated loops.
 */
static __isl_give isl_ast_node *create_domain_leaf_module(
    struct autosa_kernel *kernel, __isl_take isl_ast_node *node,
    __isl_keep isl_ast_build *build, struct autosa_stmt *autosa_stmt)
{
  struct autosa_transform_data data;
  struct autosa_kernel_stmt *stmt;
  isl_ctx *ctx;
  isl_id *id;
  isl_pw_multi_aff *sched2copy;
  isl_map *map;
  isl_pw_multi_aff *iterator_map;
  isl_union_map *schedule;

  if (!node)
    return NULL;
  ctx = isl_ast_node_get_ctx(node);

  stmt = isl_calloc_type(ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

  schedule = isl_ast_build_get_schedule(build);
  map = isl_map_reverse(isl_map_from_union_map(schedule));
  iterator_map = isl_pw_multi_aff_from_map(map);
  if (kernel)
    sched2copy = compute_sched_to_copy(kernel,
                                       isl_pw_multi_aff_copy(iterator_map));
  else
    sched2copy = NULL;

  stmt->type = AUTOSA_KERNEL_STMT_DOMAIN;
  stmt->u.d.stmt = autosa_stmt;

  data.kernel = kernel;
  data.accesses = stmt->u.d.stmt->accesses;
  data.iterator_map = iterator_map;
  data.sched2copy = sched2copy;
  stmt->u.d.ref2expr = pet_stmt_build_ast_exprs(stmt->u.d.stmt->stmt,
                                                build, &transform_index_module, &data,
                                                &transform_expr_module, &data);

  isl_pw_multi_aff_free(iterator_map);
  isl_pw_multi_aff_free(sched2copy);

  id = isl_id_alloc(ctx, "user", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* This function extracts the reduce op in the stmt name, which is in the format of:
 * in/out_trans_reduce_[op]
 */
static char *extract_io_stmt_reduce_op(
  isl_ctx *ctx, const char *type)
{
  isl_printer *p_str;
  char *op;
  int loc = 0;
  char ch;
  int underscore_cnt = 0;

  p_str = isl_printer_to_str(ctx);  
  while ((ch = type[loc]) != '\0')
  {
    if (ch == '.')
      break;
    if (ch == '_')
      underscore_cnt++;
    else if (underscore_cnt == 3) {
      char buf[2];
      buf[0] = ch;
      buf[1] = '\0';
      p_str = isl_printer_print_str(p_str, buf);      
    }
    loc++;
  }

  op = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  return op;
}

/* AutoSA stmt is in the format of
 * [].[].[]
 * This function extracts the integer field at the pos-th position.
 * If the position is not found, -1 is returned.
 */
static int extract_autosa_stmt_int_field(
  isl_ctx *ctx, const char *type, int pos) 
{
  int loc = 0;
  char ch;
  int dot_time = 0;
  isl_printer *p_str;
  char *comp_str;
  int ret;

  while ((ch = type[loc]) != '\0')
  {
    if (ch == '.')
      dot_time++;
    if (dot_time == pos)
      break;
    loc++;
  }

  if (ch == '\0') {
    //std::string stmt(type);
    //std::string info = "[AutoSA] Error: Wrong pos: " + std::to_string(pos) + 
    //  " in stmt: " + stmt;
    //throw std::runtime_error(info);
    return -1;
  }

  p_str = isl_printer_to_str(ctx);
  loc++;
  while (((ch = type[loc]) != '\0') && ((ch = type[loc]) != '.'))
  {
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    p_str = isl_printer_print_str(p_str, buf);
    loc++;
  }

  comp_str = isl_printer_get_str(p_str);
  ret = atoi(comp_str);
  free(comp_str);
  isl_printer_free(p_str);

  return ret;
}

/* AutoSA stmt is in the format of
 * [].[].[]
 * This function extracts the string field at the pos-th position.
 * If the position is not found, NULL is returned.
 */
static __isl_give char *extract_autosa_stmt_str_field(
  isl_ctx *ctx, const char *type, int pos) 
{
  int loc = 0;
  char ch;
  int dot_time = 0;
  isl_printer *p_str;
  char *comp_str;  

  while ((ch = type[loc]) != '\0')
  {
    if (ch == '.')
      dot_time++;
    if (dot_time == pos)
      break;
    loc++;
  }

  if (ch == '\0') {    
    return NULL;
  }

  p_str = isl_printer_to_str(ctx);
  loc++;
  while (((ch = type[loc]) != '\0') && ((ch = type[loc]) != '.'))
  {
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    p_str = isl_printer_print_str(p_str, buf);
    loc++;
  }

  comp_str = isl_printer_get_str(p_str);  
  isl_printer_free(p_str);

  return comp_str;
}

static __isl_give isl_ast_node *create_serialize_leaf(struct autosa_kernel *kernel,
                                                      struct autosa_array_ref_group_pair *pair,
                                                      __isl_take isl_ast_node *node,
                                                      const char *name,
                                                      __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  struct autosa_array_ref_group *group;
  isl_ctx *ctx;
  isl_map *access;
  isl_set *set;
  isl_pw_multi_aff *pma, *pma2;
  isl_space *space;
  isl_ast_expr *expr;
  isl_id *id;

  stmt = isl_calloc_type(kernel->ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);
  stmt->type = AUTOSA_KERNEL_STMT_HOST_SERIALIZE;
  ctx = kernel->ctx;
  group = pair->local_group;

  /* Compute the global index. */
  /* type[D -> A] -> L */
  access = isl_map_from_union_map(isl_ast_build_get_schedule(build));
  /* L -> type[D -> A] */
  access = isl_map_reverse(access);
  pma = isl_pw_multi_aff_from_map(access);
  pma = isl_pw_multi_aff_reset_tuple_id(pma, isl_dim_out);
  space = isl_space_range(isl_pw_multi_aff_get_space(pma));
  space = isl_space_unwrap(space);
  /* [D -> A] -> A */
  pma2 = isl_pw_multi_aff_range_map(space);
  /* L -> A */
  pma2 = isl_pw_multi_aff_pullback_pw_multi_aff(pma2,
                                                pma);
  expr = isl_ast_build_access_from_pw_multi_aff(build, pma2);
  expr = autosa_local_array_info_linearize_index(group->local_array, expr);

  stmt->u.s.index = expr;
  stmt->u.s.in = !prefixcmp(name, "serialize") ? 1 : 0;
  stmt->u.s.group = pair->io_group;

  id = isl_id_alloc(kernel->ctx, "serialize", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* This function is called for each statement node in the AST
 * for transferring through fifos.
 * Attach a pointer to an autosa_kernel_stmt representing the io
 * statemet to the node.
 * The statement name is "in" or "out", depending on whether we are 
 * transferring in or out via fifos.
 *
 * The schedule is of the form
 *
 *  type[D -> A] -> L
 *
 * where D corresponds to the outer tile->depth dimensions of 
 * the kernel schedule, A to the global array and L to the outer 
 * generated AST schedule.
 * We compute the inverse and strip off the type, resulting in
 *
 *  L -> [D -> A]
 *
 * We combine this mapping with the group tiling
 *
 *  [D -> A] -> T
 *
 * resulting in
 *   
 *  L -> T
 *
 * and store the corresponding expressions in stmt->local_index,
 * where stmt points to the autosa_kernel_stmt that is attached to the node.
 */
static __isl_give isl_ast_node *create_io_leaf(struct autosa_kernel *kernel,
                                               struct autosa_hw_module *module,
                                               struct autosa_array_ref_group_pair *pair,
                                               __isl_take isl_ast_node *node,
                                               __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  struct autosa_array_tile *tile;
  isl_multi_aff *new_tiling;
  isl_map *access;
  const char *type;
  isl_pw_multi_aff *pma, *pma2;
  isl_space *space;
  isl_ast_expr *expr;
  isl_id *id;
  int is_trans;        // i/o transfer statement between on-chip modules
  int is_trans_dram;   // i/o transfer statement between dram and on-chip modules
  int is_trans_lower;  // i/o transfer statement with lower transfer
  int is_trans_buf;    // i/o transfer statement with local buffers
  int is_trans_boundary;
  int is_trans_reduce;
  int is_dummy;
  int is_dummy_reduce;
  int is_serialize; // is dram access to be serialized
  struct autosa_array_ref_group *group = pair->local_group;
  int depth;
  isl_ctx *ctx;

  stmt = isl_calloc_type(kernel->ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);
  ctx = kernel->ctx;

  /* type[D -> A] -> L */
  access = isl_map_from_union_map(isl_ast_build_get_schedule(build));
  isl_set *set = isl_map_domain(isl_set_unwrap(isl_map_domain(isl_map_copy(access))));
  depth = isl_set_dim(set, isl_dim_set);
  isl_set_free(set);

  type = isl_map_get_tuple_name(access, isl_dim_in);  
  /* The format of io_trans stmt name:
   * in/out_trans[_dram]/[_dram_serialize]/[_boundary]/[_reduce_(reduce_op)].[in_fifo_name].[out_fifo_name].[is_buffer].
   * [cur_pack_lane].[nxt_pack_lane].[coalesce_depth].[coalesce_bound]
   * or 
   * in/out[_dummy][_reduce].[fifo_name].[cur_pack_lane].[nxt_pack_lane]
   */

  /* Classify the io stmt type. */
  is_trans = !prefixcmp(type, "in_trans") || !prefixcmp(type, "out_trans");
  is_trans_dram = !prefixcmp(type, "in_trans_dram") || !prefixcmp(type, "out_trans_dram");
  is_trans_boundary = !prefixcmp(type, "in_trans_boundary") || !prefixcmp(type, "out_trans_boundary");
  is_trans_reduce = !prefixcmp(type, "in_trans_reduce") || !prefixcmp(type, "out_trans_reduce");
  if (is_trans)
  {    
    is_trans_buf = extract_autosa_stmt_int_field(ctx, type, 3);    
  }
  if (!is_trans)
  {
    is_dummy = !prefixcmp(type, "in_dummy") || !prefixcmp(type, "out_dummy");
  }
  else
  {
    is_dummy = 0;
  }
  if (is_dummy) {
    is_dummy_reduce = !prefixcmp(type, "in_dummy_reduce") || !prefixcmp(type, "out_dummy_reduce");
  } else {
    is_dummy_reduce = 0;
  }
  if (is_trans_dram)
  {
    is_serialize = !prefixcmp(type, "in_trans_dram_serialize") || !prefixcmp(type, "out_trans_dram_serialize");
  }
  
  stmt->u.i.simd_depth = pair->simd_depth;
  stmt->u.i.dummy = is_dummy;
  stmt->u.i.in = type && !prefixcmp(type, "in");
  stmt->u.i.buf = is_trans_buf;  
  stmt->u.i.serialize = is_serialize;
  if (is_trans) {
    stmt->u.i.data_pack = extract_autosa_stmt_int_field(ctx, type, 4);
    stmt->u.i.nxt_data_pack = extract_autosa_stmt_int_field(ctx, type, 5);
    stmt->u.i.coalesce_depth = extract_autosa_stmt_int_field(ctx, type, 6);
    stmt->u.i.coalesce_bound = extract_autosa_stmt_int_field(ctx, type, 7);
  } else {
    stmt->u.i.data_pack = extract_autosa_stmt_int_field(ctx, type, 2);
    stmt->u.i.nxt_data_pack = extract_autosa_stmt_int_field(ctx, type, 3);
    stmt->u.i.coalesce_depth = -1;
    stmt->u.i.coalesce_bound = -1;    
  }
  if (is_trans_reduce) {
    stmt->u.i.reduce = 1;
    stmt->u.i.reduce_op = extract_io_stmt_reduce_op(ctx, type);
  } else {
    stmt->u.i.reduce = is_dummy_reduce;
    stmt->u.i.reduce_op = NULL;
  }

  /* Compute the global index. */
  /* L -> type[D -> A] */
  access = isl_map_reverse(access);
  pma = isl_pw_multi_aff_from_map(access);
  pma = isl_pw_multi_aff_reset_tuple_id(pma, isl_dim_out);

  space = isl_space_range(isl_pw_multi_aff_get_space(pma));
  space = isl_space_unwrap(space);
  /* [D -> A] -> A */
  pma2 = isl_pw_multi_aff_range_map(space);
  /* L -> A */
  pma2 = isl_pw_multi_aff_pullback_pw_multi_aff(pma2,
                                                isl_pw_multi_aff_copy(pma));
  expr = isl_ast_build_access_from_pw_multi_aff(build, pma2);
  if (group->array->linearize)
  {
    expr = autosa_local_array_info_linearize_index(group->local_array,
                                                   expr);

    if (stmt->u.i.data_pack > 1)
    {
      /* Update the last dimension,
       * divide it by the data packing factor.
       */
      isl_ast_expr *arg, *div;
      arg = isl_ast_expr_get_op_arg(expr, 1);
      div = isl_ast_expr_from_val(isl_val_int_from_si(kernel->ctx, stmt->u.i.data_pack));
      arg = isl_ast_expr_div(arg, div);
      expr = isl_ast_expr_set_op_arg(expr, 1, arg);
    }
  }
  else
  {
    if (stmt->u.i.data_pack > 1)
    {
      /* Update the last dimension,
       * divide it by the data packing factor.
       */
      int n_arg;
      isl_ast_expr *arg, *div;
      n_arg = isl_ast_expr_get_op_n_arg(expr);
      arg = isl_ast_expr_get_op_arg(expr, n_arg - 1);
      div = isl_ast_expr_from_val(isl_val_int_from_si(kernel->ctx, stmt->u.i.data_pack));
      arg = isl_ast_expr_div(arg, div);
      expr = isl_ast_expr_set_op_arg(expr, n_arg - 1, arg);
    }
  }

  stmt->u.i.index = expr;

  /* Compute the local index. */
  tile = pair->local_tile;
  if (tile)
  {
    isl_ast_expr *arg, *div;
    int n_arg;

    /* [D -> A] -> T */
    pma2 = isl_pw_multi_aff_from_multi_aff(
        isl_multi_aff_copy(tile->tiling));
    if (tile->depth < depth)
    {
      /* Extend the D dimension to depth in pma2. */
      new_tiling = autosa_array_ref_group_recompute_tiling(tile, group, depth);
      isl_pw_multi_aff_free(pma2);
      pma2 = isl_pw_multi_aff_from_multi_aff(new_tiling);
    }

    /* L -> T */
    pma2 = isl_pw_multi_aff_pullback_pw_multi_aff(pma2, pma);
    expr = isl_ast_build_access_from_pw_multi_aff(build, pma2);
    stmt->u.i.local_index = expr;
    stmt->u.i.reg = 0;
  }
  else
  {
    /* Create a scalar expr. */
    isl_printer *p_str;
    char *local_name;
    char buf[50];
    isl_ast_expr *array, *indice;
    isl_ast_expr_list *indices;

    isl_pw_multi_aff_free(pma);
    p_str = isl_printer_to_str(kernel->ctx);
    p_str = autosa_array_ref_group_print_name(group, p_str);
    local_name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    sprintf(buf, "%s", local_name);
    free(local_name);

    id = isl_id_alloc(kernel->ctx, buf, NULL);
    array = isl_ast_expr_from_id(id);
    indice = isl_ast_expr_from_val(isl_val_zero(kernel->ctx));
    indices = isl_ast_expr_list_from_ast_expr(indice);
    expr = isl_ast_expr_access(array, indices);
    stmt->u.i.local_index = expr;
    stmt->u.i.reg = 1;
  }

  if (is_trans) {
    stmt->u.i.in_fifo_name = extract_autosa_stmt_str_field(ctx, type, 1);
    stmt->u.i.out_fifo_name = extract_autosa_stmt_str_field(ctx, type, 2);
  } else {
    stmt->u.i.in_fifo_name = extract_autosa_stmt_str_field(ctx, type, 1);
    stmt->u.i.out_fifo_name = extract_autosa_stmt_str_field(ctx, type, 1);
  }
  
  stmt->u.i.group = pair->io_group;
  stmt->u.i.module = module;
  stmt->u.i.array = group->array;
  stmt->u.i.local_array = group->local_array;
  if (is_trans)
  {
    if (is_trans_dram)
    {
      stmt->type = AUTOSA_KERNEL_STMT_IO_DRAM;
    }
    else
    {
      stmt->type = AUTOSA_KERNEL_STMT_IO_TRANSFER;      
      stmt->u.i.filter_sched_depth = -1;
      stmt->u.i.filter_param_id = -1;
      if (is_trans_boundary)
      {
        stmt->u.i.boundary = 1;
      }
      else
      {
        stmt->u.i.boundary = 0;
      }
    }
  }
  else
  {
    stmt->type = AUTOSA_KERNEL_STMT_IO;
  }

  id = isl_id_alloc(kernel->ctx, "io", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

static __isl_give isl_ast_node *create_drain_merge_leaf(struct autosa_kernel *kernel,
                                                        struct autosa_drain_merge_func *func, __isl_take isl_ast_node *node,
                                                        __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  struct autosa_array_ref_group *group;
  isl_ctx *ctx;
  isl_map *access;
  isl_pw_multi_aff *pma, *pma2;
  isl_space *space;
  isl_ast_expr *expr;
  isl_id *id;

  stmt = isl_calloc_type(kernel->ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);
  ctx = kernel->ctx;
  stmt->type = AUTOSA_KERNEL_STMT_DRAIN_MERGE;
  stmt->u.dm.func = func;

  /* Compute the global index. */
  /* type[D -> A] -> L */
  access = isl_map_from_union_map(isl_ast_build_get_schedule(build));
  /* L -> type[D -> A] */
  access = isl_map_reverse(access);
  pma = isl_pw_multi_aff_from_map(access);
  pma = isl_pw_multi_aff_reset_tuple_id(pma, isl_dim_out);
  space = isl_space_range(isl_pw_multi_aff_get_space(pma));
  space = isl_space_unwrap(space);
  /* [D -> A] -> A */
  pma2 = isl_pw_multi_aff_range_map(space);
  /* L -> A */
  pma2 = isl_pw_multi_aff_pullback_pw_multi_aff(pma2,
                                                isl_pw_multi_aff_copy(pma));
  expr = isl_ast_build_access_from_pw_multi_aff(build, pma2);
  isl_pw_multi_aff_free(pma);

  /* Linearize the index. */
  group = func->group;
  expr = autosa_local_array_info_linearize_index(group->local_array, expr);
  stmt->u.dm.index = expr;

  id = isl_id_alloc(ctx, "drain_merge", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

///* Exatract the boundary field from the module call type, which is in the format of:
// * io_module.[].boundary
// * or 
// * module_call.module_name.boundary
// * */
//static int extract_is_boundary(isl_ctx *ctx, const char *type)
//{
//  int ret_val;
//  char *boundary = extract_io_stmt_str_field(ctx, type, 2);
//  if (boundary && !strcmp(boundary, "boundary")) {
//    ret_val = 1;
//  } else {
//    ret_val = 0;
//  }
//  free(boundary);
//  return ret_val;
//}

/* Extract the module_name field from the module call type, which is in the format of:
 * module_call.module_name.boundary 
 */
static char *extract_module_name(isl_ctx *ctx, const char *type)
{
  char ch;
  int loc = 0;
  int n_dot = 0;
  isl_printer *p_str;
  char *module_name;

  while ((ch = type[loc]) != '\0')
  {
    if (ch == '.')
      n_dot++;
    if (n_dot == 1)
      break;
    loc++;
  }

  loc++;
  p_str = isl_printer_to_str(ctx);
  while ((ch = type[loc]) != '\0')
  {
    if (ch == '.')
      break;
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    p_str = isl_printer_print_str(p_str, buf);
    loc++;
  }

  module_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  return module_name;
}

/* There are two types of module call statements:
 * module_call_upper and module_call_lower
 * For module_call_lower, if the module is connected to PEs,
 * we will calculate the AST expression io_pe_expr which is the 
 * PE indices described by IO ids.
 */
static __isl_give isl_ast_node *create_ext_module_leaf(
    struct autosa_kernel *kernel,
    __isl_take isl_ast_node *node, struct autosa_hw_module *module,
    struct autosa_pe_dummy_module *pe_dummy_module,
    struct autosa_array_ref_group *group, const char *name,
    __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  isl_id *id;
  isl_ctx *ctx;
  isl_multi_aff *trans;
  isl_map *map;
  isl_pw_multi_aff *pma;
  isl_ast_expr *expr;

  ctx = isl_ast_node_get_ctx(node);
  stmt = isl_calloc_type(ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

  stmt->type = AUTOSA_KERNEL_STMT_EXT_MODULE;
  stmt->u.m.module = module;
  stmt->u.m.group = group;
  /* module_lower/upper.module_name.[is_boundary].[is_serialize] */
  stmt->u.m.boundary = extract_autosa_stmt_int_field(ctx, name, 2);  
  stmt->u.m.module_name = extract_autosa_stmt_str_field(ctx, name, 1);
  //stmt->u.m.dummy = !suffixcmp(stmt->u.m.module_name, "dummy");
  if (!suffixcmp(stmt->u.m.module_name, "dummy_in") || !suffixcmp(stmt->u.m.module_name, "dummy_out"))
    stmt->u.m.dummy = 1;
  else
    stmt->u.m.dummy = 0;
  stmt->u.m.pe_dummy_module = pe_dummy_module;
  if (!prefixcmp(name, "ext_module_lower"))
  {
    stmt->u.m.lower = 1;
    stmt->u.m.upper = 0;
  }
  else if (!prefixcmp(name, "ext_module_upper"))
  {
    stmt->u.m.lower = 0;
    stmt->u.m.upper = 1;
  }
  else
  {
    stmt->u.m.lower = 0;
    stmt->u.m.upper = 0;
  }

  id = isl_id_alloc(ctx, "ext_module", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* There are two types of module call statements:
 * module_call_upper and module_call_lower
 * For module_call_lower, if the module is connected to PEs,
 * we will calculate the AST expression io_pe_expr which is the 
 * PE indices described by IO ids.
 */
static __isl_give isl_ast_node *create_module_call_leaf(
    struct autosa_kernel *kernel,
    __isl_take isl_ast_node *node, struct autosa_hw_module *module,
    struct autosa_pe_dummy_module *pe_dummy_module,
    struct autosa_array_ref_group *group, const char *name,
    __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  isl_id *id;
  isl_ctx *ctx;
  isl_multi_aff *trans;
  isl_map *map;
  isl_pw_multi_aff *pma;
  isl_ast_expr *expr;

  ctx = isl_ast_node_get_ctx(node);
  stmt = isl_calloc_type(ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

//#ifdef _DEBUG
//  if (!strcmp(module->name, "U_drain_IO_L2_out")) {
//    isl_union_map *sched_tmp;
//    sched_tmp = isl_ast_build_get_schedule(build);
//    DBGUMAP(stdout, sched_tmp, kernel->ctx);
//    isl_space *space_tmp;
//    space_tmp = isl_ast_build_get_schedule_space(build);
//    DBGSPACE(stdout, space_tmp, kernel->ctx);
//  }
//#endif

  stmt->type = AUTOSA_KERNEL_STMT_MODULE_CALL;
  stmt->u.m.module = module;
  stmt->u.m.group = group;
  /* module_call_lower/upper.module_name.[is_boundary].[is_serialize].[lower_sched_val] */
  stmt->u.m.boundary = extract_autosa_stmt_int_field(ctx, name, 2);
  stmt->u.m.module_name = extract_autosa_stmt_str_field(ctx, name, 1);
  //stmt->u.m.dummy = !suffixcmp(stmt->u.m.module_name, "dummy");  
  if (!suffixcmp(stmt->u.m.module_name, "dummy_in") || !suffixcmp(stmt->u.m.module_name, "dummy_out"))
    stmt->u.m.dummy = 1;
  else
    stmt->u.m.dummy = 0;
  stmt->u.m.pe_dummy_module = pe_dummy_module;
  stmt->u.m.serialize = extract_autosa_stmt_int_field(ctx, name, 3);
  stmt->u.m.lower_sched_val = extract_autosa_stmt_int_field(ctx, name, 4);  
//#ifdef _DEBUG
//  if (!strcmp(stmt->u.m.module_name, "U_tmp_1_PE_dummy_in"))
//    printf("debug here\n");
//#endif

  if (!prefixcmp(name, "module_call_lower"))
  {
    stmt->u.m.lower = 1;
    stmt->u.m.upper = 0;
  }
  else if (!prefixcmp(name, "module_call_upper"))
  {
    stmt->u.m.lower = 0;
    stmt->u.m.upper = 1;
  }
  else
  {
    stmt->u.m.lower = 0;
    stmt->u.m.upper = 0;
  }

  if (stmt->u.m.lower)
  {
    if (!stmt->u.m.boundary)
    {
      if ((module->type == IO_MODULE || module->type == DRAIN_MODULE) && !group->io_pe_expr)
      {
        if (module->to_pe)
        {
          isl_union_map *umap = isl_ast_build_get_schedule(build);
          isl_union_set *uset = isl_union_map_range(umap);
          isl_set *set = isl_set_from_union_set(uset);
          isl_map *map = isl_set_identity(set);
          map = isl_map_flatten_range(map);
          trans = isl_multi_aff_copy(group->io_trans);
          isl_map *map2 = isl_map_from_multi_aff(trans);
          map2 = isl_map_reverse(map2);
          map = isl_map_apply_range(map, map2);
          isl_pw_multi_aff *pma = isl_pw_multi_aff_from_map(map);
          expr = isl_ast_build_access_from_pw_multi_aff(build, pma);
          group->io_pe_expr = expr;
        }
      }
    }
    /* boundary module */
    if (stmt->u.m.boundary)
    {
      if ((module->type == IO_MODULE || module->type == DRAIN_MODULE) && !group->io_pe_expr_boundary)
      {
        if (module->to_pe)
        {
          isl_union_map *umap = isl_ast_build_get_schedule(build);
          isl_union_set *uset = isl_union_map_range(umap);
          isl_set *set = isl_set_from_union_set(uset);
          isl_map *map = isl_set_identity(set);
          map = isl_map_flatten_range(map);
          trans = isl_multi_aff_copy(group->io_trans);
          isl_map *map2 = isl_map_from_multi_aff(trans);
          map2 = isl_map_reverse(map2);
          map = isl_map_apply_range(map, map2);
          isl_pw_multi_aff *pma = isl_pw_multi_aff_from_map(map);
          expr = isl_ast_build_access_from_pw_multi_aff(build, pma);
          group->io_pe_expr_boundary = expr;
        }
      }
    }
  }

  id = isl_id_alloc(ctx, "module_call", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* For fifo decleration statements, we will compute the AST expressions of 
 * PE indices that are described by the IO ids if the fifo is connected to 
 * PEs.
 */
static __isl_give isl_ast_node *create_fifo_decl_leaf(
    struct autosa_kernel *kernel,
    __isl_take isl_ast_node *node, struct autosa_hw_module *module,
    struct autosa_array_ref_group *group, const char *name,
    __isl_keep isl_ast_build *build)
{
  struct autosa_kernel_stmt *stmt;
  isl_id *id;
  isl_ctx *ctx;
  isl_multi_aff *trans;
  isl_map *map;
  isl_pw_multi_aff *pma;
  isl_ast_expr *expr;

  ctx = isl_ast_node_get_ctx(node);
  stmt = isl_calloc_type(ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

  /* Generate the AST expr of io_trans. */
  if (module->type == PE_MODULE && !group->io_L1_pe_expr)
  {
    isl_union_map *umap = isl_ast_build_get_schedule(build);
    isl_union_set *uset = isl_union_map_range(umap);
    isl_set *set = isl_set_from_union_set(uset);
    isl_map *map = isl_set_identity(set);
    map = isl_map_flatten_range(map);
    trans = group->io_L1_trans;
    isl_map *map2 = isl_map_from_multi_aff(isl_multi_aff_copy(trans));
    map2 = isl_map_reverse(map2);
    map = isl_map_apply_range(map, map2);
    isl_pw_multi_aff *pma = isl_pw_multi_aff_from_map(map);
    expr = isl_ast_build_access_from_pw_multi_aff(build, pma);
    group->io_L1_pe_expr = expr;
  }

  stmt->type = AUTOSA_KERNEL_STMT_FIFO_DECL;
  stmt->u.m.module = module;
  stmt->u.m.group = group;
  if (!prefixcmp(name, "fifo_decl_boundary"))
    stmt->u.m.boundary = 1;
  else
    stmt->u.m.boundary = 0;
  id = isl_id_alloc(ctx, "fifo_decl", stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* Attach a statement to the user node that describes the IO module type.
 */
static __isl_give isl_ast_node *create_io_module_call_leaf(
    struct autosa_kernel *kernel,
    __isl_take isl_ast_node *node, struct autosa_hw_module *module,
    const char *name, __isl_keep isl_ast_build *build)
{
  isl_id *id;
  isl_ctx *ctx;
  struct autosa_kernel_stmt *stmt;

  ctx = isl_ast_node_get_ctx(node);
  stmt = isl_calloc_type(ctx, struct autosa_kernel_stmt);
  if (!stmt)
    return isl_ast_node_free(node);

  stmt->u.f.module = module;
  stmt->u.f.boundary = extract_autosa_stmt_int_field(ctx, name, 2);
  if (!prefixcmp(name, "io_module.inter_trans"))
    stmt->type = AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTER_TRANS;
  else if (!prefixcmp(name, "io_module.intra_trans"))
    stmt->type = AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTRA_TRANS;
  else if (!prefixcmp(name, "io_module.inter_intra"))
    stmt->type = AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTER_INTRA;
  else if (!prefixcmp(name, "io_module.intra_inter"))
    stmt->type = AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTRA_INTER;
  else if (!prefixcmp(name, "io_module.state_handle"))
    stmt->type = AUTOSA_KERNEL_STMT_IO_MODULE_CALL_STATE_HANDLE;
  id = isl_id_alloc(ctx, name, stmt);
  id = isl_id_set_free_user(id, &autosa_kernel_stmt_free);
  if (!id)
    autosa_kernel_stmt_free(stmt);
  return isl_ast_node_set_annotation(node, id);
}

/* This function is called for each instance of a user statement
 * in the kernel. This may be one of the original user statements
 * or a statement introduced by AutoSA.
 *
 * We first check if the statement id corresponds to a autosa statement,
 * which indicates the statement is an original user statement. Any statement
 * that is not an original user statement has been introduced by AutoSA and
 * requires special handling.
 *
 * If the user statement is one of the original user statements, then we call
 * create_domain_leaf.  
 * If it is "init_device", then we call build_array_bounds.  
 * Otherwise, we check if it is a copy statement and call the appropriate 
 * functions.  
 * Statements that copy an array to/from the device do not need any 
 * further treatment. Neither does "clear_device".
 */
static __isl_give isl_ast_node *at_domain_module(__isl_take isl_ast_node *node,
                                                 __isl_keep isl_ast_build *build, void *user)
{
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_stmt *device_stmt;
  isl_ast_expr *expr, *arg;
  isl_id *id;
  int is_sync;
  const char *name;
  void *p;

  expr = isl_ast_node_user_get_expr(node);
  arg = isl_ast_expr_get_op_arg(expr, 0);
  id = isl_ast_expr_get_id(arg);
  name = isl_id_get_name(id);
  p = isl_id_get_user(id);
  isl_ast_expr_free(expr);
  isl_ast_expr_free(arg);

  device_stmt = find_stmt(data->prog, id);
  isl_id_free(id);

  if (device_stmt)
    return create_domain_leaf_module(data->kernel, node, build, device_stmt);

//#ifdef _DEBUG
//  std::cout << name << std::endl;
//#endif

  if (!prefixcmp(name, "to_device_") || !prefixcmp(name, "from_device_"))
    return node;
  if (!strcmp(name, "init_device"))
    return build_array_bounds(node, data->prog, build);
  if (!strcmp(name, "clear_device"))
    return node;
  if (!strcmp(name, "read") || !strcmp(name, "write"))
  {
    struct autosa_array_ref_group *group = (struct autosa_array_ref_group *)p;
    return create_access_leaf(data->kernel, group, node, build);
  }
  if (!prefixcmp(name, "in") || !prefixcmp(name, "out"))
  {
    struct autosa_array_ref_group_pair *pair = (struct autosa_array_ref_group_pair *)p;
    return create_io_leaf(data->kernel, data->module, pair, node, build);
  }
  if (!prefixcmp(name, "module_call"))
  {
    /* module_call.[module_name]
     * module_call_lower.[module_name]
     */
    struct autosa_array_ref_group *group = NULL;
    if (!prefixcmp(name, "module_call_lower"))
      group = (struct autosa_array_ref_group *)p;
    return create_module_call_leaf(data->kernel, node, data->module, data->pe_dummy_module, group, name, build);
  }
  if (!prefixcmp(name, "fifo_decl"))
  {
    /* fifo_decl.[fifo_name]
     * fifo_decl_boundary.[fifo_name]
     */
    struct autosa_array_ref_group *group = (struct autosa_array_ref_group *)p;
    return create_fifo_decl_leaf(data->kernel, node, data->module, group, name, build);
  }
  if (!prefixcmp(name, "ext_module"))
  {
    /* set_ext_module_args_upper.[module_name]
     * set_ext_module_args_lower.[module_name]
     */
    struct autosa_array_ref_group *group = NULL;
    if (!prefixcmp(name, "ext_module_lower"))
      group = (struct autosa_array_ref_group *)p;
    return create_ext_module_leaf(data->kernel, node, data->module,
                                  data->pe_dummy_module, group, name, build);
  }
  if (!prefixcmp(name, "io_module"))
  {
    return create_io_module_call_leaf(data->kernel, node, data->module, name, build);
  }
  if (!prefixcmp(name, "drain_merge"))
  {
    return create_drain_merge_leaf(data->kernel, data->drain_merge_func, node, build);
  }
  if (!prefixcmp(name, "serialize") || !prefixcmp(name, "deserialize"))
  {
    struct autosa_array_ref_group_pair *pair = (struct autosa_array_ref_group_pair *)p;
    return create_serialize_leaf(data->kernel, pair, node, name, build);
  }

  return node;
}

/* This function is called before the AST generator starts traversing
 * the schedule subtree of a node with mark "mark".
 *
 * If the mark is called "kernel", store the kernel pointer in data->kernel
 * for use in at_domain_module.
 * If the mark is called "module", store the kernel pointer in data->module
 * for use in at_domain_module.
 */
static isl_stat before_mark_module(__isl_keep isl_id *mark,
                                   __isl_keep isl_ast_build *build, void *user)
{
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;

  if (!mark)
    return isl_stat_error;
  if (!strcmp(isl_id_get_name(mark), "kernel"))
  {
    data->kernel = (struct autosa_kernel *)isl_id_get_user(mark);
  }
  if (!strcmp(isl_id_get_name(mark), "module"))
  {
    data->module = (struct autosa_hw_module *)isl_id_get_user(mark);
  }
  if (!strcmp(isl_id_get_name(mark), "pe_dummy_module"))
  {
    data->pe_dummy_module = (struct autosa_pe_dummy_module *)isl_id_get_user(mark);
    data->in_for = 0;
  }
  if (!strcmp(isl_id_get_name(mark), "io_module.inter_trans") ||
      !strcmp(isl_id_get_name(mark), "io_module.intra_trans"))
  {
    data->filter_buffer = 1;
    data->in_for = 0;
  }
  if (!strcmp(isl_id_get_name(mark), "hls_pipeline"))
  {
    data->under_pipeline = 1;
  }
  if (!strcmp(isl_id_get_name(mark), "hls_unroll"))
  {
    data->under_unroll = 1;
  }
  if (!strcmp(isl_id_get_name(mark), "drain_merge"))
  {
    data->drain_merge_func = (struct autosa_drain_merge_func *)isl_id_get_user(mark);
  }
  if (!strcmp(isl_id_get_name(mark), "host_serialize"))
  {
    data->module = (struct autosa_hw_module *)isl_id_get_user(mark);
  }

  return isl_stat_ok;
}

/* This function is called after the AST generator has finished traversing
 * the schedule subtree of a mark node. "node" points to the corresponding
 * mark AST node.
 *
 * If the mark is called "module", then replace "node" by a user node
 * that "calls" the module, representing the launch of the module.
 * The original "node" is stored inside the module object so that
 * it can be used to print the device code.
 * Also clear data->module.
 */
static __isl_give isl_ast_node *after_mark_module(__isl_take isl_ast_node *node,
                                                  __isl_keep isl_ast_build *build, void *user)
{
  isl_ctx *ctx;
  isl_id *id;
  isl_ast_expr *expr;
  isl_ast_expr_list *list;
  struct autosa_kernel *kernel;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_hw_module *module;
  struct autosa_pe_dummy_module *pe_dummy_module;
  struct autosa_drain_merge_func *func;

  ctx = isl_ast_node_get_ctx(node);
  id = isl_ast_node_mark_get_id(node);
  if (!id)
    return isl_ast_node_free(node);

  if (!strcmp(isl_id_get_name(id), "kernel") && data->kernel)
  {
    isl_id_free(id);
    if (!data->kernel->space)
      data->kernel->space = isl_ast_build_get_schedule_space(build);
    data->kernel = NULL;
    return node;
  }
  if (!strcmp(isl_id_get_name(id), "io_module.inter_trans"))
  {
    module = data->module;
    if (!module->inter_space)
      module->inter_space = isl_ast_build_get_schedule_space(build);

    if (!data->boundary)
      module->inter_tree = isl_ast_node_mark_get_node(node);
    else
      module->boundary_inter_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, id);

    return node;
  }
  if (!strcmp(isl_id_get_name(id), "io_module.intra_trans"))
  {
    module = data->module;
    if (!module->intra_space)
      module->intra_space = isl_ast_build_get_schedule_space(build);

//#ifdef _DEBUG
//    printf("%s\n", module->name);
//    DBGASTNODE(stdout, node, isl_ast_node_get_ctx(node));
//#endif

    module->intra_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, id);

    return node;
  }
  if (!strcmp(isl_id_get_name(id), "drain_merge"))
  {
    func = data->drain_merge_func;
    func->device_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, id);

    return node;
  }
  if (!strcmp(isl_id_get_name(id), "host_serialize"))
  {
    module = data->module;
    data->module = NULL;
    module->serialize_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, id);

    return node;
  }
  if (!strcmp(isl_id_get_name(id), "hls_pipeline"))
  {
    isl_id_free(id);
    data->under_pipeline = 0;

    return node;
  }
  if (!strcmp(isl_id_get_name(id), "hls_unroll"))
  {
    isl_id_free(id);
    data->under_unroll = 0;

    return node;
  }
  if (strcmp(isl_id_get_name(id), "module") || !data->module)
  {
    isl_id_free(id);
    return node;
  }
  /* Prepare for boundary I/O module. */
  if (data->boundary && data->filter_buffer == 0)
  {
    module = data->module;
    data->module = NULL;
    module->boundary_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);
    if (!module->space)
      module->space = isl_ast_build_get_schedule_space(build);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, id);

    return node;
  }

  /* Prepare for PE dummy module */
  if (data->pe_dummy && data->filter_buffer == 0)
  {
    module = data->module;
    data->module = NULL;
    pe_dummy_module = data->pe_dummy_module;
    data->pe_dummy_module = NULL;
    pe_dummy_module->device_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);
    if (!module->space)
      module->space = isl_ast_build_get_schedule_space(build);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, id);

    return node;
  }

  if (!data->boundary && data->filter_buffer == 0)
  {
    module = data->module;
    data->module = NULL;
    module->device_tree = isl_ast_node_mark_get_node(node);
    isl_ast_node_free(node);
    if (!module->space)
      module->space = isl_ast_build_get_schedule_space(build);

    expr = isl_ast_expr_from_id(isl_id_copy(id));
    list = isl_ast_expr_list_alloc(ctx, 0);
    expr = isl_ast_expr_call(expr, list);
    node = isl_ast_node_alloc_user(expr);
    node = isl_ast_node_set_annotation(node, isl_id_copy(id));
  }
  isl_id_free(id);

  return node;
}

static __isl_give isl_id *before_for_module(
    __isl_keep isl_ast_build *build, void *user)
{
  isl_id *id;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_ast_node_userinfo *node_info;

  node_info = alloc_ast_node_userinfo();
  id = isl_id_alloc(isl_ast_build_get_ctx(build), "", node_info);
  id = isl_id_set_free_user(id, free_ast_node_userinfo);

  return id;
}

//static __isl_give isl_id *before_for_module_call(
//    __isl_keep isl_ast_build *build, void *user)
//{
//  isl_id *id;
//  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
//  struct autosa_ast_node_userinfo *node_info;
//
//#ifdef _DEBUG
//  if (!strcmp(data->module->name, "U_drain_IO_L2_out")) {
//    isl_union_map *sched_tmp;
//    sched_tmp = isl_ast_build_get_schedule(build);
//    DBGUMAP(stdout, sched_tmp, data->kernel->ctx);
//  }
//#endif
//
//  node_info = alloc_ast_node_userinfo();
//  id = isl_id_alloc(isl_ast_build_get_ctx(build), "", node_info);
//  id = isl_id_set_free_user(id, free_ast_node_userinfo);
//
//  return id;
//}

static __isl_give isl_ast_node *after_for_module(
    __isl_take isl_ast_node *node, __isl_keep isl_ast_build *build,
    void *user)
{
  isl_id *id;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_ast_node_userinfo *node_info;

  id = isl_ast_node_get_annotation(node);
  node_info = (struct autosa_ast_node_userinfo *)isl_id_get_user(id);

  //if (node_info->is_outermost_for)
  //{
  //node_info->is_outermost_for = 0;
  //data->in_for = 0;
  //}

  isl_id_free(id);

  return node;
}

/* Generate AST from the schedule for AutoSA hardware modules. 
 * If "iterator_prefix" is set, we will use it as the iterator prefix.
 * Otherwise, we use the default value "c".
 */
static __isl_give isl_ast_node *autosa_generate_ast_from_schedule(
    __isl_take isl_schedule *schedule,
    struct autosa_at_domain_data data, struct autosa_gen *gen,
    const char *iterator_prefix)
{
  isl_ast_build *build;
  isl_ast_node *tree;
  isl_id_list *iterators;
  int depth;

  if (schedule == NULL)
    return NULL;

  depth = 0;
  if (isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth,
                                                  &depth) < 0)
    schedule = isl_schedule_free(schedule);
  build = isl_ast_build_alloc(gen->prog->ctx);
  iterators = ppcg_scop_generate_names(gen->prog->scop, depth,
                                       iterator_prefix == NULL ? "c" : iterator_prefix);
  build = isl_ast_build_set_iterators(build, iterators);
  build = isl_ast_build_set_at_each_domain(build, &at_domain_module, &data);
  build = isl_ast_build_set_before_each_mark(build, &before_mark_module, &data);
  build = isl_ast_build_set_after_each_mark(build, &after_mark_module, &data);
  build = isl_ast_build_set_before_each_for(build, &before_for_module, &data);
  build = isl_ast_build_set_after_each_for(build, &after_for_module, &data);

  if (gen->prog->scop->options->debug->dump_final_schedule)
    isl_schedule_dump(schedule);
  tree = isl_ast_build_node_from_schedule(build, schedule);
  isl_ast_build_free(build);

  return tree;
}

//static __isl_give isl_schedule_node *insert_coalesce_mark(
//    __isl_take isl_schedule_node *node, void *user)
//{
//  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
//  {
//    /* Examine if there is any other band node above this node. */
//    isl_bool is_top = isl_bool_true;
//    isl_schedule_node *visit_node = isl_schedule_node_copy(node);
//    while (isl_schedule_node_has_parent(visit_node))
//    {
//      visit_node = isl_schedule_node_parent(visit_node);
//      if (isl_schedule_node_get_type(visit_node) == isl_schedule_node_band)
//      {
//        is_top = isl_bool_false;
//        break;
//      }
//    }
//    isl_schedule_node_free(visit_node);
//
//    if (is_top)
//    {
//      /* Insert the mark. */
//      isl_ctx *ctx;
//      isl_id *id;
//
//      ctx = isl_schedule_node_get_ctx(node);
//      id = isl_id_alloc(ctx, "hls_coalesce", NULL);
//      node = isl_schedule_node_insert_mark(node, id);
//    }
//  }
//  return node;
//}

///* Take in the schedule tree described by "schedule".
// * Insert a "hls_coalesce" mark above the top-level band node.
// */
//static __isl_give isl_schedule *insert_top_loop_coalesce(__isl_take isl_schedule *schedule)
//{
//  schedule = isl_schedule_map_schedule_node_bottom_up(schedule,
//                                                      &insert_coalesce_mark, NULL);
//  return schedule;
//}

struct loop_infinitize_check_data
{
  /* Indicates if we are checking the outermost loop bands. */
  isl_bool outer_for;
  struct autosa_hw_module *module;
  /* Indicates if we have found any infinitizable loop. */
  isl_bool found;
};

struct iterator_used_data
{
  isl_ast_expr *iterator;
  isl_bool used;
  struct autosa_hw_module *module;
  isl_bool has_inter_intra;
};

/* Search if the isl_ast_expr_id "key" exists in the ast_expr "expr".
 */
static isl_bool search_expr_id(__isl_keep isl_ast_expr *expr, __isl_keep isl_ast_expr *key)
{
  enum isl_ast_expr_type type;

  type = isl_ast_expr_get_type(expr);
  if (type == isl_ast_expr_id)
  {
    return isl_ast_expr_is_equal(expr, key);
  }
  else if (type == isl_ast_expr_int)
  {
    return isl_bool_false;
  }
  else if (type == isl_ast_expr_op)
  {
    isl_size n_arg = isl_ast_expr_op_get_n_arg(expr);
    for (int i = 0; i < n_arg; i++)
    {
      isl_ast_expr *arg = isl_ast_expr_op_get_arg(expr, i);
      isl_bool found = search_expr_id(arg, key);
      isl_ast_expr_free(arg);
      if (found == isl_bool_true)
        return isl_bool_true;
    }
  }

  return isl_bool_false;
}

struct search_id_to_expr_id_data
{
  bool found;
  isl_ast_expr *iterator;
};

isl_stat search_id_to_expr_id(__isl_take isl_id *key,
                              __isl_take isl_ast_expr *val, void *user)
{
  struct search_id_to_expr_id_data *data = (struct search_id_to_expr_id_data *)user;
  data->found = (int)search_expr_id(val, data->iterator) || data->found;  

  isl_id_free(key);
  isl_ast_expr_free(val);
  return isl_stat_ok;
}

static isl_bool iterator_used(__isl_keep isl_ast_node *node, void *user)
{
  struct iterator_used_data *data = (struct iterator_used_data *)user;
  enum isl_ast_node_type type;
  

  type = isl_ast_node_get_type(node);
  if (type == isl_ast_node_for)
  {
    isl_ast_expr *expr;
    isl_bool found = isl_bool_false;

    /* Init */
    expr = isl_ast_node_for_get_init(node);
    found = search_expr_id(expr, data->iterator);
    isl_ast_expr_free(expr);
    if (found)
    {
      data->used = isl_bool_true;
      return isl_bool_false;
    }

    /* Cond */
    expr = isl_ast_node_for_get_cond(node);
    found = search_expr_id(expr, data->iterator);
    isl_ast_expr_free(expr);
    if (found)
    {
      data->used = isl_bool_true;
      return isl_bool_false;
    }
  }
  else if (type == isl_ast_node_if)
  {
    isl_ast_expr *expr;
    isl_bool found = isl_bool_false;

    /* Cond */
    expr = isl_ast_node_if_get_cond(node);
    found = search_expr_id(expr, data->iterator);
    isl_ast_expr_free(expr);
    if (found)
    {
      data->used = isl_bool_true;
      return isl_bool_false;
    }
  }
  else if (type == isl_ast_node_block)
  {
    /* We do nothing here. */
    return isl_bool_true;
  }
  else if (type == isl_ast_node_mark)
  {
    /* We do nothing here. */
    return isl_bool_true;
  }
  else if (type == isl_ast_node_user)
  {
    isl_ast_expr *expr;
    isl_bool found = isl_bool_false;
    isl_id *id;
    struct autosa_kernel_stmt *stmt;

    id = isl_ast_node_get_annotation(node);
    stmt = (struct autosa_kernel_stmt *)isl_id_get_user(id);
    isl_id_free(id);

    if (stmt->type == AUTOSA_KERNEL_STMT_DOMAIN)
    {
      /* TODO: At present, we only test if the array index contains the iterator.
       */
      isl_id_to_ast_expr *ref2expr = stmt->u.d.ref2expr;
      struct search_id_to_expr_id_data local_data;
      local_data.found = isl_bool_false;
      local_data.iterator = data->iterator;
      isl_id_to_ast_expr_foreach(ref2expr, &search_id_to_expr_id, &local_data);
      if (local_data.found)
      {
        data->used = isl_bool_true;
        return isl_bool_false;
      }
    }
    else if (stmt->type == AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTER_TRANS ||
             stmt->type == AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTRA_TRANS ||
             stmt->type == AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTER_INTRA ||
             stmt->type == AUTOSA_KERNEL_STMT_IO_MODULE_CALL_INTRA_INTER)
    {
      isl_ast_node *nested_node;
      struct iterator_used_data nested_used_data;

      data->has_inter_intra = isl_bool_true;

      /* Search under the nested AST tree. */
      nested_node = data->module->inter_tree;
      nested_used_data.iterator = data->iterator;
      nested_used_data.used = data->used;
      nested_used_data.module = data->module;
      isl_ast_node_foreach_descendant_top_down(nested_node, &iterator_used,
                                               &nested_used_data);
      found = nested_used_data.used;
      if (found)
      {
        data->used = isl_bool_true;
        return isl_bool_false;
      }

      /* Search under the nested AST tree. */
      nested_node = data->module->intra_tree;
      nested_used_data.iterator = data->iterator;
      nested_used_data.used = data->used;
      nested_used_data.module = data->module;
      isl_ast_node_foreach_descendant_top_down(nested_node, &iterator_used,
                                               &nested_used_data);
      found = nested_used_data.used;
      if (found)
      {
        data->used = isl_bool_true;
        return isl_bool_false;
      }
    }
    else if (stmt->type == AUTOSA_KERNEL_STMT_IO_TRANSFER)
    {
      int filter_depth = stmt->u.i.filter_sched_depth;
      if (stmt->u.i.boundary)
        filter_depth = -1;
      if (filter_depth < 0)
        return isl_bool_true;

      /* Check if the iterator equals to c[filter_depth]. */
      isl_printer *p_str;
      char *filter_iterator;
      char *cur_iterator;
      p_str = isl_printer_to_str(isl_ast_node_get_ctx(node));
      p_str = isl_printer_print_str(p_str, "c");
      p_str = isl_printer_print_int(p_str, filter_depth);
      filter_iterator = isl_printer_get_str(p_str);
      p_str = isl_printer_flush(p_str);

      p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
      p_str = isl_printer_print_ast_expr(p_str, data->iterator);
      cur_iterator = isl_printer_get_str(p_str);
      isl_printer_free(p_str);

      if (!strcmp(filter_iterator, cur_iterator))
        found = isl_bool_true;
      free(filter_iterator);
      free(cur_iterator);

      if (found)
      {
        data->used = isl_bool_true;
        return isl_bool_false;
      }
    }
  }

  return isl_bool_true;
}

static isl_bool loop_infinitize_check(__isl_keep isl_ast_node *node, void *user)
{
  struct loop_infinitize_check_data *data = (struct loop_infinitize_check_data *)user;
  enum isl_ast_node_type type;

  /* Only check the for loops in the outermost loop band. */
  if (!data->outer_for)
    return isl_bool_false;

  type = isl_ast_node_get_type(node);
  if (type == isl_ast_node_block || type == isl_ast_node_user)
  {
    data->outer_for = isl_bool_false;
    return isl_bool_false;
  }
  if (type == isl_ast_node_for && !isl_ast_node_for_is_degenerate(node))
  {
    isl_ast_expr *iterator;
    isl_ast_node *body;
    isl_bool used = isl_bool_false;
    struct iterator_used_data used_data;
    isl_id *id;

    iterator = isl_ast_node_for_get_iterator(node);
    body = isl_ast_node_for_get_body(node);
    /* Examine if the iterator exists in any AST expressions in the sub tree. */
    used_data.iterator = iterator;
    used_data.used = isl_bool_false;
    used_data.module = data->module;
    used_data.has_inter_intra = isl_bool_false;
    isl_ast_node_foreach_descendant_top_down(body, &iterator_used, &used_data); // TODO

    if (!used_data.used)
    {
      /* This loop is legal to be infinitized. */
      struct autosa_ast_node_userinfo *node_info;

      id = isl_ast_node_get_annotation(node);
      if (id)
      {
        node_info = (struct autosa_ast_node_userinfo *)isl_id_get_user(id);
        if (node_info)
        {
          node_info->is_infinitize_legal = 1;
          if (!data->found)
          {
            node_info->is_first_infinitizable_loop = 1;
            data->found = isl_bool_true;
          }

          if (used_data.has_inter_intra)
          {
            isl_space *space;
            int n;
            isl_printer *p_str;
            char *iterator_str;
            /* Update the inter/intra_trans module space. 
             * Remove the corresponding iterators from the sub module space. 
             */
            p_str = isl_printer_to_str(isl_id_get_ctx(id));
            p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
            p_str = isl_printer_print_ast_expr(p_str, iterator);
            iterator_str = isl_printer_get_str(p_str);
            isl_printer_free(p_str);

            space = data->module->inter_space;
            n = isl_space_find_dim_by_name(space, isl_dim_set, iterator_str);
            if (n >= 0)
              space = isl_space_drop_dims(space, isl_dim_set, n, 1);
            data->module->inter_space = space;

            space = data->module->intra_space;
            n = isl_space_find_dim_by_name(space, isl_dim_set, iterator_str);
            if (n >= 0)
              space = isl_space_drop_dims(space, isl_dim_set, n, 1);
            data->module->intra_space = space;

            free(iterator_str);
          }
        }
        isl_id_free(id);
      }
    }
    else
    {
      /* Stop from here. */
      isl_ast_expr_free(iterator);
      isl_ast_node_free(body);
      return isl_bool_false;
    }

    isl_ast_expr_free(iterator);
    isl_ast_node_free(body);
  }

  return isl_bool_true;
}

/* Try to apply the loop infinitization optimization.
 * This optimization is useful for Intel devices since we can remove some 
 * for loops with a simple while (1) loop to reduce the loop control overheads.
 * We will examine the outermost for loop band from outside to inside.
 * For each for loop, we exmaine if the loop iterator appears in any AST
 * expression below. If not, this loop will be marked to be infinitized later.
 * When printing out for loops later, such loops will be skipped. 
 * Since we use the nested AST for module ASTs, we examine the 
 * module->tree.
 * If we encounter any AST node calling io_module.inter_trans/io_module.intra_trans,
 * we will search from module->intra_tree and module->inter_tree
 * else, we will search from module->device_tree.
 */
static void loop_infinitization_optimize(struct autosa_hw_module *module)
{
  if (module->double_buffer || module->to_mem)
    return;

  if (module->device_tree)
  {
    isl_ast_node *node = module->device_tree;
    struct loop_infinitize_check_data data = {isl_bool_true, module, isl_bool_false};
    isl_ast_node_foreach_descendant_top_down(node, &loop_infinitize_check, &data);
  }
  if (module->boundary_tree)
  {
    isl_ast_node *node = module->boundary_tree;
    struct loop_infinitize_check_data data = {isl_bool_true, module, isl_bool_false};
    isl_ast_node_foreach_descendant_top_down(node, &loop_infinitize_check, &data);
  }
}

/* Mark all for loop as visited.  
 */
static isl_bool update_for_visit(__isl_keep isl_ast_node *node, void *user)
{
  enum isl_ast_node_type type;

  type = isl_ast_node_get_type(node);
  if (type == isl_ast_node_for)
  {
    struct autosa_ast_node_userinfo *info;
    isl_id *id;

    id = isl_ast_node_get_annotation(node);
    if (id)
    {
      info = (struct autosa_ast_node_userinfo *)isl_id_get_user(id);
      info->visited = 1;
    }
    isl_id_free(id);
  }

  return isl_bool_true;
}

/* If the ast node is a for loop node, we will first extract the annonated 
 * userinfo from the node. If the loop is marked to be infinitized, we will 
 * skip this loop.
 * Otherwise, since we visit the AST in top-down manner, this is the outermost 
 * loop to be added with the loop_coalesce pragma.
 * We will mark all the chidren nodes of this node as visited.
 * Next time when we first meet an unvisited for node, that will be the other
 * outermost loop to be annodated. 
 */
static isl_bool loop_coalesce_update(__isl_keep isl_ast_node *node, void *user)
{
  //struct loop_coalesce_update_data *data = (struct loop_coalesce_update_data *)user;
  enum isl_ast_node_type type;

  type = isl_ast_node_get_type(node);
  if (type == isl_ast_node_for)
  {
    struct autosa_ast_node_userinfo *info;
    isl_id *id;

    id = isl_ast_node_get_annotation(node);
    if (id)
    {
      info = (struct autosa_ast_node_userinfo *)isl_id_get_user(id);
      if (info && !info->is_infinitize_legal && !info->visited)
      {
        /* This is the outermost loop to be coalesced. 
         * We will then visit all the children nodes and add the visit flag.
         */
        info->visited = 1;
        info->is_outermost_for = 1;
        /* Update the children. */
        isl_ast_node_foreach_descendant_top_down(node, &update_for_visit, NULL);
      }
      isl_id_free(id);
    }
  }

  return isl_bool_true;
}

/* This function will mark the outermost for loop which is not infinitized 
 * to be added with "loop_coalesce" pragma later in the generated OpenCL code.
 * We will examine all the AST trees to be printed for this module.
 */
static void loop_coalesce_optimize(struct autosa_hw_module *module)
{
  isl_ast_node *node;

  if (module->device_tree)
  {
    node = module->device_tree;
    isl_ast_node_foreach_descendant_top_down(node, &loop_coalesce_update, NULL);
  }
  if (module->inter_tree)
  {
    node = module->inter_tree;
    isl_ast_node_foreach_descendant_top_down(node, &loop_coalesce_update, NULL);
  }
  if (module->intra_tree)
  {
    node = module->intra_tree;
    isl_ast_node_foreach_descendant_top_down(node, &loop_coalesce_update, NULL);
  }
  if (module->boundary_outer_tree)
  {
    node = module->boundary_outer_tree;
    isl_ast_node_foreach_descendant_top_down(node, &loop_coalesce_update, NULL);
  }
  if (module->boundary_inter_tree)
  {
    node = module->boundary_inter_tree;
    isl_ast_node_foreach_descendant_top_down(node, &loop_coalesce_update, NULL);
  }
  if (module->boundary_tree)
  {
    node = module->boundary_tree;
    isl_ast_node_foreach_descendant_top_down(node, &loop_coalesce_update, NULL);
  }
}

/* There are three schedules to handle in this module:
 * - outer loop schedule
 * - inter trans schedule
 * - intra trans schedule
 * We will first generate AST for inter trans function and intra trans function.
 * The AST trees below the inter trans and intra trans mark are stored 
 * seperately.
 * The outer loop AST will print out these two AST trees while handling 
 * the inter trans and intra trans function calls.
 */
isl_stat sa_filter_buffer_io_module_generate_code(struct autosa_gen *gen,
                                                  struct autosa_hw_module *module)
{
  isl_schedule *schedule;
  struct autosa_at_domain_data data;
  isl_ast_node *tree;

  /* Generate AST for inter transfer function call. */
  schedule = module->inter_sched;
  //schedule = insert_top_loop_coalesce(schedule);
  autosa_at_domain_data_init(&data, gen);
  tree = autosa_generate_ast_from_schedule(schedule, data, gen,
                                           module->double_buffer && gen->options->autosa->double_buffer_style == 0 ? "inter_c" : NULL);
  isl_ast_node_free(tree);

  if (module->boundary)
  {
    /* Generate boundary module AST. */
    schedule = module->boundary_inter_sched;
    //schedule = insert_top_loop_coalesce(schedule);
    autosa_at_domain_data_init(&data, gen);
    data.boundary = 1;
    tree = autosa_generate_ast_from_schedule(schedule, data, gen,
                                             module->double_buffer && gen->options->autosa->double_buffer_style == 0 ? "inter_c" : NULL);
    isl_ast_node_free(tree);
  }

  /* Generate AST for intra transfer function call. */
  schedule = module->intra_sched;  
//#ifdef _DEBUG
//  std::cout << module->name << "_intra_trans" << std::endl;
//  DBGSCHD(stdout, schedule, isl_schedule_get_ctx(schedule));
//#endif
  autosa_at_domain_data_init(&data, gen);
  tree = autosa_generate_ast_from_schedule(schedule, data, gen,
                                           module->double_buffer && gen->options->autosa->double_buffer_style == 0 ? "intra_c" : NULL);
  isl_ast_node_free(tree);
//#ifdef _DEBUG
//  DBGASTNODE(stdout, module->intra_tree, isl_ast_node_get_ctx(module->intra_tree));
//#endif

  /* Generate AST for outer loop function call. */
  schedule = module->outer_sched;  
  autosa_at_domain_data_init(&data, gen);
  tree = autosa_generate_ast_from_schedule(schedule, data, gen,
                                           module->double_buffer && gen->options->autosa->double_buffer_style == 0 ? "outer_c" : NULL);
  module->tree = tree;

  if (module->boundary)
  {
    /* Generate boundary module AST. */
    schedule = module->boundary_outer_sched;    
    autosa_at_domain_data_init(&data, gen);
    data.boundary = 1;
    tree = autosa_generate_ast_from_schedule(schedule, data, gen,
                                             module->double_buffer && gen->options->autosa->double_buffer_style == 0 ? "outer_c" : NULL);
    isl_ast_node_free(tree);
  }

  /* Perform loop infinitization optimization. */
  if (gen->options->target == AUTOSA_TARGET_INTEL_OPENCL &&
      gen->options->autosa->loop_infinitize)
  {
    loop_infinitization_optimize(module);
  }
  /* Perform loop coalesce optimization. 
   * This step should be always after the loop infinitization opt.
   */
  if (gen->options->target == AUTOSA_TARGET_INTEL_OPENCL)
  {
    loop_coalesce_optimize(module);
  }

  return isl_stat_ok;
}

/* Use isl to generate code for host data serialization/deserialization. 
 */
isl_stat sa_host_serialize_generate_code(struct autosa_gen *gen,
                                         struct autosa_hw_module *module)
{
  isl_schedule *schedule;
  struct autosa_at_domain_data data;
  isl_ast_node *tree;

  schedule = module->serialize_sched;
  autosa_at_domain_data_init(&data, gen);
  tree = autosa_generate_ast_from_schedule(schedule, data, gen, NULL);
  isl_ast_node_free(tree);

  return isl_stat_ok;
}

/* Use isl to generate code for the hw module from "schedule".
 * The device code of the hw module is marked by "module" mark nodes in the 
 * schedule tree, containing a pointer to a autosa_hw_module object.
 * The returned AST only contains the AST for the host code.
 * The ASTs for the device code are embedded in autosa_hw_module objects
 * attached to the leaf nodes that call "module".
 */
isl_stat sa_module_generate_code(struct autosa_gen *gen,
                                 struct autosa_hw_module *module)
{
  isl_schedule *schedule;
  struct autosa_at_domain_data data;
  isl_ast_node *tree;

//#ifdef _DEBUG
//  if (module->type == PE_MODULE) {
//    printf("debug here!\n");
//  }
//  DBGSCHD(stdout, module->sched, gen->ctx);
//#endif

  schedule = module->sched;  
  autosa_at_domain_data_init(&data, gen);
  tree = autosa_generate_ast_from_schedule(schedule, data, gen, NULL);
  module->tree = tree;

  if (module->boundary)
  {
    /* Generate boundary module AST */
    schedule = module->boundary_sched;
    autosa_at_domain_data_init(&data, gen);
    data.boundary = 1;
    tree = autosa_generate_ast_from_schedule(schedule, data, gen, NULL);
    isl_ast_node_free(tree);
  }

  if (module->n_pe_dummy_modules > 0)
  {
    /* Generate dummy module AST */
    for (int i = 0; i < module->n_pe_dummy_modules; i++)
    {
      struct autosa_pe_dummy_module *dummy_module = module->pe_dummy_modules[i];
      schedule = dummy_module->sched;
      autosa_at_domain_data_init(&data, gen);
      data.pe_dummy = 1;
      data.pe_dummy_module = dummy_module;
      tree = autosa_generate_ast_from_schedule(schedule, data, gen, NULL);
      isl_ast_node_free(tree);
    }
  }

  /* Perform loop infinitization optimization. */
  if (gen->options->target == AUTOSA_TARGET_INTEL_OPENCL &&
      gen->options->autosa->loop_infinitize)
  {
    loop_infinitization_optimize(module);
  }
  /* Perform loop coalesce optimization. 
   * This step should be always after the loop infinitization opt.
   */
  if (gen->options->target == AUTOSA_TARGET_INTEL_OPENCL)
  {
    loop_coalesce_optimize(module);
  }

  return isl_stat_ok;
}

isl_stat sa_drain_merge_generate_code(struct autosa_gen *gen,
                                      struct autosa_drain_merge_func *func)
{
  isl_schedule *schedule;
  struct autosa_at_domain_data data;
  isl_ast_node *tree;

  schedule = func->sched;
  autosa_at_domain_data_init(&data, gen);
  tree = autosa_generate_ast_from_schedule(schedule, data, gen, NULL);
  func->tree = tree;

  return isl_stat_ok;
}

/* This function is called after the AST generator has finished traversing
 * the schedule subtree of a mark node. "node" points to the corresponding
 * mark AST node.
 *
 * If the mark is called "fifo_decl", then replace "node" by a user node
 * that "calls" the fifo_decl, representing the printing of fifo decls.
 * We will store the AST node into the fifo_decl_wrapped_trees.
 */
static __isl_give isl_ast_node *after_mark_fifo_decl(
    __isl_take isl_ast_node *node,
    __isl_keep isl_ast_build *build, void *user)
{
  isl_ctx *ctx;
  isl_id *id;
  isl_ast_expr *expr;
  isl_ast_expr_list *list;
  struct autosa_kernel *kernel;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_hw_module *module;
  struct autosa_hw_top_module *top;

  ctx = isl_ast_node_get_ctx(node);
  id = isl_ast_node_mark_get_id(node);
  if (!id)
    return isl_ast_node_free(node);

  if (!strcmp(isl_id_get_name(id), "kernel") && data->kernel)
  {
    isl_id_free(id);
    if (!data->kernel->space)
      data->kernel->space = isl_ast_build_get_schedule_space(build);
    data->kernel = NULL;
    return node;
  }
  if (strcmp(isl_id_get_name(id), "module") || !data->module)
  {
    isl_id_free(id);
    return node;
  }
  top = data->top;
  data->top = NULL;
  top->n_fifo_decl_wrapped++;
  top->fifo_decl_wrapped_trees = (isl_ast_node **)realloc(
      top->fifo_decl_wrapped_trees,
      top->n_fifo_decl_wrapped * sizeof(isl_ast_node *));
  top->fifo_decl_wrapped_trees[top->n_fifo_decl_wrapped - 1] =
      isl_ast_node_mark_get_node(node);
  isl_ast_node_free(node);

  expr = isl_ast_expr_from_id(isl_id_copy(id));
  list = isl_ast_expr_list_alloc(ctx, 0);
  expr = isl_ast_expr_call(expr, list);
  node = isl_ast_node_alloc_user(expr);
  node = isl_ast_node_set_annotation(node, id);

  return node;
}

/* Generate code for declaring fifos given the input schedule "schedule". 
 */
__isl_give isl_ast_node *sa_fifo_decl_generate_code(
    struct autosa_gen *gen, __isl_take isl_schedule *schedule)
{
  struct autosa_at_domain_data data;
  isl_ast_build *build;
  isl_ast_node *tree;
  isl_id_list *iterators;

  int depth;

  if (schedule == NULL)
    return NULL;

  data.prog = gen->prog;
  data.kernel = NULL;
  data.module = NULL;
  data.top = gen->hw_top_module;

  depth = 0;
  if (isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth,
                                                  &depth) < 0)
    schedule = isl_schedule_free(schedule);
  build = isl_ast_build_alloc(gen->prog->ctx);
  iterators = ppcg_scop_generate_names(gen->prog->scop, depth, "c");
  build = isl_ast_build_set_iterators(build, iterators);
  build = isl_ast_build_set_at_each_domain(build, &at_domain_module, &data);
  build = isl_ast_build_set_before_each_mark(build, &before_mark_module, &data);
  build = isl_ast_build_set_after_each_mark(build, &after_mark_fifo_decl, &data);
  if (gen->prog->scop->options->debug->dump_final_schedule)
    isl_schedule_dump(schedule);
  tree = isl_ast_build_node_from_schedule(build, schedule);
  isl_ast_build_free(build);

  return tree;
}

/* This function is called after the AST generator has finished traversing
 * the schedule subtree of a mark node. "node" points to the corresponding
 * mark AST node.
 *
 * If the mark is called "module call", then replace "node" by a user node
 * that "calls" the module call, representing the printing of module calls.
 * We will store the AST node into the module_call_wrapped_trees.
 */
static __isl_give isl_ast_node *after_mark_module_call(
    __isl_take isl_ast_node *node,
    __isl_keep isl_ast_build *build, void *user)
{
  isl_ctx *ctx;
  isl_id *id;
  isl_ast_expr *expr;
  isl_ast_expr_list *list;
  struct autosa_kernel *kernel;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_hw_module *module;
  struct autosa_hw_top_module *top;

  ctx = isl_ast_node_get_ctx(node);
  id = isl_ast_node_mark_get_id(node);
  if (!id)
    return isl_ast_node_free(node);

  if (!strcmp(isl_id_get_name(id), "kernel") && data->kernel)
  {
    isl_id_free(id);
    if (!data->kernel->space)
      data->kernel->space = isl_ast_build_get_schedule_space(build);
    data->kernel = NULL;
    return node;
  }
  if (strcmp(isl_id_get_name(id), "module") || !data->module)
  {
    isl_id_free(id);
    return node;
  }
  top = data->top;
  data->top = NULL;
  top->n_module_call_wrapped++;
  top->module_call_wrapped_trees = (isl_ast_node **)realloc(
      top->module_call_wrapped_trees,
      top->n_module_call_wrapped * sizeof(isl_ast_node *));
  top->module_call_wrapped_trees[top->n_module_call_wrapped - 1] =
      isl_ast_node_mark_get_node(node);
  isl_ast_node_free(node);

  expr = isl_ast_expr_from_id(isl_id_copy(id));
  list = isl_ast_expr_list_alloc(ctx, 0);
  expr = isl_ast_expr_call(expr, list);
  node = isl_ast_node_alloc_user(expr);
  node = isl_ast_node_set_annotation(node, id);

  return node;
}

/* Generate code for calling modules given the input schedule "schedule". 
 */
__isl_give isl_ast_node *sa_module_call_generate_code(
    struct autosa_gen *gen, __isl_take isl_schedule *schedule)
{
  struct autosa_at_domain_data data;
  isl_ast_build *build;
  isl_ast_node *tree;
  isl_id_list *iterators;

  int depth;

  if (schedule == NULL)
    return NULL;

  data.prog = gen->prog;
  data.kernel = NULL;
  data.module = NULL;
  data.pe_dummy_module = NULL;
  data.top = gen->hw_top_module;

  depth = 0;
  if (isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth,
                                                  &depth) < 0)
    schedule = isl_schedule_free(schedule);
  build = isl_ast_build_alloc(gen->prog->ctx);
  iterators = ppcg_scop_generate_names(gen->prog->scop, depth, "c");
  build = isl_ast_build_set_iterators(build, iterators);
  build = isl_ast_build_set_at_each_domain(build, &at_domain_module, &data);
  build = isl_ast_build_set_before_each_mark(build, &before_mark_module, &data);
  build = isl_ast_build_set_after_each_mark(build, &after_mark_module_call, &data);
  //build = isl_ast_build_set_before_each_for(build, &before_for_module_call, &data);
  if (gen->prog->scop->options->debug->dump_final_schedule)
    isl_schedule_dump(schedule);
  tree = isl_ast_build_node_from_schedule(build, schedule);
  isl_ast_build_free(build);

  return tree;
}

/* This function is called after the AST generator has finished traversing
 * the schedule subtree of a mark node. "node" points to the corresponding
 * mark AST node.
 *
 * If the mark is called "module call", then replace "node" by a user node
 * that "calls" the module call, representing the printing of module calls.
 * We will store the AST node into the module_call_wrapped_trees.
 */
static __isl_give isl_ast_node *after_mark_ext_module(
    __isl_take isl_ast_node *node,
    __isl_keep isl_ast_build *build, void *user)
{
  isl_ctx *ctx;
  isl_id *id;
  isl_ast_expr *expr;
  isl_ast_expr_list *list;
  struct autosa_kernel *kernel;
  struct autosa_at_domain_data *data = (struct autosa_at_domain_data *)user;
  struct autosa_hw_module *module;
  struct autosa_hw_top_module *top;

  ctx = isl_ast_node_get_ctx(node);
  id = isl_ast_node_mark_get_id(node);
  if (!id)
    return isl_ast_node_free(node);

  if (!strcmp(isl_id_get_name(id), "kernel") && data->kernel)
  {
    isl_id_free(id);
    if (!data->kernel->space)
      data->kernel->space = isl_ast_build_get_schedule_space(build);
    data->kernel = NULL;
    return node;
  }
  if (strcmp(isl_id_get_name(id), "module") || !data->module)
  {
    isl_id_free(id);
    return node;
  }
  top = data->top;
  data->top = NULL;
  top->n_ext_module_wrapped++;
  top->ext_module_wrapped_trees = (isl_ast_node **)realloc(
      top->ext_module_wrapped_trees,
      top->n_ext_module_wrapped * sizeof(isl_ast_node *));
  top->ext_module_wrapped_trees[top->n_ext_module_wrapped - 1] =
      isl_ast_node_mark_get_node(node);
  isl_ast_node_free(node);

  expr = isl_ast_expr_from_id(isl_id_copy(id));
  list = isl_ast_expr_list_alloc(ctx, 0);
  expr = isl_ast_expr_call(expr, list);
  node = isl_ast_node_alloc_user(expr);
  node = isl_ast_node_set_annotation(node, id);

  return node;
}

/* Generate code for setting arguments of the io modules connected to the 
 * external memory given the input schedule "schedule". 
 */
__isl_give isl_ast_node *sa_set_ext_module_args_generate_code(
    struct autosa_gen *gen, __isl_take isl_schedule *schedule)
{
  struct autosa_at_domain_data data;
  isl_ast_build *build;
  isl_ast_node *tree;
  isl_id_list *iterators;

  int depth;

  if (schedule == NULL)
    return NULL;

  data.prog = gen->prog;
  data.kernel = NULL;
  data.module = NULL;
  data.pe_dummy_module = NULL;
  data.top = gen->hw_top_module;

  depth = 0;
  if (isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth,
                                                  &depth) < 0)
    schedule = isl_schedule_free(schedule);
  build = isl_ast_build_alloc(gen->prog->ctx);
  iterators = ppcg_scop_generate_names(gen->prog->scop, depth, "c");
  build = isl_ast_build_set_iterators(build, iterators);
  build = isl_ast_build_set_at_each_domain(build, &at_domain_module, &data);
  build = isl_ast_build_set_before_each_mark(build, &before_mark_module, &data);
  build = isl_ast_build_set_after_each_mark(build,
                                            &after_mark_ext_module, &data);
  if (gen->prog->scop->options->debug->dump_final_schedule)
    isl_schedule_dump(schedule);
  tree = isl_ast_build_node_from_schedule(build, schedule);
  isl_ast_build_free(build);

  return tree;
}

/* Generate AST for module calls and fifo decls in the top module.
 */
isl_stat sa_top_module_generate_code(struct autosa_gen *gen)
{
  struct autosa_hw_top_module *top = gen->hw_top_module;
  /* fifo declaration */
  top->fifo_decl_trees = (isl_ast_node **)malloc(
      top->n_fifo_decls * sizeof(isl_ast_node *));
  for (int i = 0; i < top->n_fifo_decls; i++)
  {
    top->fifo_decl_trees[i] = sa_fifo_decl_generate_code(gen,
                                                         top->fifo_decl_scheds[i]);
  }

  /* module call */
  top->module_call_trees = (isl_ast_node **)malloc(
      top->n_module_calls * sizeof(isl_ast_node *));
  for (int i = 0; i < top->n_module_calls; i++)
  {
    top->module_call_trees[i] = sa_module_call_generate_code(gen,
                                                             top->module_call_scheds[i]);
  }

  if (gen->options->target == AUTOSA_TARGET_INTEL_OPENCL)
  {
    top->ext_module_trees = (isl_ast_node **)malloc(
        top->n_ext_module * sizeof(isl_ast_node *));
    for (int i = 0; i < top->n_ext_module; i++)
    {
      top->ext_module_trees[i] = sa_set_ext_module_args_generate_code(gen,
                                                                      top->ext_module_scheds[i]);
    }

    //    for (int i = 0; i < top->n_ext_module; i++) {
    //      isl_ast_node_free(top->ext_module_trees[i]);
    //      isl_ast_node_free(top->ext_module_wrapped_trees[i]);
    //    }
    //    free(top->ext_module_trees);
    //    free(top->ext_module_wrapped_trees);
    //    top->ext_module_trees = NULL;
    //    top->ext_module_wrapped_trees = NULL;
    //    top->n_ext_module = 0;
  }

  return isl_stat_ok;
}

/* Representation of a statement inside a generated AST.
 *
 * "stmt" refers to the original statement.
 * "ref2expr" maps the reference identifier of each access in
 * the statement to an AST expression that should be printed
 * at the place of the access.
 */
struct ppcg_stmt {
	struct pet_stmt *stmt;

	isl_id_to_ast_expr *ref2expr;
};

static __isl_give isl_printer *print_user(__isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options,
  __isl_keep isl_ast_node *node, void *user)
{
	struct ppcg_stmt *stmt;
	isl_id *id;
  const char *stmt_name;

	id = isl_ast_node_get_annotation(node);
	stmt = (struct ppcg_stmt *)isl_id_get_user(id);
  stmt_name = isl_id_get_name(id);
	isl_id_free(id);

  if (stmt)
	  p = pet_stmt_print_body(stmt->stmt, p, stmt->ref2expr);
  else
    p = isl_printer_print_str(p, stmt_name);

	isl_ast_print_options_free(print_options);
  return p;
}

///* Set *depth (initialized to 0 by the caller) to the maximum
// * of the schedule depths of the leaf nodes for which this function is called.
// */
//static isl_bool update_depth(__isl_keep isl_schedule_node *node, void *user)
//{
//	int *depth = (int *)user;
//	int node_depth;
//
//	if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
//		return isl_bool_true;
//	node_depth = isl_schedule_node_get_schedule_depth(node);
//	if (node_depth > *depth)
//		*depth = node_depth;
//
//	return isl_bool_false;
//}

/* Find the element in scop->stmts that has the given "id".
 */
static struct pet_stmt *find_stmt(struct ppcg_scop *scop, __isl_keep isl_id *id)
{
	int i;

	for (i = 0; i < scop->pet->n_stmt; ++i) {
		struct pet_stmt *stmt = scop->pet->stmts[i];
		isl_id *id_i;

		id_i = isl_set_get_tuple_id(stmt->domain);
		isl_id_free(id_i);

		if (id_i == id)
			return stmt;
	}

	isl_die(isl_id_get_ctx(id), isl_error_internal,
		"statement not found", return NULL);
}

/* Index transformation callback for pet_stmt_build_ast_exprs.
 *
 * "index" expresses the array indices in terms of statement iterators
 * "iterator_map" expresses the statement iterators in terms of
 * AST loop iterators.
 *
 * The result expresses the array indices in terms of
 * AST loop iterators.
 */
static __isl_give isl_multi_pw_aff *pullback_index(
	__isl_take isl_multi_pw_aff *index, __isl_keep isl_id *id, void *user)
{
	isl_pw_multi_aff *iterator_map = (isl_pw_multi_aff *)user;

	iterator_map = isl_pw_multi_aff_copy(iterator_map);
	return isl_multi_pw_aff_pullback_pw_multi_aff(index, iterator_map);
}

static void ppcg_stmt_free(void *user)
{
	struct ppcg_stmt *stmt = (struct ppcg_stmt *)user;

	if (!stmt)
		return;

	isl_id_to_ast_expr_free(stmt->ref2expr);

	free(stmt);
}

/* Transform the accesses in the statement associated to the domain
 * called by "node" to refer to the AST loop iterators, construct
 * corresponding AST expressions using "build",
 * collect them in a ppcg_stmt and annotate the node with the ppcg_stmt.
 */
static __isl_give isl_ast_node *at_each_domain(__isl_take isl_ast_node *node,
	__isl_keep isl_ast_build *build, void *user)
{
	struct ppcg_scop *scop = (struct ppcg_scop *)user;
	isl_ast_expr *expr, *arg;
	isl_ctx *ctx;
	isl_id *id;
	isl_map *map;
	isl_pw_multi_aff *iterator_map;
	struct ppcg_stmt *stmt;  

	ctx = isl_ast_node_get_ctx(node);
	stmt = isl_calloc_type(ctx, struct ppcg_stmt);
	if (!stmt)
		goto error;

	expr = isl_ast_node_user_get_expr(node);
	arg = isl_ast_expr_get_op_arg(expr, 0);
	isl_ast_expr_free(expr);
	id = isl_ast_expr_get_id(arg);
	isl_ast_expr_free(arg);
	stmt->stmt = find_stmt(scop, id);
	isl_id_free(id);
	if (!stmt->stmt)
    ppcg_stmt_free(stmt);
    return node;
		//goto error;

	map = isl_map_from_union_map(isl_ast_build_get_schedule(build));
	map = isl_map_reverse(map);
	iterator_map = isl_pw_multi_aff_from_map(map);
	stmt->ref2expr = pet_stmt_build_ast_exprs(stmt->stmt, build,
				    &pullback_index, iterator_map, NULL, NULL);
	isl_pw_multi_aff_free(iterator_map);

	id = isl_id_alloc(isl_ast_node_get_ctx(node), NULL, stmt);
	id = isl_id_set_free_user(id, &ppcg_stmt_free);
	return isl_ast_node_set_annotation(node, id);
error:
	ppcg_stmt_free(stmt);
	return isl_ast_node_free(node);
}

/* For internal debugging.
 * Print out the code from the given schedule.
 */
void print_code(struct autosa_gen *gen, __isl_take isl_schedule *schedule, const char *output_f)
{
  isl_ast_node *tree;
  isl_printer *p;
  isl_ast_print_options *print_options;
  isl_ctx *ctx = gen->ctx;
  FILE *f;
  int depth;
  isl_ast_build *build;
  isl_id_list *iterators;

//#ifdef _DEBUG
//  DBGSCHD(stdout, schedule, ctx);
//#endif

  //tree = sa_generate_code(gen, isl_schedule_copy(schedule));
  depth = 0;
  if (isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth, &depth) < 0)
		return;
  build = isl_ast_build_alloc(ctx);
  iterators = ppcg_scop_generate_names(gen->prog->scop, depth, "c");
  build = isl_ast_build_set_iterators(build, iterators);
  build = isl_ast_build_set_at_each_domain(build, &at_each_domain, gen->prog->scop);
  tree = isl_ast_build_node_from_schedule(build, schedule);
  isl_ast_build_free(build);

  f = fopen(output_f, "w");
  p = isl_printer_to_file(ctx, f);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_user, NULL);
  p = isl_ast_node_print(tree, p, print_options);

  isl_ast_node_free(tree);
  fclose(f);
  isl_printer_free(p);
}