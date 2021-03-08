/* Define functions for communication management. */

#include <isl/ilp.h>

#include "autosa_schedule_tree.h"
#include "autosa_utils.h"
#include "autosa_print.h"
#include "autosa_codegen.h"
#include "autosa_comm.h"

/* Internal data structure for autosa_group_references.
 */
struct autosa_group_data
{
  struct autosa_gen *gen;
  struct ppcg_scop *scop;
  /* The schedule depth where the kernel launch will be 
   * introduced.
   */
  int kernel_depth;
  /* The schedule depth at which the copying in/from local_memory
   * is computed. The copy operation may then later
   * be hoisted to a higher level.
   */
  int local_depth;
  /* The schedule depth of "pe" mark. */
  int pe_depth;
  isl_schedule *schedule;

  /* All the schedules are formulated in terms of the original statement
   * instances, i.e., those that appear in the domains of the access 
   * relations. 
   */
  /* Contains the kernel_depth dimensions of the host schedule. */
  isl_union_map *host_sched;
  /* Contains the first local_depth dimensions of the kernel schedule. */
  isl_union_map *local_sched;
  /* Contains the first local_depth dimensions of the kernel schedule. */
  isl_union_map *copy_sched;
  /* Contains the first pe_depth dimensions of the kernel schedule. */
  isl_union_map *pe_sched;
  /* A union map representation of the entire kernel schedule. */
  isl_union_map *full_sched;
};

/* Return the prefix schedule at "node" as a relation
 * between domain elements and schedule dimensions after detecting
 * equalities in this relation.
 */
static __isl_give isl_union_map *prefix_with_equalities(
    __isl_keep isl_schedule_node *node)
{
  isl_union_map *schedule;

  schedule = isl_schedule_node_get_prefix_schedule_relation(node);
  /* Simplify. */
  schedule = isl_union_map_detect_equalities(schedule);

  return schedule;
}

/* Expand the domain of the schedule "s" by plugging in
 * the contraction "contraction" and return the result.
 */
static isl_union_map *expand(__isl_take isl_union_map *s,
                             __isl_keep isl_union_pw_multi_aff *contraction)
{
  contraction = isl_union_pw_multi_aff_copy(contraction);
  s = isl_union_map_preimage_domain_union_pw_multi_aff(s, contraction);
  return s;
}

/* Fill up the groups of array with singleton groups, i.e., one group
 * per reference, initializing all the necessary fields.
 * In particular the access field is initialized to the scheduled
 * access relation of the array reference.
 *
 * Return the number of elements initialized, i.e., the number of
 * active references in the current kernel.
 */
static int populate_array_references_pe(struct autosa_local_array_info *local,
                                        struct autosa_array_ref_group **groups, struct autosa_group_data *data)
{
  int i;
  int j;
  int n;
  isl_ctx *ctx = isl_union_map_get_ctx(data->pe_sched);

  n = 0;
  for (i = 0; i < local->array->n_ref; ++i)
  {
    isl_union_map *umap;
    isl_map *map;
    struct autosa_array_ref_group *group;
    struct autosa_stmt_access *access = local->array->refs[i];

    map = isl_map_copy(access->access);
    umap = isl_union_map_from_map(map);
    umap = isl_union_map_apply_domain(umap,
                                      isl_union_map_copy(data->pe_sched));

    if (isl_union_map_is_empty(umap))
    {
      isl_union_map_free(umap);
      continue;
    }

    map = isl_map_from_union_map(umap);
    map = isl_map_detect_equalities(map);

    group = isl_calloc_type(ctx, struct autosa_array_ref_group);
    if (!group)
    {
      isl_map_free(map);
      return -1;
    }
    group->local_array = local;
    group->array = local->array;
    group->access = map;
    group->write = access->write;
    group->exact_write = access->exact_write;
    group->slice = access->n_index < local->array->n_index;
    group->refs = &local->array->refs[i];
    group->n_ref = 1;
    group->io_type = AUTOSA_UNKNOWN_IO;
    group->dir = NULL;
    group->old_dir = NULL;
    group->group_type = AUTOSA_PE_GROUP;
    group->local_tile = NULL;
    group->io_trans = NULL;
    group->io_pe_expr = NULL;
    group->n_io_buffer = 0;
    group->io_buffers = NULL;
    group->copy_schedule = NULL;
    group->pe_tile = NULL;

    groups[n++] = group;
  }

  return n;
}

/* Combine the given two groups into a single group, containing
 * the references of both groups.
 */
static struct autosa_array_ref_group *join_groups(
    struct autosa_array_ref_group *group1,
    struct autosa_array_ref_group *group2)
{
  int i, j;
  isl_ctx *ctx;
  struct autosa_array_ref_group *group;

  if (!group1 || !group2)
    return NULL;

  ctx = isl_map_get_ctx(group1->access);
  group = isl_calloc_type(ctx, struct autosa_array_ref_group);
  if (!group)
    return NULL;
  group->local_array = group1->local_array;
  group->array = group1->array;
  group->access = isl_map_union(isl_map_copy(group1->access),
                                isl_map_copy(group2->access));
  group->write = group1->write || group2->write;
  group->exact_write = group1->exact_write && group2->exact_write;
  group->slice = group1->slice || group2->slice;
  //group->n_ref = group1->n_ref + group2->n_ref;
  //group->refs = isl_alloc_array(ctx, struct autosa_stmt_access *,
  //                              group->n_ref);
  //if (!group->refs)
  //  return autosa_array_ref_group_free(group);  
  group->n_ref = group1->n_ref;
  group->refs = isl_alloc_array(ctx, struct autosa_stmt_access *,
                                group->n_ref);
  if (!group->refs)                                     
    return autosa_array_ref_group_free(group);
  for (i = 0; i < group1->n_ref; ++i)
    group->refs[i] = group1->refs[i];
  /* Compare if the refs equals */      
  for (i = 0; i < group2->n_ref; ++i) {
    struct autosa_stmt_access *ref = group2->refs[i];
    bool found = false;
    for (j = 0; j < group1->n_ref; j++) {
      if (isl_map_is_equal(ref->tagged_access, group1->refs[j]->tagged_access)) {
        found = true;
        break;
      }
    }
    if (!found) {
      group->n_ref++;
      group->refs = (struct autosa_stmt_access **)realloc(group->refs,
                        group->n_ref * sizeof(struct autosa_stmt_access *));
      //group->refs[group1->n_ref + i] = group2->refs[i];
      group->refs[group->n_ref - 1] = group2->refs[i];
    }
  }

  group->io_type = group1->io_type;
  group->dir = isl_vec_copy(group1->dir);
  group->group_type = group1->group_type;
  group->pe_io_dir = group1->pe_io_dir;
  group->array_io_dir = group1->array_io_dir;
  group->io_trans = group1->io_trans;
  group->io_pe_expr = group1->io_pe_expr;
  group->io_L1_pe_expr = group1->io_L1_pe_expr;
  group->n_io_buffer = group1->n_io_buffer;
  group->io_buffers = group1->io_buffers;
  group->n_mem_ports = group1->n_mem_ports;

  return group;
}

/* Combine the given two groups into a single group and free
 * the original two groups.
 */
static struct autosa_array_ref_group *join_groups_and_free(
    struct autosa_array_ref_group *group1,
    struct autosa_array_ref_group *group2)
{
  struct autosa_array_ref_group *group;

  group = join_groups(group1, group2);
  autosa_array_ref_group_free(group1);
  autosa_array_ref_group_free(group2);
  return group;
}

static void set_array_groups_default(struct autosa_local_array_info *array,
                                     int n, struct autosa_array_ref_group **groups)
{
  int i;

  array->n_group = n;
  array->groups = groups;

  for (i = 0; i < n; ++i)
    groups[i]->nr = i;
}

/* Default grouping. Simply group all array references together
 * if any of them is associated with RAW/RAR carried by space loops.
 */
static int group_array_references_default(struct autosa_kernel *kernel,
                                          struct autosa_local_array_info *local, struct autosa_group_data *data)
{
  int i, j;
  int n;
  isl_ctx *ctx = isl_union_map_get_ctx(data->pe_sched);
  struct autosa_array_ref_group **groups;
  int merge_all = 0;
  isl_schedule_node *node;

  groups = isl_calloc_array(ctx, struct autosa_array_ref_group *,
                            local->array->n_ref);
  if (!groups)
    return -1;

  n = populate_array_references_pe(local, groups, data);

  /* Examine if any of the array references is associated with RAW or
   * RAR carried at space loops. If then, merge all the groups. 
   */
  for (int i = 0; i < n; ++i)
  {
    struct autosa_array_ref_group *group_i = groups[i];
    for (int j = 0; j < group_i->n_ref; ++j)
    {
      struct autosa_stmt_access *ref_i = group_i->refs[j];
      for (int k = 0; k < ref_i->n_io_info; ++k)
      {
        if (ref_i->io_info[k]->dep->type == AUTOSA_DEP_RAW)
        {
          merge_all = 1;
          break;
        }
      }
    }
  }

  if (merge_all)
  {
    /* Join all referneces together. */
    for (int i = 1; i < n; ++i)
    {
      groups[0] = join_groups_and_free(groups[0], groups[i]);
    }
    n = 1;
  }

  set_array_groups_default(local, n, groups);

  return 0;
}

/* Return the union of all read (read = 1) and/or write (write = 1)
 * access relations in the group.
 */
__isl_give isl_union_map *autosa_array_ref_group_access_relation(
    struct autosa_array_ref_group *group, int read, int write)
{
  int i;
  isl_union_map *access;

  access = isl_union_map_empty(isl_map_get_space(group->access));
  for (i = 0; i < group->n_ref; ++i)
  {
    isl_map *map_i;

    if (!((read && group->refs[i]->read) ||
          (write && group->refs[i]->write)))
      continue;
    map_i = isl_map_copy(group->refs[i]->access);
    access = isl_union_map_union(access,
                                 isl_union_map_from_map(map_i));
  }

  return access;
}

/* Map the domain of "access" to the outer data->pe_depth
 * schedule dimensions.   
 */
static __isl_give isl_map *local_access_pe(struct autosa_array_ref_group *group,
                                           __isl_keep isl_union_map *access, struct autosa_group_data *data)
{
  isl_union_map *local;

  local = isl_union_map_copy(access);
  /* Group at the PE level. */
  local = isl_union_map_apply_domain(local,
                                     isl_union_map_copy(data->pe_sched));
  return isl_map_from_union_map(local);
}

/* Given an array access "access", check if for any index i there is
 * a shift a(p) and a stride g such that
 *
 *	a(p) + i = 0 mod g
 *
 * If so, record the information in tile->bound[i]->stride and
 * tile->bound[i]->shift.
 * Otherwise, set tile->bound[i]->stride to 1 (and tile->bound[i]->shift to 0).
 * Return isl_bool_true if any non-trivial stride was found.
 *
 * Note that the stride info returned by isl_map_get_range_stride_info
 * is of the form
 *
 *	i = o(p) + g n
 *
 * a(p) can therefore be taken to be equal to -o(p).
 */
static isl_bool detect_strides(struct autosa_array_tile *tile,
                               __isl_keep isl_map *access)
{
  int i;
  isl_bool has_strides = isl_bool_false;

  for (i = 0; i < tile->n; ++i)
  {
    struct autosa_array_bound *bound = &tile->bound[i];
    isl_stride_info *si;

    si = isl_map_get_range_stride_info(access, i);
    bound->stride = isl_stride_info_get_stride(si);
    bound->shift = isl_aff_neg(isl_stride_info_get_offset(si));
    isl_stride_info_free(si);

    if (!has_strides)
      has_strides = isl_val_gt_si(bound->stride, 1);
    if (has_strides < 0)
      return isl_bool_error;
  }

  return has_strides;
}

/* Given an array access "access", remove the strides based
 * on the information in tile->bound[i]->stride and tile->bound[i]->shift.
 *
 * In particular let the access be A[a] and
 * let the shifts s_i(p) and the strides g_i be such that
 *
 *  S(p) + a = 0 mod G
 *
 * Replace the access by
 *
 *  A[(a + S(p))/G]
 *
 * First collect the shifts s_i into an isl_multi_aff and
 * the strides into the scaling function A[i] -> A[G i].
 * Then add the shifts to the original access and
 * take the preimage over the scaling.
 */
static __isl_give isl_map *remove_strides(__isl_take isl_map *access,
                                          struct autosa_array_tile *tile)
{
  int i;
  isl_space *space;
  isl_multi_aff *shift, *scale;
  isl_multi_val *stride;

  space = isl_map_get_space(access);
  shift = isl_multi_aff_zero(isl_space_copy(space));
  space = isl_space_range(space);
  stride = isl_multi_val_zero(isl_space_copy(space));
  scale = isl_multi_aff_identity(isl_space_map_from_set(space));
  for (i = 0; i < tile->n; ++i)
  {
    struct autosa_array_bound *bound = &tile->bound[i];
    isl_aff *shift_i;
    isl_val *stride_i;

    shift_i = isl_aff_copy(bound->shift);
    stride_i = isl_val_copy(bound->stride);
    shift = isl_multi_aff_set_aff(shift, i, shift_i);
    stride = isl_multi_val_set_val(stride, i, stride_i);
  }
  scale = isl_multi_aff_scale_multi_val(scale, stride);

  access = isl_map_sum(access, isl_map_from_multi_aff(shift));
  access = isl_map_preimage_range_multi_aff(access, scale);

  return access;
}

/* Check if we can find a memory tile for the given array
 * based on the given accesses, and if so, put the results in "tile".
 *
 * We project the accesses on each index in turn and look for a parametric
 * offset such that the size is constant, after removing
 * any stride that may appear in the accesses.
 *
 * tile->depth is initialized to the input dimension of the computed bounds.
 */
isl_bool can_tile(__isl_keep isl_map *access,
                  struct autosa_array_tile *tile)
{
  int i;
  isl_bool has_strides, valid;
  isl_fixed_box *box;
  isl_multi_aff *offset;
  isl_multi_val *size;

  if (!tile)
    return isl_bool_error;

  isl_map_free(isl_map_detect_equalities(isl_map_copy(access)));

  has_strides = detect_strides(tile, access);
  if (has_strides < 0)
    return isl_bool_error;

  tile->depth = isl_map_dim(access, isl_dim_in);

  access = isl_map_copy(access);
  if (has_strides)
    access = remove_strides(access, tile);

  box = isl_map_get_range_simple_fixed_box_hull(access);
  isl_map_free(access);

  valid = isl_fixed_box_is_valid(box);
  if (valid >= 0 && valid)
  {
    offset = isl_fixed_box_get_offset(box);
    size = isl_fixed_box_get_size(box);
    for (i = 0; i < tile->n; ++i)
    {
      tile->bound[i].size = isl_multi_val_get_val(size, i);
      tile->bound[i].lb = isl_multi_aff_get_aff(offset, i);
    }
    isl_multi_aff_free(offset);
    isl_multi_val_free(size);
  }
  isl_fixed_box_free(box);

  return valid;
}

struct check_contraction_data {
  bool legal;
  struct autosa_array_ref_group *group;
  struct autosa_kernel *kernel;
  isl_union_map *prefix;
  isl_union_pw_multi_aff *prefix_upma;
  int depth;
};

struct check_stmt_contain_acc_data {
  struct autosa_kernel *kernel;
  struct autosa_array_ref_group *group;
};

/* Test if the current statement with the domain "set" contains the array access
 * in the current array group. 
 */
static isl_bool check_stmt_contain_acc(__isl_keep isl_set *set, void *user)
{
  isl_space *space;
  isl_id *id;
  struct autosa_stmt *stmt;
  struct check_stmt_contain_acc_data *data = (struct check_stmt_contain_acc_data *)user;
  struct autosa_stmt_access *accesses, *access;

  space = isl_set_get_space(set);
  id = isl_space_get_tuple_id(space, isl_dim_set);
  isl_space_free(space);
  stmt = find_stmt(data->kernel->prog, id);
  isl_id_free(id);
  accesses = stmt->accesses;

  for (access = accesses; access; access = access->next)
  {
    //if (access == data->group->refs[0])
    //{
    //  return isl_bool_false;
    //}
    for (int i = 0; i < data->group->n_ref; i++) {
      if (access == data->group->refs[i])
        return isl_bool_false;
    }
  }

  return isl_bool_true;
}

/* Check if the pe_group is mapped to a single register.
 * Specifically, check for each array access in the current pe_group, 
 * if all the loops above the array access and below the PE mark are
 * parallel loops.
 */
static __isl_give isl_schedule_node *check_contraction(
  __isl_take isl_schedule_node *node, void *user)
{
  struct check_contraction_data *data = (struct check_contraction_data *)user;
  isl_union_set *domain;
  isl_bool not_contain_acc;
  struct check_stmt_contain_acc_data check_data;
  isl_schedule_node *tmp_node;
  isl_ctx *ctx = isl_schedule_node_get_ctx(node);

  //DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return node;

  if (!data->legal)
    return node;

  /* Test if the statement contains the access from the group. */
  domain = isl_schedule_node_get_domain(node);
  check_data.kernel = data->kernel;
  check_data.group = data->group;
  not_contain_acc = isl_union_set_every_set(domain, &check_stmt_contain_acc, &check_data);
  isl_union_set_free(domain);  

  /* Then check if all the loops above the statement until the PE mark are parallel loops. */
  tmp_node = isl_schedule_node_copy(node);
  if (!not_contain_acc) {    
    isl_schedule_node *tmp_node2;

    /* If the node is under SIMD, we will move up to the "SIMD" mark, and 
     * compute the tiling at this level.
     */
    int is_simd;
    is_simd = is_node_under_simd(tmp_node);
    if (is_simd) {
      tmp_node = autosa_tree_move_up_to_mark(tmp_node, "simd");      
    }

    tmp_node2 = isl_schedule_node_copy(tmp_node);

    /* Check if all band nodes above are parallel loops. */    
    while (!(autosa_tree_node_is_mark(tmp_node, "pe"))) {    
      if (isl_schedule_node_get_type(tmp_node) == isl_schedule_node_band) {
        int dim = isl_schedule_node_band_n_member(tmp_node);
        for (int i = 0; i < dim; i++) {
          if (!isl_schedule_node_band_member_get_coincident(tmp_node, i)) {
            data->legal = false;
            break;
          }
        }
      }
      tmp_node = isl_schedule_node_parent(tmp_node);
    }

    if (data->prefix == NULL) {
      data->prefix = isl_schedule_node_get_prefix_schedule_union_map(tmp_node2);
      data->prefix_upma = isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(tmp_node2);
      data->depth = isl_schedule_node_get_schedule_depth(tmp_node2);
    } else {
      /* Find the depth that shares the same prefix schedule with the current one. */
      /* Lift the node until it reaches a scheduling depth no greater than data->depth. */
      while (isl_schedule_node_get_schedule_depth(tmp_node2) > data->depth)
        tmp_node2 = isl_schedule_node_parent(tmp_node2);
      if (isl_schedule_node_get_schedule_depth(tmp_node2) < data->depth) {
        /* Lower the node until the scheduling depth equals to the data->depth */                  
        tmp_node2 = isl_schedule_node_band_split(tmp_node2, 
                      data->depth - isl_schedule_node_get_schedule_depth(tmp_node2));
        tmp_node2 = isl_schedule_node_child(tmp_node2, 0);
      }

      /* Lift the node until it achieves the same prefix schedule with the data->prefix. */
      isl_union_map *tmp_prefix = isl_schedule_node_get_prefix_schedule_union_map(tmp_node2);
      int tmp_depth = isl_schedule_node_get_schedule_depth(tmp_node2);      
      isl_set *tmp_prefix_range = isl_set_from_union_set(isl_union_map_range(tmp_prefix));
      isl_set *prefix_range = isl_set_from_union_set(isl_union_map_range(isl_union_map_copy(data->prefix)));
      
      //DBGUSET(stdout, prefix_range, ctx);

      int common_depth = 0;
      for (common_depth = 0; common_depth < tmp_depth; common_depth++) {
        isl_set *tmp_range = isl_set_project_out(isl_set_copy(tmp_prefix_range), isl_dim_set, common_depth, tmp_depth - common_depth);
        isl_set *range = isl_set_project_out(isl_set_copy(prefix_range), isl_dim_set, common_depth, tmp_depth - common_depth);
        isl_set *diff = isl_set_subtract(tmp_range, range);
        if (!isl_set_is_empty(diff)) {
          common_depth--;
          isl_set_free(diff);
          break;
        }
        isl_set_free(diff);
      }
      isl_set_free(tmp_prefix_range);
      isl_set_free(prefix_range);

      /* Lift the node until if reaches common_depth */
      while (isl_schedule_node_get_schedule_depth(tmp_node2) > common_depth) {
        tmp_node2 = isl_schedule_node_parent(tmp_node2);
      }
      if (isl_schedule_node_get_schedule_depth(tmp_node2) < common_depth) {
        tmp_node2 = isl_schedule_node_band_split(tmp_node2, 
                      common_depth - isl_schedule_node_get_schedule_depth(tmp_node2));
        tmp_node2 = isl_schedule_node_child(tmp_node2, 0);
      }
 
      /* Update the scheduling information */      
      isl_union_map_free(data->prefix);
      isl_union_pw_multi_aff_free(data->prefix_upma);
      data->prefix = isl_schedule_node_get_prefix_schedule_union_map(tmp_node2);
      data->prefix_upma = isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(tmp_node2);
      data->depth = isl_schedule_node_get_schedule_depth(tmp_node2);
    }    
    isl_schedule_node_free(tmp_node2);
  }
  isl_schedule_node_free(tmp_node);

  return node;
}

/* Compute the tiling of the group at the PE level.
 * If array_contraction is enabled, check if all loops under the PE mark
 * and before the SIMD marks are parallel loops. 
 * If so, contract the local tile to a single register.
 */
static isl_stat compute_group_bounds_core_pe(struct autosa_kernel *kernel,
                                             struct autosa_array_ref_group *group, struct autosa_group_data *data)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. */
  access = autosa_array_ref_group_access_relation(group, 1, 1);
  /* Create local tile */
  if (use_local)
  {
    struct check_contraction_data contract_data;
    isl_schedule_node *node;        
    contract_data.legal = false;
    contract_data.prefix = NULL;

    /* Create a tile. */
    group->local_tile = autosa_array_tile_create(ctx,
                                                 group->array->n_index);

    /* Check if array contraction is possible. */
    if (kernel->options->autosa->local_reduce && kernel->options->autosa->array_contraction) {      
      contract_data.group = group;
      contract_data.kernel = kernel;
      contract_data.legal = true;
      contract_data.prefix = NULL;
      contract_data.prefix_upma = NULL;
      contract_data.depth = -1;      
      node = isl_schedule_get_root(kernel->schedule);
      node = autosa_tree_move_down_to_pe(node, kernel->core);
      //DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
      node = isl_schedule_node_map_descendant_bottom_up(node, &check_contraction, &contract_data);
      isl_schedule_node_free(node);      
      //std::cout << contract_data.legal << std::endl;
      //std::cout << contract_data.depth << std::endl;
    }
    
    if (contract_data.legal) {
      /* We are able to create a register tiling. */
      acc = isl_map_from_union_map(isl_union_map_apply_domain(isl_union_map_copy(access), 
                                                              isl_union_map_copy(contract_data.prefix)));
      group->copy_schedule_dim = contract_data.depth;
      group->copy_schedule = contract_data.prefix_upma;
      group->copy_schedule = isl_union_pw_multi_aff_pullback_union_pw_multi_aff(group->copy_schedule,
                                                                                isl_union_pw_multi_aff_copy(kernel->contraction));
    } else {
      /* Map the domain to the outer scheduling dimensions */
      acc = local_access_pe(group, access, data);
    }
    if (contract_data.prefix) 
      isl_union_map_free(contract_data.prefix);

    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, group->local_tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      group->local_tile =
          autosa_array_tile_free(group->local_tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Internal struct for compute_group_bounds_core_pe_acc. */
struct compute_local_tile_acc_data
{
  struct autosa_kernel *kernel;
  struct autosa_array_ref_group *group;
  int depth;
  isl_union_map *prefix;
  isl_union_pw_multi_aff *prefix_upma;
  int status;
};

/* Examine the schedule depth and prefix schedule used to calculated the 
 * register tiling. Specifically, if the access is under the SIMD loop,
 * we will move up to the "SIMD" mark and compute tiling at this level.
 * Otherwise, we will compute the tiling at the statement level.
 * In addition, if the access is found in more than one loop, we will 
 * not create register tiling. Instead, we create a local buffer at the PE level.
 */
static __isl_give isl_schedule_node *compute_local_tile_acc(
    __isl_take isl_schedule_node *node, void *user)
{
  struct compute_local_tile_acc_data *data = (struct compute_local_tile_acc_data *)user;
  struct autosa_array_ref_group *group = data->group;
  struct autosa_stmt_access *acc = group->refs[0];
  isl_union_set *domain;
  isl_union_map *prefix;
  isl_union_pw_multi_aff *prefix_upma;
  isl_bool not_contain_acc;
  int depth;
  struct check_stmt_contain_acc_data check_data;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
    return node;

  /* Test if the statement contains the access. */
  domain = isl_schedule_node_get_domain(node);
  check_data.kernel = data->kernel;
  check_data.group = data->group;
  not_contain_acc = isl_union_set_every_set(domain, &check_stmt_contain_acc, &check_data);
  isl_union_set_free(domain);

  if (!not_contain_acc)
  {
    int is_simd;
    is_simd = is_node_under_simd(node);
    if (is_simd)
    {
      /* If the node is under SIMD, we will move up to the "SIMD" mark, and 
       * compute the tiling at this level. 
       */
      isl_schedule_node *new_node;

      new_node = isl_schedule_node_copy(node);
      new_node = autosa_tree_move_up_to_mark(new_node, "simd");
      prefix = isl_schedule_node_get_prefix_schedule_union_map(new_node);
      prefix_upma = isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(new_node);
      depth = isl_schedule_node_get_schedule_depth(new_node);
      isl_schedule_node_free(new_node);
    }
    else
    {
      prefix = isl_schedule_node_get_prefix_schedule_union_map(node);
      prefix_upma = isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(node);
      depth = isl_schedule_node_get_schedule_depth(node);
    }
    if (data->depth == -1)
    {
      data->depth = depth;
      data->prefix = prefix;
      data->prefix_upma = prefix_upma;
      data->status = 1;
    }
    else
    {
      /* The array reference is found in more than one loop. 
       * We will compute the tiling at the PE level. 
       */
      isl_union_map_free(prefix);
      isl_union_pw_multi_aff_free(prefix_upma);
      data->status = 0;
    }
  }

  return node;
}

/* Compute the tiling of the group at the statement level.
 */
static isl_stat compute_group_bounds_core_pe_acc(struct autosa_kernel *kernel,
                                                 struct autosa_array_ref_group *group, struct autosa_group_data *data)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;
  isl_schedule_node *node;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group */
  access = autosa_array_ref_group_access_relation(group, 1, 1);
  /* Create local tile */
  if (use_local)
  {
    struct compute_local_tile_acc_data tile_data;

    tile_data.kernel = kernel;
    tile_data.group = group;
    tile_data.status = 0;
    tile_data.depth = -1;
    tile_data.prefix = NULL;
    /* Create a tile. */
    group->local_tile = autosa_array_tile_create(ctx, group->array->n_index);
    /* Map the domain to the outer scheduling dimensions */
    node = isl_schedule_get_root(kernel->schedule);
    node = autosa_tree_move_down_to_pe(node, kernel->core);
    node = isl_schedule_node_map_descendant_bottom_up(node, &compute_local_tile_acc, &tile_data);
    isl_schedule_node_free(node);
    if (tile_data.status)
    {
      /* We are able to create a register tiling. */
      acc = isl_map_from_union_map(isl_union_map_apply_domain(isl_union_map_copy(access),
                                                              tile_data.prefix));
      /* Update the copy schedule. */
      group->copy_schedule_dim = tile_data.depth;
      group->copy_schedule = tile_data.prefix_upma;
      group->copy_schedule = isl_union_pw_multi_aff_pullback_union_pw_multi_aff(group->copy_schedule,
                                                                                isl_union_pw_multi_aff_copy(kernel->contraction));
    }
    else
    {
      /* We will create the tiling at the PE level. */
      acc = local_access_pe(group, access, data);
      /* Update the copy schedule */
      node = isl_schedule_get_root(kernel->schedule);
      node = autosa_tree_move_down_to_pe(node, kernel->core);
      group->copy_schedule_dim = isl_schedule_node_get_schedule_depth(node);
      group->copy_schedule =
          isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(node);
      group->copy_schedule = isl_union_pw_multi_aff_pullback_union_pw_multi_aff(
          group->copy_schedule, isl_union_pw_multi_aff_copy(kernel->contraction));
      isl_schedule_node_free(node);
    }
    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, group->local_tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      group->local_tile = autosa_array_tile_free(group->local_tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Compute the local memory tiles for the array
 * reference group "group" of array "array" and set the tile depth.
 * Return 0 on success and -1 on error.
 */
static int compute_group_bounds_pe(struct autosa_kernel *kernel,
                                   struct autosa_array_ref_group *group, struct autosa_group_data *data)
{
  if (!group)
    return -1;
  if (compute_group_bounds_core_pe(kernel, group, data) < 0)
    return -1;

  return 0;
}

/* Compute the register tiles for the array
 * reference group "group" of array "array" and set the tile depth.
 * Return 0 on success and -1 on error.
 */
static int compute_group_bounds_pe_acc(struct autosa_kernel *kernel,
                                       struct autosa_array_ref_group *group, struct autosa_group_data *data)
{
  if (!group)
    return -1;
  if (compute_group_bounds_core_pe_acc(kernel, group, data) < 0)
    return -1;

  return 0;
}

/* Set array->n_group and array->groups to n and groups.
 *
 * Additionally, set the "nr" field of each group.
 */
static void set_array_groups_pe(struct autosa_local_array_info *array,
                                int n, struct autosa_array_ref_group **groups)
{
  int i;

  array->n_pe_group = n;
  array->pe_groups = groups;

  for (i = 0; i < n; ++i)
    groups[i]->nr = i;
}

/* Populate the array reference groups with single array reference.
 * If any of the array reference is associated with RAW, the array reference
 * is from an internal array, we will merge all the array references into 
 * one single group.
 * Otherwise, the array reference is from an external array, we do nothing
 * here. 
 * For internal array, we compute the group tiling at the PE level.
 * For external array, we compute the group tiling at the statement level.
 * Return -1 on error.
 */
static int group_array_references_pe(struct autosa_kernel *kernel,
                                     struct autosa_local_array_info *local, struct autosa_group_data *data)
{
  int i, j;
  int n;
  isl_ctx *ctx = isl_union_map_get_ctx(data->pe_sched);
  struct autosa_array_ref_group **groups;
  int merge_all = 0;
  isl_schedule_node *node;

  groups = isl_calloc_array(ctx, struct autosa_array_ref_group *,
                            local->array->n_ref);
  if (!groups)
    return -1;

  n = populate_array_references_pe(local, groups, data);

  /* Examine if any of the array references is associated with RAW. 
   * If then, merge all the groups. 
   */
  for (int i = 0; i < n; ++i)
  {
    struct autosa_array_ref_group *group_i = groups[i];
    for (int j = 0; j < group_i->n_ref; ++j)
    {
      struct autosa_stmt_access *ref_i = group_i->refs[j];
      for (int k = 0; k < ref_i->n_io_info; ++k)
      {
        if (ref_i->io_info[k]->dep->type == AUTOSA_DEP_RAW)
        {
          merge_all = 1;
          break;
        }
      }
    }
  }  

  if (merge_all)
  {
    /* Join all referneces together. */
    for (int i = 1; i < n; ++i)
    {
      groups[0] = join_groups_and_free(groups[0], groups[i]);
    }
    n = 1;
  }

  if (merge_all)
  {
    /* Internal array. */
    for (i = 0; i < n; ++i)
    {
      if (compute_group_bounds_pe(kernel, groups[i], data) < 0)
      {
        for (j = 0; j < n; j++)
        {
          autosa_array_ref_group_free(groups[j]);
        }
        free(groups);
        return -1;
      }

      if (groups[i]->copy_schedule_dim == 0) {
        /* Update the copy schedule at the PE level */
        node = isl_schedule_get_root(kernel->schedule);
        node = autosa_tree_move_down_to_pe(node, kernel->core);
        groups[i]->copy_schedule_dim = isl_schedule_node_get_schedule_depth(node);
        groups[i]->copy_schedule =
            isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(node);
        groups[i]->copy_schedule =
            isl_union_pw_multi_aff_pullback_union_pw_multi_aff(groups[i]->copy_schedule,
                                                               isl_union_pw_multi_aff_copy(kernel->contraction));
        isl_schedule_node_free(node);
      }
    }
  }
  else
  {
    /* External array. 
     * We will build the tiling for each array access. */
    for (i = 0; i < n; ++i)
    {
      if (compute_group_bounds_pe_acc(kernel, groups[i], data) < 0)
      {
        for (j = 0; j < n; j++)
        {
          autosa_array_ref_group_free(groups[j]);
        }
        free(groups);
        return -1;
      }
    }
  }

  set_array_groups_pe(local, n, groups);

  return 0;
}

/* Fill up the groups array with singleton groups, i.e., one group
 * per reference, initializing the array, access, write, n_ref and refs fields.
 * In particular the access field is initialized to the scheduled
 * access relation of the array reference.
 *
 * Return the number of elements initialized, i.e., the number of
 * active references in the current kernel.
 */
static int populate_array_references_io(struct autosa_local_array_info *local,
                                        struct autosa_array_ref_group **groups, struct autosa_group_data *data)
{
  int i;
  int j;
  int n;
  isl_ctx *ctx = isl_union_map_get_ctx(data->pe_sched);

  n = 0;
  for (i = 0; i < local->array->n_ref; ++i)
  {
    for (j = 0; j < local->array->refs[i]->n_io_info; ++j)
    {
      if (!((local->array->refs[i]->io_info[j]->dep->type == AUTOSA_DEP_RAR) ||
         (local->array->refs[i]->io_info[j]->dep->type == AUTOSA_DEP_RAW)))
         continue;

      isl_union_map *umap;
      isl_map *map;
      struct autosa_array_ref_group *group;
      struct autosa_stmt_access *access = local->array->refs[i];

      map = isl_map_copy(access->access);
      umap = isl_union_map_from_map(map);
      umap = isl_union_map_apply_domain(umap,
                                        isl_union_map_copy(data->copy_sched));

      if (isl_union_map_is_empty(umap))
      {
        isl_union_map_free(umap);
        continue;
      }

      map = isl_map_from_union_map(umap);
      map = isl_map_detect_equalities(map);

      group = isl_calloc_type(ctx, struct autosa_array_ref_group);
      if (!group)
      {
        isl_map_free(map);
        return -1;
      }
      group->local_array = local;
      group->array = local->array;
      group->access = map; // not used
      group->write = access->write;
      group->exact_write = access->exact_write;
      group->slice = access->n_index < local->array->n_index;
      group->refs = &local->array->refs[i];
      group->n_ref = 1;
      group->io_type = access->io_info[j]->io_type;
      group->dir = isl_vec_copy(access->io_info[j]->dir);
      group->old_dir = isl_vec_copy(group->dir);
      group->group_type = AUTOSA_IO_GROUP;
      group->pe_io_dir = IO_NULL;
      group->array_io_dir = IO_NULL;
      group->io_trans = NULL;
      group->io_pe_expr = NULL;
      group->io_L1_pe_expr = NULL;
      group->n_io_buffer = 0;
      group->io_buffers = NULL;
      group->copy_schedule = NULL;
      group->pe_tile = NULL;
      group->n_mem_ports = 1;

      groups[n++] = group;
    }
  }

  return n;
}

/* Examine if two groups share the same I/O modules:
 * - with the same I/O type
 * - with the same I/O direction
 */
static int share_io(struct autosa_array_ref_group *group1,
                    struct autosa_array_ref_group *group2)
{
  if (group1->io_type != group2->io_type)
    return 0;

  for (int i = 0; i < isl_vec_size(group1->dir); i++)
  {
    if (isl_vec_cmp_element(group1->dir, group2->dir, i))
      return 0;
  }

  return 1;
}

/* If two groups have shared I/O (as determined by
 * the "share" function),
 * then merge the two groups into one.
 * TODO: If "compute_bounds" is set, then call compute_group_bounds
 * on the merged groups.
 *
 * Return the updated number of groups.
 * Return -1 on error.
 */
static int group_io(struct autosa_kernel *kernel,
                    int n, struct autosa_array_ref_group **groups,
                    int (*share)(struct autosa_array_ref_group *group1,
                                 struct autosa_array_ref_group *group2),
                    int compute_bounds,
                    struct autosa_group_data *data)
{
  int i, j;

  for (i = 0; i < n; ++i)
  {
    for (j = n - 1; j > i; --j)
    {
      if (!share(groups[i], groups[j]))
        continue;

      groups[i] = join_groups_and_free(groups[i], groups[j]);
      if (j != n - 1)
        groups[j] = groups[n - 1];
      groups[n - 1] = NULL;
      n--;

      if (!groups[i])
        return -1;
      //			if (compute_bounds &&
      //			    compute_group_bounds_io(kernel, groups[i], data) < 0)
      //				return -1;
    }
  }

  return n;
}

/* If two groups share the same I/O type and I/O direction,
 * merge the two groups into one.
 *
 * Return the updated number of groups.
 */
static int group_share_io(struct autosa_kernel *kernel,
                          int n, struct autosa_array_ref_group **groups,
                          struct autosa_group_data *data)
{
  return group_io(kernel, n, groups, &share_io, 0, data);
}

/* Perform interior I/O elimination.
 * Find the I/O group with interior I/O, and assign new data tranfer direction 
 * at the PE level.
 * At present, we will assign the first dim to 1 by default.
 */
static isl_stat autosa_interior_io_eliminate(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    struct autosa_gen *gen, struct autosa_group_data *data)
{
  if (isl_vec_is_zero(group->dir))
  {
    /* This group will generate interior I/O, which needs to be eliminated. 
     * By default, set the first dim to be 1. 
     * Hack: For LU, we set the the last dim to be 1. 
     * TODO: make it an option.
     */
    if (gen->options->autosa->int_io_dir == 0)
      group->dir = isl_vec_set_element_si(group->dir, 0, 1);
    else
      group->dir = isl_vec_set_element_si(group->dir, isl_vec_size(group->dir) - 1, 1);

    /* Update the array info */
    for (int i = 0; i < group->n_ref; i++)
    {
      struct autosa_stmt_access *ref = group->refs[i];
      for (int j = 0; j < ref->n_io_info; j++)
      {
        struct autosa_io_info *io_info = ref->io_info[j];
        if (io_info->io_type == group->io_type && isl_vec_is_zero(io_info->dir))
        {
          isl_vec_free(io_info->dir);
          io_info->dir = isl_vec_copy(group->dir);
        }
      }
    }
  }
  return isl_stat_ok;
}

/* The "node" points to the current space band.
 * We will cluster it using the direction "dir".
 * Specifically, following the space-time transformation using projection and 
 * scheduling vectors, we assign projection vector d = dir, scheduling vector
 * s = dir.
 * Next, we compose the new transformation matrix:
 * 
 * T = [ P
 *      ---
 *       s ]
 * where PdT = 0.
 * 
 * This new transformation matrix is applied to the space band.
 * We will return the transformaton matrix in "io_trans_mat" and "io_trans_ma".
 */
static __isl_give isl_schedule_node *io_cluster(
    __isl_take isl_schedule_node *node,
    __isl_keep isl_vec *dir, isl_mat **io_trans_mat, isl_multi_aff **io_trans_ma)
{
  isl_multi_union_pw_aff *mupa;
  isl_mat *trans_mat, *d_mat, *null_mat;
  int space_dim;
  isl_ctx *ctx;
  isl_space *space;
  isl_multi_aff *ma;

  mupa = isl_schedule_node_band_get_partial_schedule(node);
  space_dim = isl_schedule_node_band_n_member(node);
  ctx = isl_schedule_node_get_ctx(node);

  /* Build the transformation matrix. */
  trans_mat = isl_mat_alloc(ctx, space_dim, space_dim);
  d_mat = isl_mat_alloc(ctx, 1, space_dim);
  for (int i = 0; i < isl_vec_size(dir); i++)
  {
    d_mat = isl_mat_set_element_val(d_mat, 0, i,
                                    isl_vec_get_element_val(dir, i));
  }
  null_mat = isl_mat_right_kernel(d_mat);
  //#ifdef _DEBUG
  //  DBGVAR(std::cout, isl_mat_rows(null_mat));
  //  DBGVAR(std::cout, isl_mat_cols(null_mat));
  //  print_mat(stdout, null_mat);
  //#endif

  for (int i = 0; i < isl_mat_cols(null_mat); i++)
    for (int j = 0; j < isl_mat_rows(null_mat); j++)
    {
      trans_mat = isl_mat_set_element_val(trans_mat, i, j,
                                          isl_mat_get_element_val(null_mat, j, i));
    }
  for (int i = 0; i < isl_vec_size(dir); i++)
  {
    trans_mat = isl_mat_set_element_val(trans_mat, isl_mat_cols(null_mat), i,
                                        isl_vec_get_element_val(dir, i));
  }
  *io_trans_mat = trans_mat;

  /* Convert the transformation matrix to multi_aff. */
  space = isl_multi_union_pw_aff_get_space(mupa);
  space = isl_space_map_from_set(space);
  ma = isl_multi_aff_identity(space);

  for (int i = 0; i < isl_mat_rows(trans_mat); i++)
  {
    isl_aff *aff = isl_multi_aff_get_aff(ma, i);
    for (int j = 0; j < isl_mat_cols(trans_mat); j++)
    {
      isl_val *val = isl_mat_get_element_val(trans_mat, i, j);      
      aff = isl_aff_set_coefficient_si(aff, isl_dim_in, j, isl_val_get_num_si(val));      
      isl_val_free(val);
    }
    ma = isl_multi_aff_set_aff(ma, i, aff);
  }

//#ifdef _DEBUG
//  DBGMUPA(stdout, mupa, isl_schedule_node_get_ctx(node));
//  DBGMA(stdout, ma, isl_schedule_node_get_ctx(node));
//#endif
  /* Apply the new transformation on the original partial schedule. */
  mupa = isl_multi_union_pw_aff_apply_multi_aff(mupa, isl_multi_aff_copy(ma));
  *io_trans_ma = ma;

  node = isl_schedule_node_delete(node);
  /* Insert the new partial schedule. */
  node = isl_schedule_node_insert_partial_schedule(node, mupa);

  isl_mat_free(null_mat);

  return node;
}

static isl_stat extract_set_max_dim(__isl_take isl_basic_set *bset, void *user)
{
  isl_val *val;
  isl_val **max_val = (isl_val **)user;

  val = isl_basic_set_dim_max_val(bset, 0);
  if (isl_val_gt(val, *max_val))
  {
    isl_val_free(*max_val);
    *max_val = val;
  }
  else
  {
    isl_val_free(val);
  }

  return isl_stat_ok;
}

/* Insert the global context for introducing the IO module identifiers. 
 * The "node" points to the "kernel" mark.
 * Return the node at the same position.
 */
static __isl_give isl_schedule_node *insert_io_module_context(
  __isl_take isl_schedule_node *node,
  struct autosa_array_ref_group *group,
  struct autosa_gen *gen, struct autosa_kernel *kernel)
{
  int n_io_ids;
  isl_id_list *io_ids;
  isl_set *context;

  n_io_ids = group->space_dim;
  if (n_io_ids <= 0)
    return node;
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");
  n_io_ids = 0;

  /* Update the context. */
  context = isl_set_universe(isl_set_get_space(kernel->context));
  node = autosa_tree_move_down_to_array(node, kernel->core);

  while (!isl_schedule_node_is_io_mark(node, 1))
  {
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
      isl_union_map *umap;
      isl_union_set *uset;
      isl_multi_pw_aff *size;
      isl_id *id;
      isl_id_list *ids;
      isl_union_set *domain;
      isl_union_pw_multi_aff *contraction;

      umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
      domain = isl_schedule_node_get_domain(node);
      contraction = isl_schedule_node_get_subtree_contraction(node);
      domain = isl_union_set_preimage_union_pw_multi_aff(domain, contraction);
      umap = isl_union_map_intersect_domain(umap, domain);
      uset = isl_union_map_range(umap);
      size = ppcg_size_from_extent(isl_set_from_union_set(uset));
      ids = isl_id_list_from_id(isl_id_list_get_id(io_ids, n_io_ids));
      n_io_ids++;
      context = add_bounded_parameters_dynamic(context, size, ids);
      isl_id_list_free(ids);
      isl_multi_pw_aff_free(size);
    }
    node = isl_schedule_node_child(node, 0);
  }
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_context(node, context);
  node = autosa_tree_move_up_to_kernel(node);

  isl_id_list_free(io_ids);

  return node;
}

/* Perform HBM/Multi-port DRAM optimization.
 */
static __isl_give isl_schedule_node *hbm_optimize(
    __isl_take isl_schedule_node *node,
    isl_multi_aff **io_trans_ma,
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    struct autosa_gen *gen)
{
  isl_union_set *uset;
  isl_set *set;
  isl_basic_set *bset;
  isl_union_map *umap;
  isl_val *val;
  isl_ctx *ctx = gen->ctx;
  int tile_len = 1;
  int *tile_size = NULL;
  cJSON *hbm_json, *hbm_mode_json;
  const char *hbm_mode;
  isl_printer *p_str;
  char *module_name;
  int *ubs = NULL;

  /* Parse the tuning configuration. */
  hbm_json = cJSON_GetObjectItemCaseSensitive(gen->tuning_config, "hbm");
  if (!hbm_json)
  {
    /* Default in auto mode. */
    hbm_mode = "auto";
  }
  else
  {
    hbm_mode_json = cJSON_GetObjectItemCaseSensitive(hbm_json, "mode");
    hbm_mode = hbm_mode_json->valuestring;
  }

  ubs = extract_band_upper_bounds(node);
  if (!strcmp(hbm_mode, "auto"))
  {
    /* HBM optimization is set in AUTO mode. 
     * We will pick up the tiling factors by default.
     */
    tile_size = read_default_hbm_tile_sizes(kernel, tile_len);
  }
  else
  {
    /* HBM optimization is set in MANUAL mode. 
     * We will take the user specification to select the HBM factors.
     */
    char *name;
    isl_printer *p_str;
    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "hbm_");
    p_str = autosa_array_ref_group_print_prefix(group, p_str);
    name = isl_printer_get_str(p_str);
    isl_printer_free(p_str);

    tile_size = read_hbm_tile_sizes(kernel, tile_len, name);
    if (!tile_size)
    {
      /* User hasn't specified the tiling factors for HBM optimization yet,
       * we will dump out the number and upper bounds of the last-level IO loops
       * and exit the program.
       */

      FILE *fp;
      char *content;
      cJSON *tuning, *hbm_json, *loops_json;
      isl_printer *p_str;
      char *tuning_path;

      tuning = cJSON_CreateObject();
      hbm_json = cJSON_CreateObject();
      cJSON_AddItemToObject(tuning, name, hbm_json);
      loops_json = cJSON_CreateArray();
      cJSON_AddItemToObject(hbm_json, "tilable_loops", loops_json);
      for (int i = 0; i < tile_len; i++)
      {
        cJSON *loop = cJSON_CreateNumber(ubs[i]);
        cJSON_AddItemToArray(loops_json, loop);
      }
      p_str = isl_printer_to_str(ctx);
      p_str = isl_printer_print_str(p_str, kernel->options->autosa->output_dir);
      p_str = isl_printer_print_str(p_str, "/tuning.json");
      tuning_path = isl_printer_get_str(p_str);
      fp = fopen(tuning_path, "w");
      content = cJSON_Print(tuning);
      fprintf(fp, "%s", content);
      cJSON_Delete(tuning);
      isl_printer_free(p_str);
      free(tuning_path);
      free(name);
      free(ubs);
      exit(0);
    }
    free(name);
  }

  p_str = isl_printer_to_str(ctx);
  p_str = autosa_array_ref_group_print_prefix(group, p_str);
  module_name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  printf("[AutoSA] #HBM port for %s: %d \n", module_name, tile_size[0]);
  free(module_name);

  /* Check if the tile factor is greater or equal than the loop bound. */
  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  uset = isl_union_map_range(umap);
  set = isl_set_from_union_set(uset);
  val = isl_val_zero(ctx);
  isl_set_foreach_basic_set(set, &extract_set_max_dim, &val);
  isl_set_free(set);
  if (isl_val_get_num_si(val) <= tile_size[0])
  {
    /* The current loop bound is smaller than the tile size, 
     * no need to further tile. 
     */
    // TODO: At present, we require tile factor to be greater than the loop bound.
    // This is due to the reason that we can't handle loop with bound one since
    // such loop will be degenerated. Fix it in the future.
    free(tile_size);
    isl_val_free(val);
    printf("[AutoSA] HBM optimization failed! Please try to use a smaller HBM port number.\n");
    return node;
  }
  isl_val_free(val);

  group->n_mem_ports = tile_size[0];
  group->space_dim++;

  tile_size[0] = ubs[0] / tile_size[0];
  node = autosa_tile_band(node, tile_size);
  node = isl_schedule_node_child(node, 0);

  /* Update the transformation function. */
  isl_aff *aff = isl_multi_aff_get_aff(*io_trans_ma, 0);
  isl_aff *tile_aff, *point_aff;
  tile_aff = isl_aff_scale_down_ui(isl_aff_copy(aff), tile_size[0]);
  tile_aff = isl_aff_floor(tile_aff);
  point_aff = isl_aff_scale_down_ui(isl_aff_copy(aff), tile_size[0]);
  point_aff = isl_aff_floor(point_aff);
  point_aff = isl_aff_scale_val(point_aff, isl_val_int_from_ui(ctx, tile_size[0]));
  point_aff = isl_aff_sub(aff, point_aff);

  isl_aff_list *aff_list = isl_aff_list_from_aff(tile_aff);
  aff_list = isl_aff_list_add(aff_list, point_aff);
  for (int n = 1; n < isl_multi_aff_dim(*io_trans_ma, isl_dim_out); n++)
  {
    aff = isl_multi_aff_get_aff(*io_trans_ma, n);
    aff_list = isl_aff_list_add(aff_list, aff);
  }

  isl_space *space = isl_multi_aff_get_space(*io_trans_ma);
  isl_multi_aff_free(*io_trans_ma);
  space = isl_space_add_dims(space, isl_dim_out, 1);
  *io_trans_ma = isl_multi_aff_from_aff_list(space, aff_list);
  free(tile_size);
  free(ubs);

  return node;
}

/* This function examines if the accessed elements of the I/O group 
 * are fully overlapped at the PE level.
 * We will create a relation "overlap"
 * 
 *  [[D -> R] -> [D' -> R']]
 * 
 * of pairs of domain iterations accessing the reference group and 
 * the domain iterations D' is lexicographically greater than D by one 
 * at the last array_part loop with PE loops equal.
 * 
 * This relation is intersected with all flow dependences to derive the 
 * pairs of iterations that overlapped due to the flow dependence.
 * 
 * Next, we construct a relation "external"
 * that contains pair of iteration domains with flow dependences that 
 * access the elements by the I/O group.
 * 
 * We substract "overlap" from "external". If the diff is null, clearly
 * the accessed elements are overlapped between different array partitions 
 * for one PE, we will return true for this case.
 * Otherwise, we return false.
 */
static isl_bool internal_group_in_out_overlap(
    __isl_keep isl_schedule_node *node,
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group, int read)
{
  int empty;
  struct autosa_prog *prog = kernel->prog;
  isl_union_pw_multi_aff *tagger;
  isl_union_map *prefix;
  isl_union_map *access, *tagged;
  isl_union_set *domain;
  isl_set *prefix_range;
  isl_map *lt;
  int n_sched_dim;
  isl_union_map *overlap;
  isl_union_map *external, *universe;
  isl_union_set *access_domain;
  isl_union_set *tag_set;
  isl_map *sched_identity;
  int pe_depth, array_depth;

  node = isl_schedule_node_copy(node);
  node = autosa_tree_move_down_to_array(node, kernel->core);
  array_depth = isl_schedule_node_get_schedule_depth(node);
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  pe_depth = isl_schedule_node_get_schedule_depth(node);
  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  isl_schedule_node_free(node);
  access = autosa_io_group_access_relation(group, kernel, read, !read);
  tagged = group_tagged_access_relation(group);

  /* Remove the local dependency first. */
  access = remove_local_accesses_group_flow(kernel, group, access, prefix, read);
  
//#ifdef _DEBUG
//  DBGUMAP(stdout, access, kernel->ctx);
//  DBGUMAP(stdout, prefix, kernel->ctx);
//#endif

  /* Tagger maps the tagged iteration domain to untagged iteration domain.
   * Iteration domain is tagged to the access function.
   * e.g. [S1[i,j,k] -> _pet_ref_1[]] -> S1[(i),(j),(k)]
   */
  tagger = isl_union_pw_multi_aff_copy(prog->scop->tagger);
  domain = isl_union_map_domain(isl_union_map_copy(tagged));
  tagger = isl_union_pw_multi_aff_intersect_domain(tagger,
                                                   isl_union_set_copy(domain));
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix, tagger);

  prefix_range = isl_set_from_union_set(isl_union_map_range(isl_union_map_copy(prefix)));
  n_sched_dim = isl_set_dim(prefix_range, isl_dim_set);
  sched_identity = isl_set_identity(isl_set_copy(prefix_range));

  lt = isl_map_lex_lt_first(isl_map_get_space(sched_identity), array_depth);
  isl_map_free(sched_identity);

  /* Set the space dims equal. */
  for (int i = array_depth; i < n_sched_dim; i++)
  {
    lt = isl_map_equate(lt, isl_dim_in, i, isl_dim_out, i);
  }
  lt = isl_map_intersect_domain(lt, isl_set_copy(prefix_range));
  lt = isl_map_intersect_range(lt, prefix_range);
  lt = isl_map_lexmin(lt);

  overlap = isl_union_map_apply_range(isl_union_map_copy(prefix),
                                      isl_union_map_from_map(lt));
  overlap = isl_union_map_apply_range(overlap, isl_union_map_reverse(prefix));
  overlap = isl_union_map_coalesce(overlap);

//#ifdef _DEBUG
//  DBGUMAP(stdout, overlap, isl_union_map_get_ctx(overlap));
//  DBGUMAP(stdout, prog->scop->tagged_dep_flow, isl_union_map_get_ctx(overlap));
//#endif
  /* Derive the overlapping set. */
  overlap = isl_union_map_intersect(overlap,
                                    isl_union_map_copy(prog->scop->tagged_dep_flow));
//#ifdef _DEBUG
//  DBGUMAP(stdout, overlap, isl_union_map_get_ctx(overlap));
//#endif                                    
  empty = isl_union_map_is_empty(overlap);

  external = isl_union_map_copy(prog->scop->tagged_dep_flow);
  universe = isl_union_map_universe(isl_union_map_copy(access));
  access_domain = isl_union_map_domain(universe);
  domain = isl_union_set_universe(domain);
  universe = isl_union_set_unwrap(domain);
  universe = isl_union_map_intersect_domain(universe, access_domain);
  /* D -> __pet_ref_1 */
  domain = isl_union_map_wrap(universe);
  if (read)
    external = isl_union_map_intersect_range(external, domain);
  else
    external = isl_union_map_intersect_domain(external, domain);
  external = isl_union_map_intersect_params(external,
                                            isl_set_copy(prog->scop->context));
  /* external contains flow dep that are associated with the group access. */

  external = isl_union_map_subtract(external, overlap);
  /* external only contains access non-overlap RAW pairs. */

  if (read)
  {
    tag_set = isl_union_map_range(external);
    external = wrapped_reference_to_access(tag_set, tagged);
  }
  else
  {
    tag_set = isl_union_map_domain(external);
    external = wrapped_reference_to_access(tag_set, tagged);
  }

  if (empty < 0)
    external = isl_union_map_free(external);
  else if (empty)
    external = isl_union_map_universe(external);

//#ifdef _DEBUG
//  DBGUMAP(stdout, external, isl_union_map_get_ctx(external))
//  DBGUMAP(stdout, access, isl_union_map_get_ctx(access))
//#endif

  access = isl_union_map_intersect(access, external);
  empty = isl_union_map_is_empty(access);
  isl_union_map_free(access);

  if (empty)
    return isl_bool_true;
  else
    return isl_bool_false;
}

/* This function examines if the dependence in the io group are carried by the 
 * loops above the "array" node. 
 */
static isl_bool io_group_carried_by_array_loops(
    __isl_keep isl_schedule_node *node,
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group, int read)
{
  isl_union_map *prefix, *identity_sched;
  isl_union_map *access, *tagged;
  isl_union_pw_multi_aff *tagger;
  isl_union_set *domain, *access_domain;
  struct autosa_prog *prog = kernel->prog;
  isl_set *prefix_range;
  int n_sched_dim;
  isl_map *sched_identity;
  isl_union_map *external, *universe;
  isl_union_set *tag_set;
  int empty;  

  node = isl_schedule_node_copy(node);
  node = autosa_tree_move_down_to_array(node, kernel->core);

  /* Test if the array partition band is empty */
  node = isl_schedule_node_parent(node);
  if (isl_schedule_node_get_type(node) != isl_schedule_node_band) {
    /* No array partitioning, directly return. */
    isl_schedule_node_free(node);
    return isl_bool_false;
  }
  node = autosa_tree_move_down_to_array(node, kernel->core);

  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  isl_schedule_node_free(node);
  access = autosa_io_group_access_relation(group, kernel, read, !read);  
//#ifdef _DEBUG
//  DBGUMAP(stdout, access, kernel->ctx);
//#endif  
  /* Remove the local dependence first. */
  access = remove_local_accesses_group_flow(kernel, group, access, prefix, read);
//#ifdef _DEBUG
//  DBGUMAP(stdout, access, kernel->ctx);
//#endif

  tagged = group_tagged_access_relation(group);
  tagger = isl_union_pw_multi_aff_copy(prog->scop->tagger);
  domain = isl_union_map_domain(isl_union_map_copy(tagged));
  tagger = isl_union_pw_multi_aff_intersect_domain(tagger,
                                                   isl_union_set_copy(domain));

  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix, tagger);  
//#ifdef _DEBUG
//  DBGUMAP(stdout, prefix, isl_union_map_get_ctx(prefix))
//#endif  
  identity_sched = isl_union_map_apply_range(prefix, 
                                             isl_union_map_reverse(isl_union_map_copy(prefix)));
//#ifdef _DEBUG
//  DBGUMAP(stdout, identity_sched, isl_union_map_get_ctx(identity_sched))
//#endif
  identity_sched = isl_union_map_intersect(identity_sched,
                                           isl_union_map_copy(prog->scop->tagged_dep_flow));
//#ifdef _DEBUG
//  DBGUMAP(stdout, identity_sched, kernel->ctx);
//#endif
  empty = isl_union_map_is_empty(identity_sched);

  external = isl_union_map_copy(prog->scop->tagged_dep_flow);
  universe = isl_union_map_universe(isl_union_map_copy(access));
  access_domain = isl_union_map_domain(universe);
  domain = isl_union_set_universe(domain);
  universe = isl_union_set_unwrap(domain);
  universe = isl_union_map_intersect_domain(universe, access_domain);
  domain = isl_union_map_wrap(universe);
  if (read)
    external = isl_union_map_intersect_range(external, domain);
  else
    external = isl_union_map_intersect_domain(external, domain);
  external = isl_union_map_intersect_params(external,
                                            isl_set_copy(prog->scop->context));
  external = isl_union_map_subtract(external, identity_sched);
///#ifdef _DEBUG
///  DBGUMAP(stdout, external, kernel->ctx);
///#endif

  if (read)
  {
    tag_set = isl_union_map_range(external);
    external = wrapped_reference_to_access(tag_set, tagged);
  }
  else
  {
    tag_set = isl_union_map_domain(external);
    external = wrapped_reference_to_access(tag_set, tagged);
  }

  if (empty < 0)
    external = isl_union_map_free(external);
  else if (empty)
    external = isl_union_map_universe(external);

  access = isl_union_map_intersect(access, external);
  empty = isl_union_map_is_empty(access);
  isl_union_map_free(access);

  if (empty)
    return isl_bool_false;
  else
    return isl_bool_true;   
}

/* Return is the inter PE communication is required for this group.
 * There are several cases to consider:
 * - For I/O group with RAR dependences
 *   - if the group is with exterior I/O, then both in/out PE comm is required.
 *   - if the group is with interior I/O, only in PE comm is required.
 * - For I/O group with RAW deps
 *   - If the group is with exterior I/O, then both in/out PE comm is required.
 *   - If the group is with interior I/O, then it equals the array-level I/O direction. 
 */
static isl_bool is_inter_pe_comm_valid(
    __isl_keep isl_schedule_node *node,
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group, int read)
{
  int external_group = 1;

  if (group->group_type == AUTOSA_PE_GROUP)
    return isl_bool_true;
  
  /* External group */
  for (int i = 0; i < group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = group->refs[i];
    for (int j = 0; j < ref->n_io_info; j++)
    {
      struct autosa_io_info *io_info = ref->io_info[j];
      if (io_info->io_type == group->io_type && !isl_vec_cmp(io_info->dir, group->dir))
      {
        if (io_info->dep->type != AUTOSA_DEP_RAR)
        {
          external_group = 0;
          break;
        }
      }
    }
  }

  if (external_group)
  {
    if (group->io_type == AUTOSA_EXT_IO)      
      return isl_bool_true;
    else {
      if (read)
        return isl_bool_true;
      else
        return isl_bool_false;
    }   
  } else {
    if (group->io_type == AUTOSA_EXT_IO)
      return isl_bool_true;
    else {
      if (read) 
        return (group->array_io_dir == IO_IN || group->array_io_dir == IO_INOUT)? isl_bool_true : isl_bool_false;
      else 
        return (group->array_io_dir == IO_OUT || group->array_io_dir == IO_INOUT)? isl_bool_true : isl_bool_false;
    }
  }

  return isl_bool_true;
}

/* Return if the current module is valid to be generated. 
 * There are several cases to consider:
 * - For I/O group with all RAR depenendence, no copy-out modules to be generated.
 * - For I/O group with RAW dependence.
 *   - If the dep is carried by array loops
 *     - if the group is interior I/O and the next read equals the previous write, no copy-in/copy-out to be generated.
 *   - Else if the dep is not carried by array loops
 *     - no copy-in/copy-out to be generated.
 */
isl_bool is_io_module_valid(
    __isl_keep isl_schedule_node *node,
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group, int read)
{
  int external_group = 1;

  if (group->group_type == AUTOSA_PE_GROUP)
    return isl_bool_true;
  if (group->group_type == AUTOSA_DRAIN_GROUP && read)
    return isl_bool_false;
  if (group->attached_drain_group)
    return isl_bool_true;

  /* External group */
  for (int i = 0; i < group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = group->refs[i];
    for (int j = 0; j < ref->n_io_info; j++)
    {
      struct autosa_io_info *io_info = ref->io_info[j];
      if (io_info->io_type == group->io_type && !isl_vec_cmp(io_info->dir, group->dir))
      {
        if (io_info->dep->type != AUTOSA_DEP_RAR)
        {
          external_group = 0;
          break;
        }
      }
    }
  }

  if (external_group)
  {
    if (read)
      return isl_bool_true;
    else
      return isl_bool_false;
  }

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "C") && read) {
//    DBGSCHDNODE(stdout, node, kernel->ctx);
//  }
//#endif

  /* Internal group */
  if (io_group_carried_by_array_loops(node, kernel, group, read)) {
    if (group->io_type == AUTOSA_INT_IO &&
        internal_group_in_out_overlap(node, kernel, group, read))
      return isl_bool_false;
  } else {
    return isl_bool_false;
  }

  return isl_bool_true;
}

/* This function computes the schedule for the I/O modules that transfers
 * the data for the I/O group "group".
 * We will cluster I/O modules level by level. 
 * We will first insert a "IO_L1" mark below the space loops, which indicates
 * IO_L1 modules will be allocated beside each PE.
 * Next, to clulster IO_L1 modules, we look at the space loops above the current
 * mark. We will perform a space-time transformation to cluster the I/O modules.
 * In the current implmentation, we will always use the projection vector (1,X)
 * to project all I/O modules along the direction of (1,X) together, and 
 * schedule them following the direction of (1,X).
 * After one clustering, we will insert a new I/O mark below the new space loops.
 * This is done iteratively untill we run out of the available space loops.
 * The transformed space band will look like:
 * "array" mark
 * |
 * "IO_LX" mark
 * |
 * X 
 * | 
 * "IO_LY" mark
 * |
 * Y
 * |
 * "PE" mark
 */
static isl_stat compute_io_group_schedule(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    struct autosa_gen *gen)
{
  isl_printer *p_str;
  char *io_str;
  int io_level = 0;
  int i;
  isl_ctx *ctx = gen->ctx;
  isl_id *id;
  isl_schedule *sched;
  isl_mat *io_trans_mat = NULL;
  isl_multi_aff *io_trans_ma = NULL;
  isl_map *io_trans_map = NULL;
  isl_schedule_node *node;
  int space_dim;
  isl_schedule *schedule;

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp"))
//    printf("here\n");
//#endif

  /* Sink to the space band */
  schedule = isl_schedule_dup(kernel->schedule);
  node = isl_schedule_get_root(schedule);
  isl_schedule_free(schedule);

  node = autosa_tree_move_down_to_array(node, kernel->core);
  node = isl_schedule_node_child(node, 0);
  space_dim = isl_schedule_node_band_n_member(node);
  group->space_dim = space_dim;

  /* Insert the IO_L1 mark. */
  node = isl_schedule_node_child(node, 0);
  p_str = isl_printer_to_str(ctx);
  p_str = isl_printer_print_str(p_str, "io_L");
  p_str = isl_printer_print_int(p_str, io_level + 1);
  io_str = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  id = isl_id_alloc(ctx, io_str, NULL);
  free(io_str);
  node = isl_schedule_node_insert_mark(node, id);
  io_level++;
  node = isl_schedule_node_parent(node);

  /* Cluster the I/O modules from innermost space loops to outermost loops. */
  for (int i = space_dim - 1; i >= 0; i--)
  {
    isl_mat *io_trans_mat_i;
    isl_multi_aff *io_trans_ma_i;
    isl_vec *dir;
    isl_mat *mat;

    /* Perform space-time transformation on the current band. */
    //if (i == space_dim - 1 && group->io_type == AUTOSA_EXT_IO)
    if (i == space_dim - 1)
    {      
      dir = isl_vec_dup(group->dir);
    }
    else
    {
      /* By default, we set the first element of the direction vector as 1. */
      dir = isl_vec_zero(ctx, i + 1);
      dir = isl_vec_set_element_si(dir, 0, 1);
    }
//#ifdef _DEBUG
//    DBGVEC(stdout, dir, isl_schedule_node_get_ctx(node));
//#endif
    node = io_cluster(node, dir, &io_trans_mat_i, &io_trans_ma_i);
    isl_vec_free(dir);

    if (io_level == 1)
    {
      sched = isl_schedule_node_get_schedule(node);
      group->io_L1_schedule = isl_schedule_dup(sched);
      // TODO: if the space schedule is to be degenerated, we
      // will need to update the io_trans/io_L1_trans as well.
      group->io_L1_trans = isl_multi_aff_copy(io_trans_ma_i);

      isl_schedule_free(sched);
      io_trans_mat = io_trans_mat_i;
      io_trans_ma = io_trans_ma_i;
    }
    else
    {
      isl_multi_aff_free(io_trans_ma_i);
      /* Update the transformation matrix. */
      int nrow = isl_mat_rows(io_trans_mat);
      int ncol = isl_mat_cols(io_trans_mat);
      isl_mat *extend_mat = isl_mat_alloc(ctx, nrow, ncol);
      isl_mat *product_mat = isl_mat_alloc(ctx, nrow, ncol);
      for (int r = 0; r < nrow; r++)
        for (int c = 0; c < ncol; c++)
        {
          extend_mat = isl_mat_set_element_si(extend_mat, r, c, 0);
          product_mat = isl_mat_set_element_si(product_mat, r, c, 0);
        }

      for (int r = 0; r < isl_mat_rows(io_trans_mat_i); r++)
        for (int c = 0; c < isl_mat_cols(io_trans_mat_i); c++)
        {
          extend_mat = isl_mat_set_element_val(extend_mat, r, c,
                                               isl_mat_get_element_val(io_trans_mat_i, r, c));
        }
      for (int r = isl_mat_rows(io_trans_mat_i); r < nrow; r++)
      {
        extend_mat = isl_mat_set_element_si(extend_mat, r, r, 1);
      }
      for (int r = 0; r < nrow; r++)
        for (int c = 0; c < ncol; c++)
        {
          for (int k = 0; k < nrow; k++)
          {
            isl_val *v1, *v2, *v3;
            v1 = isl_mat_get_element_val(extend_mat, r, k);
            v2 = isl_mat_get_element_val(io_trans_mat, k, c);
            v3 = isl_mat_get_element_val(product_mat, r, c);
            v1 = isl_val_mul(v1, v2);
            v3 = isl_val_add(v1, v3);
            product_mat = isl_mat_set_element_val(product_mat, r, c, v3);
          }
        }
      isl_mat_free(io_trans_mat);
      isl_mat_free(extend_mat);
      isl_mat_free(io_trans_mat_i);
      io_trans_mat = product_mat;

      /* Reset the transformation function. */
      for (int r = 0; r < nrow; r++)
      {
        isl_aff *aff = isl_multi_aff_get_aff(io_trans_ma, r);
        for (int c = 0; c < ncol; c++)
        {
          isl_val *val = isl_mat_get_element_val(io_trans_mat, r, c);          
          aff = isl_aff_set_coefficient_si(aff, isl_dim_in, c, isl_val_get_num_si(val));          
          isl_val_free(val);
        }
        io_trans_ma = isl_multi_aff_set_aff(io_trans_ma, r, aff);
      }
    }

    /* Split the band and insert the IO mark. */
    if (i > 0)
    {
      node = isl_schedule_node_band_split(node, i);
      node = isl_schedule_node_child(node, 0);
    }

    /* If the multi-port DRAM/HBM is to be used, we will need to tile the loop again.
     */
    if (i == 0 && gen->options->autosa->hbm)
    {
      /* Test if this group contains both copy-in and copy-out set. 
       * At present, HBM optimization is not supported for this type of I/O group.
       * We will need to make sure the copy-in and copy-out set for each HBM channel 
       * do not overlap since we only support fixed HBM port mapping for now.
       * Therefore, for this type of I/O group, we will disable the HBM optimization.
       * TODO: Relax this constraint in the future.
       */
      printf("[AutoSA] Apply HBM optimization.\n");
      if (group->group_type == AUTOSA_IO_GROUP &&
          is_flow_dep_carried_by_array_part_loops(kernel->schedule, group, kernel))
      {
        isl_printer *p_str;
        char *module_name;
        p_str = isl_printer_to_str(ctx);
        p_str = autosa_array_ref_group_print_prefix(group, p_str);
        module_name = isl_printer_get_str(p_str);
        isl_printer_free(p_str);

        printf("[AutoSA] The flow dependence is carried by the array partitioning loops.\n");
        printf("[AutoSA] HBM optimization for the group: %s is omitted.\n", module_name);
        free(module_name);
        goto next;
      }
      if (group->io_type == AUTOSA_EXT_IO && i == space_dim - 1)
      {
        printf("[AutoSA] HBM optimization failed! Not enough I/O modules.\n");
        goto next;
      }
      node = hbm_optimize(node, &io_trans_ma, kernel, group, gen);
    }
  next:
    p_str = isl_printer_to_str(ctx);
    p_str = isl_printer_print_str(p_str, "io_L");
    p_str = isl_printer_print_int(p_str, io_level + 1);
    io_str = isl_printer_get_str(p_str);
    isl_printer_free(p_str);
    id = isl_id_alloc(ctx, io_str, NULL);
    free(io_str);
    node = isl_schedule_node_insert_mark(node, id);
    node = isl_schedule_node_parent(node);
    io_level++;
  }

  isl_mat_free(io_trans_mat);  

  group->io_level = io_level;
  group->io_trans = io_trans_ma;

  /* Insert the context node for the IO ids. 
   * NOTE: We will update this again in the later IO module generation.
   */
  node = autosa_tree_move_up_to_kernel(node);
  node = insert_io_module_context(node, group, gen, kernel);

  /* Determine if the I/O module for this group could be eliminated.
   */
  group->copy_in = 0;
  group->copy_out = 0;
  if (is_io_module_valid(node, kernel, group, 1))
  {
    group->copy_in = 1;
    group->array_io_dir = (group->array_io_dir == IO_OUT)? IO_INOUT : IO_IN;
  }
  if (is_io_module_valid(node, kernel, group, 0))
  {
    group->copy_out = 1;
    group->array_io_dir = (group->array_io_dir == IO_IN)? IO_INOUT : IO_OUT;
  }
  /* For drain group, copy-out module is always required. */
  if (group->group_type == AUTOSA_DRAIN_GROUP) {
    group->copy_out = 1;
    group->array_io_dir = (group->array_io_dir == IO_IN)? IO_INOUT : IO_OUT;
  }

  if (group->copy_in || group->copy_out)
  {
    group->mem_port_id = group->local_array->n_mem_ports;
    group->local_array->n_mem_ports += group->n_mem_ports;
  }

  /* Determine if the inter-PE communication is required. */
  if (is_inter_pe_comm_valid(node, kernel, group, 1)) {
    group->pe_io_dir = (group->pe_io_dir == IO_OUT)? IO_INOUT : IO_IN;
  }
  if (is_inter_pe_comm_valid(node, kernel, group, 0)) {
    group->pe_io_dir = (group->pe_io_dir == IO_IN)? IO_INOUT : IO_OUT;
  }
  if (group->group_type == AUTOSA_DRAIN_GROUP) {
    group->pe_io_dir = (group->pe_io_dir == IO_IN)? IO_INOUT : IO_OUT;
  }

  /* Store the I/O schedule. */
  sched = isl_schedule_node_get_schedule(node);
  group->io_schedule = isl_schedule_dup(sched);
  isl_schedule_free(sched);
  isl_schedule_node_free(node);

//#ifdef _DEBUG
//  if (!strcmp(group->array->name, "U_tmp") && group->nr == 1) {
//    //printf("print here\n");
//    //print_code(gen, group->io_L1_schedule, "U_tmp_1_tmp_code.c");
//    DBGSCHD(stdout, group->io_L1_schedule, ctx);
//  }
//#endif

  return isl_stat_ok;
}

static __isl_give isl_map *local_access_io_at_node(struct autosa_kernel *kernel,
                                                   struct autosa_array_ref_group *group,
                                                   __isl_keep isl_union_map *access, __isl_keep isl_schedule_node *node)
{
  isl_union_map *local, *sched;
  isl_union_pw_multi_aff *contraction;

  local = isl_union_map_copy(access);
  sched = prefix_with_equalities(node);
  // TODO: fix the contraction
  contraction = isl_schedule_node_get_subtree_contraction(node);
  /* #ifdef _DEBUG
  isl_printer *pd = isl_printer_to_file(isl_schedule_node_get_ctx(node), stdout);
  pd = isl_printer_print_union_pw_multi_aff(pd, contraction);
  pd = isl_printer_end_line(pd);
  isl_printer_free(pd);
#endif */

  sched = expand(sched, contraction);
  local = isl_union_map_apply_domain(local, sched);

  isl_union_pw_multi_aff_free(contraction);

  return isl_map_from_union_map(local);
}

/* Compute the local memory tiles for the drain group "group"
 * of array "array". Return isl_stat_ok on success and isl_stat_error on error.
 *
 * If the array is a read-only scalar or if the user requested not to use local
 * memory, then we do not need to do anything.
 */
isl_stat compute_group_bounds_drain_at_node(struct autosa_kernel *kernel,
                                            struct autosa_array_ref_group *group, __isl_keep isl_schedule_node *node,
                                            struct autosa_io_buffer *buffer)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. */
  access = autosa_array_ref_group_access_relation(group, 0, 1);
  /* Create local tile */
  if (use_local)
  {
    /* Create a tile */
    buffer->tile = autosa_array_tile_create(ctx, group->array->n_index);
    /* Map the domain to the outer scheduling dimensions */
    acc = local_access_io_at_node(kernel, group, access, node);
    /* Collect the shift and scale factors of the tile */
    ok = can_tile(acc, buffer->tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      buffer->tile = autosa_array_tile_free(buffer->tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Should this array reference group be mapped to local or global
 * memory?
 * If the array is scalar, we will map it to the global memory.
 * Otherwise, it is mapped to local memory. 
 */
enum autosa_group_access_type autosa_array_ref_group_type(
    struct autosa_array_ref_group *group)
{
  if (autosa_array_is_read_only_scalar(group->array))
    return AUTOSA_ACCESS_GLOBAL;
  else
    return AUTOSA_ACCESS_LOCAL;
}

/* Return the effective array_tile associated to "group" or
 * NULL if there is no such array_tile.
 */
struct autosa_array_tile *autosa_array_ref_group_tile(
    struct autosa_array_ref_group *group)
{
  switch (autosa_array_ref_group_type(group))
  {
  case AUTOSA_ACCESS_GLOBAL:
    return NULL;
  case AUTOSA_ACCESS_LOCAL:
    return group->local_tile;
  }

  return NULL;
}

/* Should this array reference group be mapped to local or global
 * memory?
 */
enum autosa_group_access_type autosa_cpu_array_ref_group_type(
    struct autosa_array_ref_group *group)
{
  if (group->local_tile)
    return AUTOSA_ACCESS_LOCAL;
  return AUTOSA_ACCESS_GLOBAL;
}

/* Given a description of an array tile "tile" and the "space"
 *
 *	{ D -> A }
 *
 * where D represents the first tile->depth schedule dimensions
 * and A represents the array, construct an isl_multi_aff
 *
 *	{ [D[i] -> A[a]] -> A'[a'] }
 *
 * with A' a scaled down copy of A according to the shifts and strides
 * in "tile".  In particular,
 *
 *	a' = (a + shift(i))/stride
 *
 * "insert_array" represents
 *
 *	{ [D -> A] -> D }
 *
 * and is used to insert A into the domain of functions that only
 * reference D.
 */
static __isl_give isl_multi_aff *strided_tile(
    struct autosa_array_tile *tile, __isl_keep isl_space *space,
    __isl_keep isl_multi_aff *insert_array)
{
  int i;
  isl_ctx *ctx;
  isl_multi_aff *shift;
  isl_multi_val *stride;
  isl_space *space2;
  isl_local_space *ls;
  isl_multi_aff *tiling;

  ctx = isl_space_get_ctx(space);
  space2 = isl_space_domain(isl_space_copy(space));
  ls = isl_local_space_from_space(space2);
  space2 = isl_space_range(isl_space_copy(space));
  stride = isl_multi_val_zero(space2);
  shift = isl_multi_aff_zero(isl_space_copy(space));

  for (i = 0; i < tile->n; ++i)
  {
    struct autosa_array_bound *bound = &tile->bound[i];
    isl_val *stride_i;
    isl_aff *shift_i;

    stride_i = isl_val_copy(bound->stride);
    shift_i = isl_aff_copy(bound->shift);

    stride = isl_multi_val_set_val(stride, i, stride_i);
    shift = isl_multi_aff_set_aff(shift, i, shift_i);
  }
  isl_local_space_free(ls);

  shift = isl_multi_aff_pullback_multi_aff(shift,
                                           isl_multi_aff_copy(insert_array));

  tiling = isl_multi_aff_range_map(isl_space_copy(space));
  tiling = isl_multi_aff_add(tiling, shift);
  tiling = isl_multi_aff_scale_down_multi_val(tiling, stride);

  return tiling;
}

/* Print the name of the local copy of a given group of array references.
 */
__isl_give isl_printer *autosa_array_ref_group_print_name(
    struct autosa_array_ref_group *group, __isl_take isl_printer *p)
{
  int global = 0;
  enum autosa_group_access_type type;

  type = autosa_array_ref_group_type(group);
  if (type == AUTOSA_ACCESS_LOCAL)
    p = isl_printer_print_str(p, "local_");
  else
    global = 1;

  p = isl_printer_print_str(p, group->array->name);
  if (!global)
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

/* Compute a tiling for the array reference group "group".
 *
 * The tiling is of the form
 *
 *	{ [D[i] -> A[a]] -> T[t] }
 *
 * where D represents the first tile->depth schedule dimensions,
 * A represents the global array and T represents the local memory 
 * tile.  The name of T is the name of the local array.
 *
 * If there is any stride in the accesses, then the mapping is
 *
 *	t = (a + shift(i))/stride - lb(i)
 *
 * otherwise, it is simply
 *
 *	t = a - lb(i)
 *
 * Compute the tiling based on the "tile". If "tile" is NULL, 
 * compute the tiling based on the tile from the "group".
 */
void autosa_array_ref_group_compute_tiling(
    struct autosa_array_tile *tile,
    struct autosa_array_ref_group *group)
{
  int i;
  isl_space *space;
  isl_multi_aff *tiling, *lb, *insert_array;
  isl_printer *p;
  char *local_name;

  if (tile == NULL && autosa_array_ref_group_tile(group) == NULL)
    return;

  if (tile == NULL)
    tile = autosa_array_ref_group_tile(group);

  space = isl_map_get_space(group->access);
  space = isl_space_from_range(isl_space_range(space));
  /* Build D[i] -> A[a] */
  space = isl_space_add_dims(space, isl_dim_in, tile->depth);
  /* Build [D[i] -> A[a]] -> D[i] */
  insert_array = isl_multi_aff_domain_map(isl_space_copy(space));

  for (i = 0; i < tile->n; ++i)
    if (tile->bound[i].shift)
      break;

  if (i < tile->n)
    tiling = strided_tile(tile, space, insert_array);
  else
    tiling = isl_multi_aff_range_map(isl_space_copy(space));

  lb = isl_multi_aff_zero(space);
  for (i = 0; i < tile->n; ++i)
  {
    isl_aff *lb_i = isl_aff_copy(tile->bound[i].lb);
    lb = isl_multi_aff_set_aff(lb, i, lb_i);
  }
  lb = isl_multi_aff_pullback_multi_aff(lb, insert_array);

  tiling = isl_multi_aff_sub(tiling, lb);

  p = isl_printer_to_str(isl_multi_aff_get_ctx(tiling));
  p = autosa_array_ref_group_print_name(group, p);
  local_name = isl_printer_get_str(p);
  isl_printer_free(p);
  tiling = isl_multi_aff_set_tuple_name(tiling, isl_dim_out, local_name);
  free(local_name);

  tile->tiling = tiling;
}

/* Compute the tiling bounds for the drain group at the PE level. 
 */
static isl_stat compute_group_bounds_drain_at_node_PE(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    __isl_keep isl_schedule_node *node)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. */
  access = autosa_array_ref_group_access_relation(group, 0, 1);
  /* Create local tile. */
  if (use_local)
  {
    /* Create a tile. */
    group->pe_tile = autosa_array_tile_create(ctx, group->array->n_index);
    /* Map the domain to the outer scheduling dimensions. */
    acc = local_access_io_at_node(kernel, group, access, node);
    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, group->pe_tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      group->pe_tile = autosa_array_tile_free(group->pe_tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Compute the drain group tiling at the PE level. */
static isl_stat compute_drain_tiling_at_PE(struct autosa_kernel *kernel,
                                           struct autosa_array_ref_group *group)
{
  isl_schedule_node *node;
  struct autosa_array_tile *tile;

  node = isl_schedule_get_root(kernel->schedule);
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  compute_group_bounds_drain_at_node_PE(kernel, group, node);
  autosa_array_ref_group_compute_tiling(group->pe_tile, group);
  isl_schedule_node_free(node);

  return isl_stat_ok;
}

/* Compute the local memory tiles for the io group "group"
 * of array "array". Return isl_stat_ok on success and isl_stat_error on error.
 *
 * If the array is a read-only scalar or if the user requested not to use local
 * memory, then we do not need to do anything.
 */
isl_stat compute_group_bounds_io_at_node(struct autosa_kernel *kernel,
                                         struct autosa_array_ref_group *group, __isl_keep isl_schedule_node *node,
                                         struct autosa_io_buffer *buffer)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. */
  access = autosa_array_ref_group_access_relation(group, 1, 1);
  /* Create local tile. */
  if (use_local)
  {
    /* Create a tile. */
    buffer->tile = autosa_array_tile_create(ctx, group->array->n_index);
    /* Map the domain to the outer scheduling dimensions. */
    acc = local_access_io_at_node(kernel, group, access, node);
    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, buffer->tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      buffer->tile = autosa_array_tile_free(buffer->tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Compute the tiling group bounds for the io group at the PE level. */
isl_stat compute_group_bounds_io_at_node_PE(
    struct autosa_kernel *kernel,
    struct autosa_array_ref_group *group, __isl_keep isl_schedule_node *node)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. */
  access = autosa_array_ref_group_access_relation(group, 1, 1);
  /* Create local tile. */
  if (use_local)
  {
    /* Create a tile. */
    group->pe_tile = autosa_array_tile_create(ctx, group->array->n_index);
    /* Map the domain to the outer scheduling dimensions. */
    acc = local_access_io_at_node(kernel, group, access, node);
    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, group->pe_tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      group->pe_tile = autosa_array_tile_free(group->pe_tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Create the tiling for the IO group at the PE level. */
static isl_stat compute_io_tiling_at_PE(struct autosa_kernel *kernel,
                                        struct autosa_array_ref_group *group)
{
  isl_schedule_node *node;
  struct autosa_array_tile *tile;

  node = isl_schedule_get_root(kernel->schedule);
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  compute_group_bounds_io_at_node_PE(kernel, group, node);
  autosa_array_ref_group_compute_tiling(group->pe_tile, group);
  isl_schedule_node_free(node);

  return isl_stat_ok;
}

/* Insert the IO module filter ids into the schedule.
 * "node" points to the IO_L[io_level] mark.
 * Return the new node points to the same position.
 */
static __isl_give isl_schedule_node *insert_io_module_ids(
    struct autosa_gen *gen, struct autosa_kernel *kernel,
    __isl_take isl_schedule_node *node, int space_dim, int io_level)
{
  int n_io_ids;
  isl_id_list *io_ids;
  isl_set *context;
  isl_union_set *filter = NULL;

  n_io_ids = space_dim - io_level + 1;
  if (n_io_ids <= 0)
    return node;
  io_ids = ppcg_scop_generate_names(gen->prog->scop, n_io_ids, "p");
  n_io_ids = 0;

  /* Add the filters. */
  n_io_ids = 0;
  node = autosa_tree_move_up_to_array(node);
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
      if (filter == NULL)
        filter = uset;
      else
        filter = isl_union_set_union(filter, uset);

      isl_id_list_free(ids);      
    }
    node = isl_schedule_node_child(node, 0);
  }

  isl_id_list_free(io_ids);
  /* Insert the filter. */
  node = autosa_tree_move_up_to_kernel(node);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_filter(node, filter);
  node = autosa_tree_move_down_to_io_mark(node, kernel->core, io_level);

  return node;
}

/* Allocate I/O buffers at each I/O level.
 * If two-level buffer is disabled, we will only allocate buffer 
 * at the innermost level for each group:
 * - drain group @ io_L1
 * - io group @ io_L1 (INT_IO) | io_L2 (EXT_IO)
 * If two-level buffer is turned on, we will also allocate buffers
 * at the outermost level for each group.
 */
static isl_stat compute_io_group_buffer(struct autosa_kernel *kernel,
                                        struct autosa_array_ref_group *group, struct autosa_gen *gen)
{
  isl_schedule_node *node;
  int io_level = group->io_level;
  int i;
  int two_level_buffer = gen->options->autosa->two_level_buffer;

  node = isl_schedule_get_root(group->io_schedule);

  /* Compute the group tiling at each I/O level. */
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  i = 1;
  assert(group->io_buffers == NULL);
  assert(group->n_io_buffer == 0);
  group->io_buffers = NULL;
  group->n_io_buffer = 0;
  while (i <= io_level)
  {
    isl_schedule_node *node_cp = NULL;
    node = isl_schedule_node_parent(node);
    if (isl_schedule_node_is_io_mark(node, i))
    {
      /* In the automatic mode, AutoSA only computes the tiling at L1
       * for drain group and I/O group with interior I/O, and at L2 for I/O 
       * group with exterior I/O.
       */
      (group->n_io_buffer)++;
      group->io_buffers = (struct autosa_io_buffer **)realloc(
          group->io_buffers, sizeof(struct autosa_io_buffer *) * group->n_io_buffer);
      group->io_buffers[group->n_io_buffer - 1] = autosa_io_buffer_alloc();          
      group->io_buffers[group->n_io_buffer - 1]->level = i;
      group->io_buffers[group->n_io_buffer - 1]->tile = NULL;

      node_cp = isl_schedule_node_copy(node);      
      if (group->group_type == AUTOSA_DRAIN_GROUP)
      {
        if (i == 1)
        {
          /* Compute the group tiling at this level */
          compute_group_bounds_drain_at_node(kernel, group, node_cp,
                                             group->io_buffers[group->n_io_buffer - 1]);
          autosa_array_ref_group_compute_tiling(
              group->io_buffers[group->n_io_buffer - 1]->tile, group);
          compute_drain_tiling_at_PE(kernel, group);
        }
        else
        {
          group->io_buffers[group->n_io_buffer - 1]->tile = NULL;
        }
      }
      else if (group->group_type == AUTOSA_IO_GROUP)
      {
        if ((group->io_type == AUTOSA_EXT_IO && i == 2) ||
            (group->io_type == AUTOSA_INT_IO && i == 1))
        {
          /* Compute the group tiling at this level. */
          compute_group_bounds_io_at_node(kernel, group, node_cp,
                                          group->io_buffers[group->n_io_buffer - 1]);
          autosa_array_ref_group_compute_tiling(
              group->io_buffers[group->n_io_buffer - 1]->tile, group);
          if (group->io_type == AUTOSA_INT_IO && i == 1)
          {
            compute_io_tiling_at_PE(kernel, group);
          }
        }
        else
        {
          group->io_buffers[group->n_io_buffer - 1]->tile = NULL;
        }
      }
      else
      {
        group->io_buffers[group->n_io_buffer - 1]->tile = NULL;
      }
      if (two_level_buffer)
      {
        if (i == io_level)
        {          
          /* Compute the group tiling at the outermost I/O module. */
          if (group->group_type == AUTOSA_DRAIN_GROUP)
            compute_group_bounds_drain_at_node(kernel, group, node_cp, group->io_buffers[group->n_io_buffer - 1]);
          else if (group->group_type == AUTOSA_IO_GROUP)
            compute_group_bounds_io_at_node(kernel, group, node_cp, group->io_buffers[group->n_io_buffer - 1]);

          autosa_array_ref_group_compute_tiling(group->io_buffers[group->n_io_buffer - 1]->tile, group);
        }
      }      
      isl_schedule_node_free(node_cp);
      i++;
    }
  }

  isl_schedule_node_free(node);

  return isl_stat_ok;
}

/* Adjust the fields of "tile" to reflect the new input dimension "depth".
 * The dimension beyond "depth" are assumed not to affect the tile,
 * so they can simply be dropped.
 */
static int tile_adjust_depth(struct autosa_array_tile *tile, int depth)
{
  int i;

  if (tile->depth == depth)
    return 0;

  for (i = 0; i < tile->n; ++i)
  {
    tile->bound[i].lb = isl_aff_drop_dims(tile->bound[i].lb,
                                          isl_dim_in, depth, tile->depth - depth);
    if (!tile->bound[i].lb)
      return -1;
    if (!tile->bound[i].shift)
      continue;
    tile->bound[i].shift = isl_aff_drop_dims(tile->bound[i].shift,
                                             isl_dim_in, depth, tile->depth - depth);
    if (!tile->bound[i].shift)
      return -1;
  }

  tile->depth = depth;

  return 0;
}

/* Compute the number of outer schedule tile dimensions that affect
 * the offset of "tile".
 * If there is no such dimension, then return the index
 * of the first kernel dimension, i.e., data->kernel_depth.
 */
static int compute_tile_depth(struct autosa_group_data *data,
                              struct autosa_array_tile *tile)
{
  int i, j;

  for (j = tile->depth - 1; j >= data->kernel_depth; --j)
  {
    for (i = 0; i < tile->n; ++i)
    {
      isl_aff *lb;
      isl_aff *shift;

      lb = tile->bound[i].lb;
      if (isl_aff_involves_dims(lb, isl_dim_in, j, 1))
        break;

      shift = tile->bound[i].shift;
      if (!shift)
        continue;
      if (isl_aff_involves_dims(shift, isl_dim_in, j, 1))
        break;
    }
    if (i < tile->n)
      break;
  }

  return ++j;
}

/* Determine the number of schedule dimensions that affect the offset of the
 * local tile "tile" and store the result in tile->depth, with
 * a lower bound of data->kernel_depth.
 * Also adjust the fields of the tile to only refer to the tile->depth
 * outer schedule dimensions.
 */
static isl_stat tile_set_depth(struct autosa_group_data *data,
                               struct autosa_array_tile *tile)
{
  if (tile_adjust_depth(tile, compute_tile_depth(data, tile)) < 0)
    return isl_stat_error;

  return isl_stat_ok;
}

/* Internal struct used for update_group_simd. */
struct update_group_simd_data
{
  struct autosa_array_ref_group *group;
  struct autosa_kernel *kernel;
};

/* Examine if there is any array references in the "group" under the SIMD loop.
 * If so, exmaine if the array reference has a stride of 1 under the SIMD loop.
 * If so, update the SIMD lane of the "group".
 */
static isl_bool update_group_simd(__isl_keep isl_schedule_node *node, void *user)
{
  struct update_group_simd_data *data = (struct update_group_simd_data *)user;

  if (isl_schedule_node_get_type(node) == isl_schedule_node_mark)
  {
    isl_id *id;
    isl_union_set *domain;
    struct autosa_array_ref_group *group = data->group;

    id = isl_schedule_node_mark_get_id(node);
    if (strcmp(isl_id_get_name(id), "simd"))
    {
      isl_id_free(id);
      return isl_bool_true;
    }

    isl_id_free(id);
    node = isl_schedule_node_child(node, 0);
    domain = isl_schedule_node_get_domain(node);
    for (int i = 0; i < group->n_ref; i++)
    {
      struct autosa_stmt_access *ref = group->refs[i];
      for (int j = 0; j < ref->n_io_info; j++)
      {
        struct autosa_io_info *info = ref->io_info[j];
        if (info->io_type == group->io_type && !isl_vec_cmp(info->dir, group->dir))
        {
          /* Test if either the source or dest of the dependence associated with
           * the array reference is intersected with the current loop domain. */
          struct autosa_dep *dep = info->dep;
          isl_basic_map *bmap;
          isl_map *map;
          isl_set *src, *dest;
          isl_union_set *uset;
          bmap = isl_basic_map_copy(dep->isl_dep);
          map = isl_map_from_basic_map(bmap);
          map = isl_map_factor_domain(map);
          src = isl_map_domain(isl_map_copy(map));
          dest = isl_map_range(map);
          uset = isl_union_set_union(isl_union_set_from_set(src),
                                     isl_union_set_from_set(dest));
          uset = isl_union_set_intersect(uset, isl_union_set_copy(domain));
          if (!isl_union_set_is_empty(uset))
          {
            if (ref->simd_stride == 1)
              group->n_lane = data->kernel->simd_w;
          }
          isl_union_set_free(uset);
        }
      }
    }
    isl_union_set_free(domain);
  }

  return isl_bool_true;
}

/* Select the data pack factor for I/O buffers. For this function, the array
 * that the I/O group is assoicated with is a sparse matrix.
 * The unit of data packing factor is the non_zero_num elements + one offset.
 */
static isl_stat compute_io_group_data_pack_sparse(
  struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
  struct autosa_gen *gen, int max_n_lane)
{
  isl_schedule_node *node;
  isl_union_map *sizes;
  int *data_pack_ubs = NULL;
  struct update_group_simd_data data;
  int ele_size = group->array->size; // bytes
  /* Given the maximal DRAM port width as 64 Bytes, 
   * compute the maximal data pack factor. */
  //if (max_n_lane == -1)
  //  max_n_lane = 64 / ele_size;

  group->n_lane = 1;
  node = isl_schedule_get_root(kernel->schedule);
  data.group = group;
  data.kernel = kernel;
  isl_schedule_node_foreach_descendant_top_down(node, &update_group_simd, &data);
  isl_schedule_node_free(node);
  /* Update the group n_lane considering the sparse information */
  if (group->n_lane % kernel->vec_len != 0) {
    printf("[AutoSA] Error: The sparse block size is not a sub-multiple of the SIMD factor. Abort!\n");
    exit(1);
  }
  group->n_lane /= kernel->vec_len;
  
  /* If data packing is disabled, simply update the data packing factor of 
   * each I/O buffer to the SIMD lanes that are required. 
   */
  if (!gen->options->autosa->data_pack) {
    for (int i = 0; i < group->io_level; i++) {
      struct autosa_io_buffer *buf = group->io_buffers[i];
      buf->n_lane = group->n_lane;
      /* Update the sparse information */
      buf->sparse = 1;
      buf->vec_len = kernel->vec_len;
    }
    return isl_stat_ok;
  }

  int cur_n_lane = group->n_lane;
  int status = false;
  /* Parse the data pack settings. */
  sizes = extract_sizes_from_str(gen->ctx, gen->options->autosa->data_pack_sizes);
  //data_pack_ubs = read_data_pack_sizes(sizes, 3);
  data_pack_ubs = read_data_pack_sizes_array(sizes, group->array->name);
  if (!data_pack_ubs) {
    /* Use the default numbers. */
    data_pack_ubs = isl_alloc_array(gen->ctx, int, 3);
    data_pack_ubs[0] = 8;
    data_pack_ubs[1] = 32;
    data_pack_ubs[2] = 64;
  }

  int cur_max_n_lane;
  for (int i = 0; i < group->io_level; i++) {
    struct autosa_io_buffer *buf = group->io_buffers[i];
    if (i == 0)
      cur_max_n_lane = max(group->n_lane, data_pack_ubs[0] / (kernel->n_nzero * ele_size + 1));
    else if (i > 0 && i < group->io_level - 1)
      cur_max_n_lane = max(group->n_lane, data_pack_ubs[1] / (kernel->n_nzero * ele_size + 1));
    else
      cur_max_n_lane = max(group->n_lane, data_pack_ubs[2] / ((kernel->n_nzero + kernel->n_meta_data) * ele_size));
    if (buf->tile) {      
      int n_lane = cur_n_lane;
      isl_val *size = isl_val_copy(buf->tile->bound[group->array->n_index - 1].size);
      if (i == group->io_level - 1 && group->local_array->host_serialize) {
        for (int n = 0; n < group->array->n_index - 1; n++) {
          size = isl_val_mul(size, isl_val_copy(buf->tile->bound[n].size));
        }        
      }      
      size = isl_val_div(size, isl_val_int_from_si(gen->ctx, kernel->vec_len));

      while (n_lane <= cur_max_n_lane) {
        /* The lane should be multiples of SIMD lane. */
        if (n_lane % group->n_lane == 0) {
          isl_val *val = isl_val_int_from_si(gen->ctx, n_lane);
          /* The lane should be sub-multiples of the last dim of the array. */
          if (isl_val_is_divisible_by(size, val)) {
            cur_n_lane = n_lane;
            status = true;
          }
          isl_val_free(val);
        }
        //n_lane *= 2;
        n_lane += 1;
      }
      if (status) {
        buf->n_lane = cur_n_lane;        
      } else {
        printf("[AutoSA] Error: Cannot find data pack factors as sub-multiples of the last dim of the local array. Abort!\n");
        printf("[AutoSA] Please try to use different tiling factors.\n");
        exit(1);
      }
      isl_val_free(size);
    } else {
      buf->n_lane = cur_n_lane;
    }    
    /* Update the sparse information */
    buf->sparse = 1;
    buf->vec_len = kernel->vec_len;
  }
  isl_union_map_free(sizes);
  free(data_pack_ubs);

  return isl_stat_ok;
}

/* Select the data pack factor for I/O buffers. The data pack factor
 * should be sub-multiples of the last dimension of the local array.
 * Meanwhile, it should also be sub-multiples of the data pack factors 
 * selected for the upper-level I/O buffers.
 * 
 * If SIMD vectorization is enabled, and the data stored in the I/O buffer is 
 * to be vectorized, the data pack factor should also be multiples of the SIMD factor.
 */
static isl_stat compute_io_group_data_pack(struct autosa_kernel *kernel,
                                           struct autosa_array_ref_group *group,
                                           struct autosa_gen *gen,
                                           int max_n_lane)
{
  isl_schedule_node *node;
  isl_union_map *sizes;
  isl_val *size;
  int *data_pack_ubs = NULL;
  struct update_group_simd_data data;
  int ele_size = group->array->size; // bytes
  /* Given the maximal DRAM port width as 64 Bytes, 
   * compute the maximal data pack factor. */
  if (max_n_lane == -1)
    max_n_lane = 64 / ele_size;

  /* Examine if any of the array reference in the group is in used by SIMD loop.
   * The default SIMD lane for the group is 1. 
   * If any of the array references in the group is under the SIMD loop, and 
   * if the stride of reference under the loop is one. The SIMD lane of the 
   * group is then updated to the SIMD lane of the loop.
   */
  group->n_lane = 1;
  node = isl_schedule_get_root(kernel->schedule);
  data.group = group;
  data.kernel = kernel;
  isl_schedule_node_foreach_descendant_top_down(node, &update_group_simd, &data);
  isl_schedule_node_free(node);
  if (max_n_lane % group->n_lane != 0)
  {
    printf("[AutoSA] Error: The data is not aligned to the DRAM port. Abort!\n");
    printf("[AutoSA] Please try to use a SIMD factor as sub-multiples of %d.\n", max_n_lane);
    exit(1);
  }

  /* If data packing is disabled, simply update the data packing factor of 
   * each I/O buffer to the SIMD lanes that are required.
   */
  if (!gen->options->autosa->data_pack)
  {
    for (int i = 0; i < group->io_level; i++)
    {
      struct autosa_io_buffer *buf = group->io_buffers[i];
      buf->n_lane = group->n_lane;
    }
    return isl_stat_ok;
  }

  int cur_n_lane = group->n_lane;
  int status = false;
  /* For L1 buffers, we restrain the fifo widths to be no more than 256 bits 
   * given hardware consideration (on Xilinx). 
   * Specifically, for FIFOs with depth * width > 512bits, HLS will 
   * use BRAM/SRL to implement FIFOs, which could potentially increase 
   * the BRAM/LUT usage by a great scale and cause routing failure.
   * 
   * Furthermore, for L1 buffers reside at the io_L1 level (beside PEs), we 
   * furtehr restrain the FIFO widths to be no more than 64 bits to mitigate 
   * the potential routing congestion.
   */
  /* Parse the data pack settings. */
  sizes = extract_sizes_from_str(gen->ctx, gen->options->autosa->data_pack_sizes);
  //data_pack_ubs = read_data_pack_sizes(sizes, 3);
  data_pack_ubs = read_data_pack_sizes_array(sizes, group->array->name);
  //std::cout << group->array->name << std::endl;
  //std::cout << data_pack_ubs[2] << std::endl;
  if (!data_pack_ubs)
  {
    /* Use the default numbers. */
    data_pack_ubs = isl_alloc_array(gen->ctx, int, 3);
    data_pack_ubs[0] = 8;
    data_pack_ubs[1] = 32;
    data_pack_ubs[2] = 64;
  }

  int cur_max_n_lane;
  for (int i = 0; i < group->io_level; i++)
  {
    struct autosa_io_buffer *buf = group->io_buffers[i];
    if (i == 0)
      cur_max_n_lane = max(group->n_lane, data_pack_ubs[0] / ele_size);
    else if (i > 0 && i < group->io_level - 1)
      cur_max_n_lane = max(group->n_lane, data_pack_ubs[1] / ele_size);
    else
      cur_max_n_lane = max(group->n_lane, data_pack_ubs[2] / ele_size);
    if (buf->tile && group->array->n_index > 0)
    {      
      size = isl_val_copy(buf->tile->bound[group->array->n_index - 1].size);
compute_data_pack:      
      int n_lane = cur_n_lane;
      while (n_lane <= cur_max_n_lane)
      {
        /* The lane should be multiples of SIMD lane. */
        if (n_lane % group->n_lane == 0)
        {
          isl_val *val = isl_val_int_from_si(gen->ctx, n_lane);
          /* The lane should be sub-multiples of the last dim of the array. */
          if (isl_val_is_divisible_by(size, val))
          {
            cur_n_lane = n_lane;
            status = true;
          }
          isl_val_free(val);
        }
        n_lane = n_lane * 2;
      }
      if (status)
      {
        buf->n_lane = cur_n_lane;
      }
      else
      {
        printf("[AutoSA] Error: Cannot find data pack factors as sub-multiples of the last dim of the local array. Abort!\n");
        printf("[AutoSA] Please try to use different tiling factors.\n");
        exit(1);
      }
      isl_val_free(size);      
    } else if (i == group->io_level - 1 && !gen->options->autosa->host_serialize) {
      /* If it is the outermost loop, try to extend the data packing factor again. 
       * If the host serialization is enabled, as there is a re-packing later.
       * We won't do anything here. 
       */
      /* Locate the next buffer. */            
      struct autosa_io_buffer *nxt_buf;
      for (int j = i; j >= 0; j--) {
        nxt_buf = group->io_buffers[j];
        if (nxt_buf->tile) 
          break;                  
      }
      if (nxt_buf->tile) {        
        size = isl_val_copy(nxt_buf->tile->bound[group->array->n_index - 1].size);
        goto compute_data_pack;
      }        
    } else
    {
      buf->n_lane = cur_n_lane;
    }
  }
  isl_union_map_free(sizes);
  free(data_pack_ubs);

  return isl_stat_ok;
}

/* Lift up the L1 I/O buffer between the paralle loops and non-parallel loops
 * in the array loop band.
 * If there is no array loop band. Lift up the L1 I/O buffer above the array mark.
 */
static isl_stat hoist_L1_io_buffer_local_reduce(
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group,
  struct autosa_gen *gen,
  struct autosa_group_data *data)
{
  struct autosa_io_buffer *cur_buffer;
  isl_schedule_node *node, *node_cp;
  int n;

  /* Find the L1 buffer. */
  for (int i = 1; i <= group->io_level; i++) 
  {
    cur_buffer = group->io_buffers[i - 1];
    if (cur_buffer->tile)
      break;
  }

  autosa_array_tile_free(cur_buffer->tile);
  node = isl_schedule_get_root(group->io_schedule);
  node = autosa_tree_move_down_to_io_mark(node, kernel->core, cur_buffer->level);
  node = insert_io_module_ids(gen, kernel, node, group->space_dim, cur_buffer->level);
  node = autosa_tree_move_up_to_array(node);

  if (kernel->array_part_w > 0) {
    int pos = 0;
    node = isl_schedule_node_parent(node);
    n = isl_schedule_node_band_n_member(node);
    for (pos = n - 1; pos >= 0; pos--)
    {
      if (isl_schedule_node_band_member_get_coincident(node, pos))
        break;
    }
    if (pos == n - 1) {
      node = isl_schedule_node_child(node, 0);
    } else {
      node = isl_schedule_node_band_split(node, pos + 1);
      node = isl_schedule_node_child(node, 0);      
    }
  } 
  
  if (group->group_type == AUTOSA_DRAIN_GROUP)
    compute_group_bounds_drain_at_node(kernel, group, node, cur_buffer);
  else if (group->group_type == AUTOSA_IO_GROUP)
    compute_group_bounds_io_at_node(kernel, group, node, cur_buffer);
  autosa_array_ref_group_compute_tiling(cur_buffer->tile, group);
  
  return isl_stat_ok;
}

struct update_int_io_L1_buffer_data {
  struct autosa_array_ref_group *group;  
  struct autosa_kernel *kernel;
  bool inserted;
  bool tile_computed;
  int depth;
};

static __isl_give isl_schedule_node *update_int_io_L1_depth(__isl_take isl_schedule_node *node, void *user)
{
  struct update_int_io_L1_buffer_data *data = (struct update_int_io_L1_buffer_data *)user;
  int under_simd, n;
  struct autosa_array_ref_group *group;
  isl_schedule_node *insert_node = NULL;  
  isl_union_set *domain;
  int is_carried = 0;

  if (data->inserted)
    return node;
  /* Examine if the node is under the SIMD mark */
  under_simd = is_node_under_simd(node);
  if (under_simd)
    return node;
  
  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return node;

  domain = isl_schedule_node_get_domain(node);
  if (isl_union_set_is_empty(domain)) {
    isl_union_set_free(domain);
    return node;
  }
  isl_union_set_free(domain);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  n = isl_schedule_node_band_n_member(node);
  /* Examine if the dependences of the current I/O group are carreid by the current band. */
  group = data->group;
  for (int i = 0; i < n; i++) {
    isl_schedule_node *node_tmp = isl_schedule_node_copy(node);
    if (n > 1) {
      if (i > 0) {
        node_tmp = isl_schedule_node_band_split(node_tmp, i);
        node_tmp = isl_schedule_node_child(node_tmp, 0);
      }
      if (n - i - 1 > 0) {
        node_tmp = isl_schedule_node_band_split(node_tmp, 1);
      }
    }

    for (int j = 0; j < group->n_ref; j++) {
      struct autosa_stmt_access *ref = group->refs[j];
      for (int k = 0; k < ref->n_io_info; k++) {
        struct autosa_io_info *io_info = ref->io_info[k];
        if (io_info->io_type == group->io_type && 
            !isl_vec_cmp(io_info->dir, group->dir)) {
          if (is_dep_carried_by_node(io_info->dep->isl_dep, node_tmp)) {
            ///* Insert the I/O buffer below the current node */
            //insert_node = isl_schedule_node_copy(node_tmp);
            //insert_node = isl_schedule_node_child(insert_node, 0);
            is_carried = 1;
            break;
          }
        }
      }
      if (is_carried)
        break;      
    }

    if (is_carried) {
      insert_node = isl_schedule_node_copy(node_tmp);
      //insert_node = isl_schedule_node_child(insert_node, 0);
      isl_schedule_node_free(node_tmp);
      break;
    }

    isl_schedule_node_free(node_tmp);
  }

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, insert_node, isl_schedule_node_get_ctx(insert_node));
//#endif

  if (insert_node) {
    data->depth = isl_schedule_node_get_schedule_depth(insert_node);
    data->inserted = true;
    isl_schedule_node_free(insert_node);
  }
  
  return node;
}

static __isl_give isl_schedule_node *update_int_io_L1_buffer(
  __isl_take isl_schedule_node *node, void *user)
{
  struct update_int_io_L1_buffer_data *data = (struct update_int_io_L1_buffer_data *)user;
  int under_simd;
  isl_union_set *domain;
  struct autosa_array_ref_group *group;

  ///* Examine if the node is under the SIMD mark */
  //under_simd = is_node_under_simd(node);
  //if (under_simd)
  //  return node;

  if (data->tile_computed)
    return node;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return node;
  
  domain = isl_schedule_node_get_domain(node);
  if (isl_union_set_is_empty(domain)) {
    isl_union_set_free(domain);
    return node;
  }
  isl_union_set_free(domain);

  if (isl_schedule_node_get_schedule_depth(node) < data->depth) {
    /* Check the child node */
    node = isl_schedule_node_child(node, 0);
  }

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  if (isl_schedule_node_get_schedule_depth(node) == data->depth) {
    /* Find the L1 buffer */
    struct autosa_io_buffer *cur_buffer;
    group = data->group;
    for (int i = 1; i < group->io_level; i++) {
      cur_buffer = group->io_buffers[i - 1];
      if (cur_buffer->tile)
        break;
    }

    autosa_array_tile_free(cur_buffer->tile);
    if (group->group_type == AUTOSA_DRAIN_GROUP)
      compute_group_bounds_drain_at_node(data->kernel, group, node, cur_buffer);
    else if (group->group_type == AUTOSA_IO_GROUP)
      compute_group_bounds_io_at_node(data->kernel, group, node, cur_buffer);
    autosa_array_ref_group_compute_tiling(cur_buffer->tile, group);

    data->tile_computed = true;
  }

  return node;
}

//static __isl_give isl_schedule_node *update_int_io_L1_buffer(__isl_take isl_schedule_node *node, void *user)
//{
//  struct update_int_io_L1_buffer_data *data = (struct update_int_io_L1_buffer_data *)user;
//  int under_simd, n;
//  struct autosa_array_ref_group *group;
//  isl_schedule_node *insert_node = NULL;  
//  isl_union_set *domain;
//  int is_carried = 0;
//
//  if (data->inserted)
//    return node;
//  /* Examine if the node is under the SIMD mark */
//  under_simd = is_node_under_simd(node);
//  if (under_simd)
//    return node;
//  
//  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
//    return node;
//
//  domain = isl_schedule_node_get_domain(node);
//  if (isl_union_set_is_empty(domain)) {
//    isl_union_set_free(domain);
//    return node;
//  }
//  isl_union_set_free(domain);
//
//  n = isl_schedule_node_band_n_member(node);
//  /* Examine if the dependences of the current I/O group are carreid by the current band. */
//  group = data->group;
//  for (int i = 0; i < n; i++) {
//    isl_schedule_node *node_tmp = isl_schedule_node_copy(node);
//    if (n > 1) {
//      if (i > 0) {
//        node_tmp = isl_schedule_node_band_split(node_tmp, i);
//        node_tmp = isl_schedule_node_child(node_tmp, 0);
//      }
//      if (n - i - 1 > 0) {
//        node_tmp = isl_schedule_node_band_split(node_tmp, 1);
//      }
//    }
//
//    for (int j = 0; j < group->n_ref; j++) {
//      struct autosa_stmt_access *ref = group->refs[j];
//      for (int k = 0; k < ref->n_io_info; k++) {
//        struct autosa_io_info *io_info = ref->io_info[k];
//        if (io_info->io_type == group->io_type && 
//            !isl_vec_cmp(io_info->dir, group->dir)) {
//          if (is_dep_carried_by_node(io_info->dep->isl_dep, node_tmp)) {
//            ///* Insert the I/O buffer below the current node */
//            //insert_node = isl_schedule_node_copy(node_tmp);
//            //insert_node = isl_schedule_node_child(insert_node, 0);
//            is_carried = 1;
//            break;
//          }
//        }
//      }
//      if (is_carried)
//        break;      
//    }
//
//    if (!is_carried) {
//      insert_node = isl_schedule_node_copy(node_tmp);
//      insert_node = isl_schedule_node_child(insert_node, 0);
//      break;
//    }
//
//    isl_schedule_node_free(node_tmp);
//  }
//
//  if (insert_node) {      
////#ifdef _DEBUG
////    DBGSCHDNODE(stdout, insert_node, isl_schedule_node_get_ctx(insert_node));
////#endif
//
//    /* Find the L1 buffer */
//    struct autosa_io_buffer *cur_buffer;
//    for (int i = 1; i < group->io_level; i++) {
//      cur_buffer = group->io_buffers[i - 1];
//      if (cur_buffer->tile)
//        break;
//    }
//    autosa_array_tile_free(cur_buffer->tile);
//    if (group->group_type == AUTOSA_DRAIN_GROUP)
//      compute_group_bounds_drain_at_node(data->kernel, group, insert_node, cur_buffer);
//    else if (group->group_type == AUTOSA_IO_GROUP)
//      compute_group_bounds_io_at_node(data->kernel, group, insert_node, cur_buffer);
//    autosa_array_ref_group_compute_tiling(cur_buffer->tile, group);
//
////#ifdef _DEBUG    
////    printf("%d\n", cur_buffer->tile->depth);
////#endif
//
//    isl_schedule_node_free(insert_node);
//    data->inserted = true;
//  }
//  
//  return node;
//}

static __isl_give isl_schedule_node *insert_io_L1_mark(
  __isl_take isl_schedule_node *node, void *user)
{
  int *depth = (int *)user;

  if (isl_schedule_node_get_schedule_depth(node) == *depth && 
      isl_schedule_node_get_type(node) == isl_schedule_node_band) 
  {
    isl_id *id;
    id = isl_id_alloc(isl_schedule_node_get_ctx(node), "io_L1", NULL);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_insert_mark(node, id);
    node = isl_schedule_node_parent(node);
  }

  return node;
}

/* This function generates a new io schedule when the L1 IO buffer is lowered.
 * Specifically, the L1 io band node with its mark node will be sunk to schedule
 * depth of (depth - 1). 
 * This function assume that the entire schedule tree is fully permutable. 
 * The legality should be checked before calling this function.
 */
static __isl_give isl_schedule *generate_io_L1_lower_schedule(
  __isl_keep isl_schedule *schedule,
  struct autosa_kernel *kernel,
  int depth)
{
  isl_schedule_node *node;
  isl_schedule *new_schedule;

  new_schedule = isl_schedule_dup(schedule);
  node = isl_schedule_get_root(new_schedule);
  isl_schedule_free(new_schedule);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  node = autosa_tree_move_down_to_io_mark(node, kernel->core, 1);
  node = isl_schedule_node_delete(node);
  node = isl_schedule_node_parent(node);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif
  /* Sink the L1 band to (depth - 1) */
  node = autosa_node_sink_to_depth(node, depth - 1);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif
  /* Insert the io_L1 mark */
  int depth_inc = depth - 1;
  node = isl_schedule_node_map_descendant_bottom_up(node, &insert_io_L1_mark, &depth_inc);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  new_schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);
  return new_schedule;
}

/* This function tries to lower the L1 buffer for the interior I/O module (for external array)
 * to help reduce the memory resource usage.
 * 
 * It first checks if the I/O group is with the interior I/O, and if the array is
 * an external array.
 * If so, one L1 I/O buffer is allocated by default. 
 * Next, it examines if there is at least one parallel loop (independent of the 
 * reuse dependence) from innermost. L1 buffer will be lowered to the boundary
 * between the non-parallel and parallel loops.
 */
static isl_stat lower_int_io_L1_buffer(
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group,
  struct autosa_gen *gen)
{
  if (!(group->io_type == AUTOSA_INT_IO && group->local_array->array_type == AUTOSA_EXT_ARRAY))
    return isl_stat_ok;

  isl_schedule_node *node;
  struct update_int_io_L1_buffer_data data = {group, kernel, false, false, -1};

  node = isl_schedule_get_root(group->io_schedule);
  /* Insert the domain filter for the current I/O group */
  node = autosa_tree_move_down_to_kernel(node);
  /* This function only works for copy-in modules */
  node = insert_io_group_domain(node, group, kernel, gen, 1);  

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, gen->ctx);
//  //printf("%s\n", group->array->name);
//#endif
  /* Update the depth to insert the buffer */
  node = isl_schedule_node_map_descendant_bottom_up(node, &update_int_io_L1_depth, &data);
  isl_schedule_node_free(node);
  
  if (data.inserted) {
    /* Generate the new I/O schedule */
    group->io_L1_lower_schedule = 
      generate_io_L1_lower_schedule(group->io_schedule, kernel, data.depth);
    /* Update the L1 buffer */
    node = isl_schedule_get_root(group->io_L1_lower_schedule);    
    node = isl_schedule_node_map_descendant_bottom_up(node, &update_int_io_L1_buffer, &data);
    isl_schedule_node_free(node);
  }

  return isl_stat_ok;
}

/* This function is used when lower IO L1 buffer is enabled.
 * An extra second-level buffer is inserted to increase the effective DRAM BW.
 */
static isl_stat insert_L2_io_buffer(
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group,
  struct autosa_gen *gen
){
  if (!(group->io_type == AUTOSA_INT_IO && group->local_array->array_type == AUTOSA_EXT_ARRAY))
    return isl_stat_ok;

  isl_schedule_node *node;
  struct autosa_io_buffer *buffer;

  node = isl_schedule_get_root(group->io_L1_lower_schedule);
  node = autosa_tree_move_down_to_array(node, kernel->core);
  buffer = group->io_buffers[group->io_level - 1];
  if (group->group_type == AUTOSA_DRAIN_GROUP)
    compute_group_bounds_drain_at_node(kernel, group, node, buffer);
  else if (group->group_type == AUTOSA_IO_GROUP)
    compute_group_bounds_io_at_node(kernel, group, node, buffer);

  autosa_array_ref_group_compute_tiling(buffer->tile, group);
  isl_schedule_node_free(node);

  return isl_stat_ok;
}

/* This function tries to hoist the L2 I/O buffer to increase the memory 
 * coelescing. 
 * 
 * Specifically, we will start from the original position where the L2 buffer
 * in inserted. We will compare if the last dimension of the L2 buffer is 
 * larger than the last dimension of the L1 buffer.
 * If not, we will try to hoist the L2 buffer until the last dimension is increased.
 * 
 * If we could not increase the last dimension, we will reallocate the L2 buffer
 * at the outermost I/O level. And try to hoist up the buffer if the local 
 * buffer size is irrelevant to the outer loop. This helps save the communication.
 * 
 * If the buffer location is not changed, we will last check if the last dimension
 * of the array can be packed as multiples of 512 bits. Since the maximal DRAM
 * port width is 512 bits.
 * This is helpful because on Xilinx FPGAs, we limit the maximal on-chip fifo 
 * width to 256 bits. Repacking the data to 512 bits at the L2 I/O buffer 
 * could help improve the effective DRAM bandwidth.
 *
 * If it is not a multiple of 512 bits, there is no benefit overall to generate
 * L2 I/O buffers. In this case, we will free up the L2 I/O buffer. 
 * No L2 I/O buffer is generated.
 */
static isl_stat hoist_L2_io_buffer(
  struct autosa_kernel *kernel,
  struct autosa_array_ref_group *group, 
  struct autosa_gen *gen,
  struct autosa_group_data *data)
{
  struct autosa_io_buffer *cur_buffer, *nxt_buffer;
  int io_level = group->io_level;
  bool is_last_dim_equal = false;
  isl_val *cur_last_dim, *nxt_last_dim;
  isl_schedule_node *node, *node_cp;
  int i, n;
  int old_depth, new_depth;

  cur_buffer = group->io_buffers[io_level - 1];
  for (int i = io_level - 1; i >= 1; i--)
  {
    nxt_buffer = group->io_buffers[i - 1];
    if (nxt_buffer->tile)
      break;
  }

  /* Compare if the last dimension of the current buffer
   * and the next buffer equals.
   */
  cur_last_dim = cur_buffer->tile->bound[cur_buffer->tile->n - 1].size;
  nxt_last_dim = nxt_buffer->tile->bound[nxt_buffer->tile->n - 1].size;
  is_last_dim_equal = isl_val_eq(cur_last_dim, nxt_last_dim);

  if (is_last_dim_equal)
  {
    /* Try to hoist the io buffer until the last dimenison is increased. */
    autosa_array_tile_free(cur_buffer->tile);
    node = isl_schedule_get_root(group->io_schedule);
    /* Insert the filter ids. */
    node = autosa_tree_move_down_to_io_mark(node, kernel->core, io_level);
    node = insert_io_module_ids(gen, kernel, node, group->space_dim, io_level);    
    node = autosa_tree_move_up_to_array(node);

    //node = autosa_tree_move_down_to_array(node, kernel->core);
    node = isl_schedule_node_parent(node);
    n = isl_schedule_node_band_n_member(node);
    for (i = n - 1; i > 0; i--)
    {
      node_cp = isl_schedule_node_copy(node);
      node_cp = isl_schedule_node_band_split(node_cp, i);
      node_cp = isl_schedule_node_child(node_cp, 0);
      if (group->group_type == AUTOSA_DRAIN_GROUP)
        compute_group_bounds_drain_at_node(kernel, group, node_cp, cur_buffer);
      else if (group->group_type == AUTOSA_IO_GROUP)
        compute_group_bounds_io_at_node(kernel, group, node_cp, cur_buffer);
      autosa_array_ref_group_compute_tiling(cur_buffer->tile, group);
      /* Test if the last dim is increased. */
      cur_last_dim = cur_buffer->tile->bound[cur_buffer->tile->n - 1].size;
      /* #ifdef _DEBUG
      pd = isl_printer_to_file(gen->ctx, stdout);
      pd = isl_printer_print_val(pd, cur_buffer->tile->bound[0].size);
      pd = isl_printer_end_line(pd);
      pd = isl_printer_free(pd);      
#endif */
      is_last_dim_equal = isl_val_eq(cur_last_dim, nxt_last_dim);
      isl_schedule_node_free(node_cp);
      if (!is_last_dim_equal)
      {
        break;
      }
      autosa_array_tile_free(cur_buffer->tile);
    }
    if (i == 0)
    {
      /* In this case, none of the second level array part loops helps 
       * increase the burst length. We will allocate the buffer again 
       * at the innermost array_L2 loop and try to hoist up the buffer 
       * to save the communication. 
       */
      int old_depth, new_depth;
      node = isl_schedule_node_child(node, 0);
      if (group->group_type == AUTOSA_DRAIN_GROUP)
        compute_group_bounds_drain_at_node(kernel, group, node, cur_buffer);
      else if (group->group_type == AUTOSA_IO_GROUP)
        compute_group_bounds_io_at_node(kernel, group, node, cur_buffer);
      autosa_array_ref_group_compute_tiling(cur_buffer->tile, group);
    }
    isl_schedule_node_free(node);
  }
  /* Test if the buffer position could be further hoisted. */
  old_depth = cur_buffer->tile->depth;
  tile_set_depth(data, cur_buffer->tile);
  new_depth = cur_buffer->tile->depth;
  if (is_last_dim_equal && new_depth == old_depth)
  {
    /* In this case, the buffer couldn't be hosited up, and it doesn't 
     * increase the burst length. 
     * We will test if the last dimension is a multiple of 512 bits (64 bytes).
     */
    cur_last_dim = cur_buffer->tile->bound[cur_buffer->tile->n - 1].size;
    long dim_val = isl_val_get_num_si(cur_last_dim);
    if ((dim_val * group->array->size) % 64 != 0)
    {
      /*There is no benefit to generate the 
       * second-level buffer. We will free up the tile.
       */
      autosa_array_tile_free(cur_buffer->tile);
      cur_buffer->tile = NULL;
    }
  }
  else
  {
    if (new_depth != old_depth)
    {
      isl_multi_aff_free(cur_buffer->tile->tiling);
      autosa_array_ref_group_compute_tiling(cur_buffer->tile, group);
    }
  }

  return isl_stat_ok;
}

/* Return the prefix I/O schedule at io_level "level". */
static __isl_give isl_union_map *get_io_schedule_at_level(
    __isl_keep isl_schedule *sched, int level)
{
  isl_schedule_node *node;
  struct autosa_kernel *kernel;
  isl_id *id;
  isl_union_map *io_sched;

  node = isl_schedule_get_root(sched);
  node = autosa_tree_move_down_to_kernel(node);
  id = isl_schedule_node_mark_get_id(node);
  kernel = (struct autosa_kernel *)isl_id_get_user(id);
  isl_id_free(id);
  node = autosa_tree_move_down_to_io_mark(node, kernel->core, level);
  io_sched = prefix_with_equalities(node);
  io_sched = expand(io_sched, kernel->contraction);
  isl_schedule_node_free(node);

  return io_sched;
}

/* Map the domain of "access" to the outer data->local_depth
 * schedule dimensions.   
 */
static __isl_give isl_map *local_access_io(struct autosa_array_ref_group *group,
                                           __isl_keep isl_union_map *access, struct autosa_group_data *data)
{
  isl_union_map *local;
  local = isl_union_map_copy(access);

  if (group->io_type == AUTOSA_EXT_IO)
  {
    /* Group at the IO_L2 level */
    isl_union_map *new_sched = get_io_schedule_at_level(group->io_schedule, 2);
    local = isl_union_map_apply_domain(local,
                                       new_sched);
  }
  else if (group->io_type == AUTOSA_INT_IO)
  {
    /* Group at the IO_L1 level. */
    isl_union_map *new_sched = get_io_schedule_at_level(group->io_schedule, 1);
    local = isl_union_map_apply_domain(local,
                                       new_sched);
  }
  return isl_map_from_union_map(local);
}

/* Compute the local memory tiles for the array reference group "group"
 * of array "array". Return isl_stat_ok on success and isl_stat_error on error.
 *
 * If the array is a read-only scalar or if the user requested not to use 
 * local emory, then we do not need to do anything.
 *
 * For interior I/O group, the tiling is computed at the io_L1 level.
 * For exteriro I/O group, the tiling is computed at the io_L2 level.
 */
static isl_stat compute_group_bounds_core_io(struct autosa_kernel *kernel,
                                             struct autosa_array_ref_group *group,
                                             struct autosa_group_data *data)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. 
   * TODO: Overapproximation */
  access = autosa_array_ref_group_access_relation(group, 1, 1);
  /* Create local tile */
  if (use_local)
  {
    /* Create a tile. */
    group->local_tile = autosa_array_tile_create(ctx,
                                                 group->array->n_index);
    /* Map the domain to the outer scheduling dimensions. */
    acc = local_access_io(group, access, data);
    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, group->local_tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      group->local_tile =
          autosa_array_tile_free(group->local_tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Compute the local memory tiles for the array
 * reference group "group" of array "array" and set the tile depth.
 * Return 0 on success and -1 on error.
 */
static int compute_group_bounds_io(struct autosa_kernel *kernel,
                                   struct autosa_array_ref_group *group,
                                   struct autosa_group_data *data)
{
  if (!group)
    return -1;
  if (compute_group_bounds_core_io(kernel, group, data) < 0)
    return -1;

  return 0;
}

/* Set array->n_group and array->groups to n and groups.
 *
 * Additionally, set the "nr" field of each group.
 */
static void set_array_groups_io(struct autosa_local_array_info *array,
                                int n, struct autosa_array_ref_group **groups)
{
  int i;

  array->n_io_group = n;
  array->io_groups = groups;

  for (i = 0; i < n; ++i)
    groups[i]->nr = i;
}

/* Group array references together if they share the I/O modules.
 * Return -1 on error.
 *
 * Two array references are grouped together if they share:
 * - I/O direction "dir" 
 * - I/O type "io_type"
 * Besides, they should all under the SIMD loop or not.
 *
 * For exterior I/O pair, calculate the group tiling at the io_L2 level.
 * For interior I/O pair, calculate the group tiling at the io_L1 level.
 */
static int group_array_references_io(struct autosa_kernel *kernel,
                                     struct autosa_local_array_info *local, struct autosa_group_data *data)
{
  int i, j;
  int n;
  isl_ctx *ctx = isl_union_map_get_ctx(data->pe_sched);
  struct autosa_array_ref_group **groups;

  /* Count the total number of groups. 
   * We first populate the groups with the number of total communication pairs 
   * (io_info).
   * We only consider io_info with RAR/RAW for IO groups.
   */
  n = 0;
  for (i = 0; i < local->array->n_ref; i++)
  {    
    struct autosa_stmt_access *ref = local->array->refs[i];
//#ifdef _DEBUG    
//    if (!strcmp(local->array->name, "U_tmp")) {
//      DBGMAP(stdout, ref->access, ctx);
//      printf("n_io_info: %d\n", ref->n_io_info);
//      for (int j = 0; j < ref->n_io_info; j++) {
//        struct autosa_io_info *io_info = ref->io_info[j];
//        DBGBMAP(stdout, io_info->dep->isl_dep, ctx);
//        printf("%d\n", io_info->io_type);
//      }
//    }
//#endif
    for (j = 0; j < ref->n_io_info; j++) {
      struct autosa_io_info *io_info = ref->io_info[j];
      if (io_info->dep->type == AUTOSA_DEP_RAW || io_info->dep->type == AUTOSA_DEP_RAR)
        n++;      
    }    
  }

  groups = (struct autosa_array_ref_group **)calloc(n,
                                                    sizeof(struct autosa_array_ref_group *));
  if (!groups)
    return -1;

  /* Populate the groups. */
  n = populate_array_references_io(local, groups, data);

  /* Group references that share the same I/O direction and I/O type. */
  n = group_share_io(kernel, n, groups, data);

  /* Perform interior I/O elimination. */
  for (i = 0; i < n; ++i)
  {
    autosa_interior_io_eliminate(kernel, groups[i], data->gen, data);
  }

  set_array_groups_io(local, n, groups);

  return 0;
}

/* Internal struct usedd for extract_access_waw_domain */
struct extract_access_waw_domain_data
{
  struct autosa_stmt_access *ref;
  isl_set *drain_domain;
};

/* Check if the access is associated with the waw,
 * if so, calculate the write-out (drain) domain as:
 * acc domain - waw src_domain
 */
static void extract_access_waw_domain(__isl_keep isl_basic_map *dep, void *user)
{
  isl_space *space;
  isl_space *src_space;
  isl_id *src_id;
  isl_set *src_domain;
  struct extract_access_waw_domain_data *data =
      (struct extract_access_waw_domain_data *)(user);
  isl_basic_map *bmap;
  isl_map *map;

  space = isl_basic_map_get_space(dep);
  src_space = isl_space_unwrap(isl_space_domain(space));
  src_id = isl_space_get_tuple_id(src_space, isl_dim_out);
  isl_space_free(src_space);

  if (src_id != data->ref->ref_id)
  {
    isl_id_free(src_id);
    return;
  }
  isl_id_free(src_id);

  bmap = isl_basic_map_copy(dep);
  map = isl_map_from_basic_map(bmap);
  map = isl_map_factor_domain(map);
  src_domain = isl_map_domain(map);

  data->drain_domain = isl_set_subtract(data->drain_domain, src_domain);

  return;
}

/* Extract the write-out domain for the given access. */
static isl_bool extract_access_waw_domain_wrap(__isl_keep isl_map *map, void *user)
{
  isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(map);
  for (int i = 0; i < isl_map_n_basic_map(map); i++)
  {
    isl_basic_map *dep = isl_basic_map_list_get_basic_map(bmap_list, i);
    extract_access_waw_domain(dep, user);
    isl_basic_map_free(dep);
  }
  isl_basic_map_list_free(bmap_list);
  return isl_bool_true;
}

/* Compute the local memory tiles for the array reference group "group"
 * of array "array". Return isl_stat_ok on success and isl_stat_error on error.
 *
 * The tiling is computed at the PE level.
 */
static isl_stat compute_group_bounds_core_drain(struct autosa_kernel *kernel,
                                                struct autosa_array_ref_group *group, struct autosa_group_data *data)
{
  isl_ctx *ctx = isl_space_get_ctx(group->array->space);
  int use_local = kernel->options->autosa->use_local_memory;
  isl_stat r = isl_stat_ok;
  isl_union_map *access;
  isl_map *acc;
  isl_bool ok;

  if (!use_local)
    return isl_stat_ok;
  if (autosa_array_is_read_only_scalar(group->array))
    return isl_stat_ok;
  if (!group->exact_write)
    return isl_stat_ok;
  if (group->slice)
    return isl_stat_ok;

  /* Collect all accesses in the group. */
  /* This is overapproximated. */
  access = autosa_array_ref_group_access_relation(group, 0, 1);
  /* Create local tile */
  if (use_local)
  {
    /* Create a tile. */
    group->local_tile = autosa_array_tile_create(ctx,
                                                 group->array->n_index);
    /* Map the domain to the outer scheduling dimensions */
    acc = local_access_io(group, access, data);
    /* Collect the shift and scale factors of the tile. */
    ok = can_tile(acc, group->local_tile);
    if (ok < 0)
      r = isl_stat_error;
    else if (!ok)
      group->local_tile =
          autosa_array_tile_free(group->local_tile);
    isl_map_free(acc);
  }

  if (r < 0)
  {
    isl_union_map_free(access);
    return r;
  }

  isl_union_map_free(access);
  return isl_stat_ok;
}

/* Compute the local memory tiles for the array
 * reference group "group" of array "array" and set the tile depth.
 * Return 0 on success and -1 on error.
 */
static int compute_group_bounds_drain(struct autosa_kernel *kernel,
                                      struct autosa_array_ref_group *group, struct autosa_group_data *data)
{
  if (!group)
    return -1;
  if (compute_group_bounds_core_drain(kernel, group, data) < 0)
    return -1;

  return 0;
}

/* Group array references together if they are associated with WAW dep and need 
 * to be drained out.
 * Return -1 on error.
 *
 * Calculate the group tiling at the PE level.
 */
static int group_array_references_drain(struct autosa_kernel *kernel,
                                        struct autosa_local_array_info *local, struct autosa_group_data *data)
{
  if (local->array->local)
    return 0;

  int i, j;
  int n;
  isl_ctx *ctx = isl_union_map_get_ctx(data->pe_sched);
  struct autosa_array_ref_group **groups = NULL;
  isl_union_map *dep_waw = kernel->scop->tagged_dep_waw;

  /* Populate the groups. */
  n = 0;
  for (int i = 0; i < local->array->n_ref; ++i)
  {
    struct autosa_stmt_access *access = local->array->refs[i];
    if (access->read)
      continue;
    isl_set *domain = isl_map_domain(isl_map_copy(access->access));
    isl_set *access_domain = isl_union_set_extract_set(
        kernel->expanded_domain,
        isl_set_get_space(domain));
    isl_set_free(domain);
    struct extract_access_waw_domain_data drain_data = {access, access_domain};
    isl_union_map_every_map(dep_waw, &extract_access_waw_domain_wrap, &drain_data);
    if (!isl_set_is_empty(drain_data.drain_domain))
    {
      isl_map *map;
      isl_union_map *umap;

      map = isl_map_copy(access->access);
      umap = isl_union_map_from_map(map);
      umap = isl_union_map_apply_domain(umap,
                                        isl_union_map_copy(data->pe_sched));

      map = isl_map_from_union_map(umap);
      map = isl_map_detect_equalities(map);

      /* Add this access relation to the group. */
      struct autosa_array_ref_group *group =
          isl_calloc_type(ctx, struct autosa_array_ref_group);
      if (!group)
      {
        isl_map_free(map);
        isl_set_free(drain_data.drain_domain);
        return -1;
      }

      group->local_array = local;
      group->array = local->array;
      group->access = map;
      group->write = access->write;
      group->exact_write = access->exact_write;
      group->slice = access->n_index < local->array->n_index;
      group->refs = &local->array->refs[i];
      group->n_ref = 1;
      group->io_type = AUTOSA_INT_IO;
      group->dir = isl_vec_zero(ctx, kernel->n_sa_dim);
      group->old_dir = isl_vec_zero(ctx, kernel->n_sa_dim);
      /* Perform interior I/O elimination by default. */
      if (kernel->options->autosa->int_io_dir == 0)
        group->dir = isl_vec_set_element_si(group->dir, 0, 1);
      else
        group->dir = isl_vec_set_element_si(group->dir, isl_vec_size(group->dir) - 1, 1);
      group->group_type = AUTOSA_DRAIN_GROUP;
      group->pe_io_dir = IO_OUT;
      group->array_io_dir = IO_OUT;
      group->io_pe_expr = NULL;
      group->io_L1_pe_expr = NULL;
      group->n_io_buffer = 0;
      group->io_buffers = NULL;
      group->copy_schedule = NULL;
      group->pe_tile = NULL;
      group->n_mem_ports = 1;

      groups = (struct autosa_array_ref_group **)realloc(groups, (++n) *
                                                                     sizeof(struct autosa_array_ref_group *));
      groups[n - 1] = group;
    }
    isl_set_free(drain_data.drain_domain);
  }

  /* Join all referneces together. */
  for (i = 1; i < n; ++i)
  {
    groups[0] = join_groups_and_free(groups[0], groups[i]);
  }
  if (n > 1)
    n = 1;

  /* Set the group. */
  if (n > 0)
  {
    groups[0]->nr = 0;
    local->drain_group = groups[0];
  }
  else
  {
    local->drain_group = NULL;
  }
  free(groups);

  return 0;
}

static int gcd(int n1, int n2)
{
  while (n1 != n2)
  {
    if (n1 > n2)
      n1 -= n2;
    else
      n2 -= n1;
  }

  return n1;
}

/* Compute a tiling for all the array reference groups in "kernel".
 */
static void compute_group_tilings_pe(struct autosa_kernel *kernel)
{
  int i, j;

  for (i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];

    for (j = 0; j < array->n_pe_group; ++j)
      autosa_array_ref_group_compute_tiling(NULL, array->pe_groups[j]);
  }
}

/* Compute a tiling for all the array reference groups in "kernel".
 */
static void compute_group_tilings_io(struct autosa_kernel *kernel)
{
  int i, j;

  for (i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];

    for (j = 0; j < array->n_io_group; ++j)
      autosa_array_ref_group_compute_tiling(NULL, array->io_groups[j]);
  }
}

/* Compute a tiling for all the array reference groups in "kernel".
 */
static void compute_group_tilings_drain(struct autosa_kernel *kernel)
{
  int i, j;

  for (i = 0; i < kernel->n_array; ++i)
  {
    struct autosa_local_array_info *array = &kernel->array[i];
    if (!array->drain_group)
      continue;
    autosa_array_ref_group_compute_tiling(NULL, array->drain_group);
  }
}

/* Update the I/O schedules by I/O module clustering. */
static isl_stat autosa_io_clustering(struct autosa_kernel *kernel,
                                     struct autosa_gen *gen, struct autosa_group_data *data)
{
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local = &kernel->array[i];
    for (int j = 0; j < local->n_io_group; j++)
    {
      compute_io_group_schedule(kernel, local->io_groups[j], gen);
    }
    if (local->drain_group)
    {
      compute_io_group_schedule(kernel, local->drain_group, gen);
    }
  }
  return isl_stat_ok;
}

/* Allocate I/O buffers inside I/O modules. */
static isl_stat autosa_io_buffer_allocate(struct autosa_kernel *kernel,
                                          struct autosa_gen *gen, struct autosa_group_data *data)
{
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local = &kernel->array[i];
    for (int j = 0; j < local->n_io_group; j++)
    {
      //if (local->io_groups[j]->copy_in || local->io_groups[j]->copy_out) {
        compute_io_group_buffer(kernel, local->io_groups[j], gen);      
        if (gen->options->autosa->two_level_buffer)
        {
          /* Seek the opportunity to hoist up the L2 I/O buffers. */
          hoist_L2_io_buffer(kernel, local->io_groups[j], gen, data);
        }      
        if (gen->options->autosa->local_reduce && local->io_groups[j]->attached_drain_group)
        {
          if (gen->options->autosa->two_level_buffer) {
            /* At present, two-level buffer and local reduce can be enabled at the same time.
             */
            throw std::runtime_error("[AutoSA] Error: Two-level buffer and local reduce can't be used at the same time.");
          }        
        }
        if (gen->options->autosa->lower_int_io_L1_buffer) {
          /* Lower the L1 buffer for interior I/O module if possible. */
          lower_int_io_L1_buffer(kernel, local->io_groups[j], gen);
          /* Enable the second-level buffer for this array */
          insert_L2_io_buffer(kernel, local->io_groups[j], gen);
          /* Seek the opportunity to hoist up the L2 I/O buffers. */
          //hoist_L2_io_buffer(kernel, local->io_groups[j], gen, data);
        }
      //}
    }
    if (local->drain_group)
    {      
      compute_io_group_buffer(kernel, local->drain_group, gen);
      if (gen->options->autosa->two_level_buffer)
      {
        hoist_L2_io_buffer(kernel, local->drain_group, gen, data);
      }
    }
  }
  return isl_stat_ok;
}

/* Compute data packing factors. */
static isl_stat autosa_io_data_pack(struct autosa_kernel *kernel,
                                    struct autosa_gen *gen, struct autosa_group_data *data)
{
  /* Initalize the IO buffer */
  for (int i = 0; i < kernel->n_array; i++) {
    struct autosa_local_array_info *local = &kernel->array[i];
    for (int j = 0; j < local->n_io_group; j++) {
      struct autosa_array_ref_group *group = local->io_groups[j];
      //if (group->copy_in || group->copy_out) {
        for (int k = 0; k < group->io_level; k++) {
          struct autosa_io_buffer *buf = group->io_buffers[k];
          buf->sparse = 0;
          buf->vec_len = 0;        
          buf->serialize = (gen->options->autosa->host_serialize == 1)? 1 : 0;
        }      
      //}
    }
    if (local->drain_group) {
      struct autosa_array_ref_group *group = local->drain_group;
      for (int k = 0; k < group->io_level; k++) {
        struct autosa_io_buffer *buf = group->io_buffers[k];
        buf->sparse = 0;
        buf->vec_len = 0;        
        buf->serialize = (gen->options->autosa->host_serialize == 1)? 1 : 0;
      }      
    }
  }

  for (int i = 0; i < kernel->n_array; i++) {
    struct autosa_local_array_info *local = &kernel->array[i];
    for (int j = 0; j < local->n_io_group; j++) {
      //if (local->io_groups[j]->copy_in || local->io_groups[j]->copy_out) {
        if (local->is_sparse)
          compute_io_group_data_pack_sparse(kernel, local->io_groups[j], gen, -1);
        else
          compute_io_group_data_pack(kernel, local->io_groups[j], gen, -1);
      //}
    }
    if (local->drain_group) {
      if (local->is_sparse)
        compute_io_group_data_pack_sparse(kernel, local->drain_group, gen, -1);
      else
        compute_io_group_data_pack(kernel, local->drain_group, gen, -1);
    }
  }
  return isl_stat_ok;
}

/* Group references of all arrays in "kernel".
 * Each array is associated with three types of groups:
 * PE group: Assign the local buffers inside PEs.
 * I/O group: Assign the I/O modules for transferring data between
 *   PEs and the external memory
 * Drain group: Assign the I/O modules for transferring out the results from
 *   PEs to the external memory.
 */
isl_stat sa_io_construct_optimize(struct autosa_kernel *kernel, struct autosa_gen *gen)
{
  int r = 0;
  struct autosa_group_data data;
  isl_schedule_node *node;
  isl_union_pw_multi_aff *contraction;

  node = isl_schedule_get_root(kernel->schedule);
  node = autosa_tree_move_down_to_kernel(node);

  /* Set autosa_group_data. */
  data.scop = kernel->prog->scop;
  data.gen = gen;
  data.kernel_depth = isl_schedule_node_get_schedule_depth(node);
  data.host_sched = isl_schedule_node_get_prefix_schedule_relation(node);
  node = autosa_tree_move_down_to_pe(node, kernel->core);
  data.pe_depth = isl_schedule_node_get_schedule_depth(node);
  data.pe_sched = prefix_with_equalities(node);
  contraction = isl_union_pw_multi_aff_copy(kernel->contraction);
  data.host_sched = expand(data.host_sched, contraction);
  data.copy_sched = isl_union_map_copy(data.pe_sched);
  data.pe_sched = expand(data.pe_sched, contraction);
  isl_union_pw_multi_aff_free(contraction);
  data.full_sched = isl_union_map_copy(data.pe_sched);
  data.full_sched = isl_union_map_flat_range_product(data.full_sched,
                                                     isl_schedule_node_get_subtree_schedule_union_map(node));
  data.schedule = kernel->schedule;

  /* Create the default array reference groups (PPCG heritage). */
  for (int i = 0; i < kernel->n_array; i++)
  {
    r = group_array_references_default(kernel, &kernel->array[i], &data);
    if (r < 0)
      break;
  }

  /* Group the array references for the PE.
   * These groups will be used for allocate local buffers inside PEs.
   */
  for (int i = 0; i < kernel->n_array; i++)
  {
    r = group_array_references_pe(kernel, &kernel->array[i], &data);
    if (r < 0)
      break;
  }

  /* Group the array references for the I/O modules. */
  for (int i = 0; i < kernel->n_array; i++)
  {
    r = group_array_references_io(kernel, &kernel->array[i], &data);
    if (r < 0)
      break;
  }

  /* Group the array references for the drain data */
  for (int i = 0; i < kernel->n_array; i++)
  {
    r = group_array_references_drain(kernel, &kernel->array[i], &data);
    if (r < 0)
      break;
  }

  /* Perform I/O Optimization */  
  /* I/O module clustering */
  autosa_io_clustering(kernel, gen, &data);

  /* Local reduce */
  if (gen->options->autosa->local_reduce) 
  {
    printf("[AutoSA] Warning: Local reduction is enabled. The legality should be guaranteed by users.\n");
    // TODO: In the future, add a legality check for this optimization.
    /* Check if there is one exterior I/O group.
     * Then attach the drain group to this I/O group and set the drain group to NULL.
     */
    for (int i = 0; i < kernel->n_array; i++)
    {
      int ext_group_cnt = 0;
      int group_id = -1;
      struct autosa_local_array_info *local = &kernel->array[i];
      for (int j = 0; j < local->n_io_group; j++)
      {
        if (local->io_groups[j]->io_type == AUTOSA_EXT_IO &&
            local->array_type == AUTOSA_INT_ARRAY) {
          ext_group_cnt++;
          group_id = j;
        }
      }
      if (local->drain_group && ext_group_cnt == 1) {
        local->io_groups[group_id]->attached_drain_group = local->drain_group;
        local->io_groups[group_id]->copy_out = 1;
        local->drain_group = NULL;
        local->io_groups[group_id]->copy_in = 0;
        local->n_mem_ports = 1;
      }    
    }
  }

  if (gen->options->autosa->host_serialize)
  {
    /* Check if there is only one I/O/drain group for each array.
     * Otherwise, we will disable the host serialize.
     */
    for (int i = 0; i < kernel->n_array; i++)
    {
      int module_cnt = 0;
      struct autosa_local_array_info *local = &kernel->array[i];
      for (int j = 0; j < local->n_io_group; j++)
      {
        if (local->io_groups[j]->copy_in)
          module_cnt++;
        if (local->io_groups[j]->copy_out)
          module_cnt++;
      }
      if (local->drain_group)
      {
        if (local->drain_group->copy_out)
          module_cnt++;
      }
      if (module_cnt > 1) {
        gen->options->autosa->host_serialize = 0;
        // TODO: In the future, we should separate this check for each array.
        printf("[AutoSA] Warning: More than one IO/drain group found for array: %s. Host data serialization is disabled.\n", local->array->name);
      }
    }
  }
  if (gen->options->autosa->host_serialize)
  {
    /* Disable the two-level buffering when host data serialization is enabled. */
    gen->options->autosa->two_level_buffer = 0;
    printf("[AutoSA] Warning: Two-level buffering is disabled because host data serialization is enabled.\n");
  }
  if (gen->options->autosa->host_serialize && gen->options->autosa->hbm)
  {
    printf("[AutoSA] Error: Host serialization and HBM can't be enabled at the same time!\n");
    exit(1);
  }

  /* Print the IO grouping information */
  print_io_grouping_info(stdout, kernel);

  /* I/O buffer allocation */
  autosa_io_buffer_allocate(kernel, gen, &data);
  /* I/O module data pack */
  autosa_io_data_pack(kernel, gen, &data);

  /* Since different I/O groups of the same array will access the DRAM with the 
   * same global array pointer. We will need to make sure the outermost 
   * data packing factors are the same across these groups.
   * Here we will examine if they are the same.
   * If not, we will need to repack to the I/O groups to make them equal. 
   */
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    int n_lane = -1;
    bool repack = false;
    for (int j = 0; j < local_array->n_io_group; j++)
    {      
      struct autosa_array_ref_group *group = local_array->io_groups[j];
      if (!(group->copy_in || group->copy_out))
        continue;
      int cur_n_lane = group->io_buffers[group->n_io_buffer - 1]->n_lane;
      if (n_lane == -1)
        n_lane = cur_n_lane;
      else
        n_lane = gcd(n_lane, cur_n_lane);
      if (n_lane != cur_n_lane)
      {
        repack = true;
      }
    }
    if (local_array->drain_group)
    {      
      struct autosa_array_ref_group *group = local_array->drain_group;
      int cur_n_lane = group->io_buffers[group->n_io_buffer - 1]->n_lane;
      if (n_lane == -1)
        n_lane = cur_n_lane;
      else
        n_lane = gcd(n_lane, cur_n_lane);
      if (n_lane != cur_n_lane)
      {
        repack = true;
      }
    }

    if (repack)
    {
      /* We need to repack the data for each I/O buffers */
      for (int j = 0; j < local_array->n_io_group; j++)
      {
        struct autosa_array_ref_group *group = local_array->io_groups[j];
        compute_io_group_data_pack(kernel, group, gen, n_lane);
      }
      if (local_array->drain_group)
      {
        struct autosa_array_ref_group *group = local_array->drain_group;
        compute_io_group_data_pack(kernel, group, gen, n_lane);
      }
    }

    local_array->n_lane = max(1, n_lane);
    local_array->array->n_lane = max(1, n_lane);
  }

  isl_union_map_free(data.host_sched);
  isl_union_map_free(data.copy_sched);
  isl_union_map_free(data.full_sched);
  isl_union_map_free(data.pe_sched);
  isl_schedule_node_free(node);

  /* Compute a tiling for all the array reference groups in "kernel". */
  compute_group_tilings_pe(kernel);
  compute_group_tilings_io(kernel);
  compute_group_tilings_drain(kernel);

  return isl_stat_ok;
}

/* Return the access relation associated with the comm pair of the array reference
 * "ref" in the current I/O group "group".
 * For each reference, if 
 * - extract copy-in access (read == 1) 
 *   - read access
 *     - RAR: extract the union of the src and dest domain of dep
 *     - RAW: extract the dest domain of dep
 * - extract copy-out access (write == 1)
 *   - write access
 *     - RAW: extract the src domain of dep 
 */
__isl_give isl_union_map *autosa_io_group_ref_access_relation(
    struct autosa_array_ref_group *group,
    struct autosa_stmt_access *ref,
    int read, int write)
{
  isl_union_map *access;
  isl_map *map;

  access = isl_union_map_empty(isl_map_get_space(ref->access));
  for (int i = 0; i < ref->n_io_info; i++)
  {
    struct autosa_io_info *info_i = ref->io_info[i];
    if (info_i->io_type == group->io_type &&
        !isl_vec_cmp(info_i->dir, group->dir))
    {
      isl_map *dep = isl_map_factor_domain(isl_map_from_basic_map(
          isl_basic_map_copy(info_i->dep->isl_dep)));
      isl_set *dep_src = isl_map_domain(isl_map_copy(dep));
      isl_set *dep_dest = isl_map_range(dep);
      if (info_i->dep->type == AUTOSA_DEP_RAR)
      {
        isl_set *domain = isl_set_union(dep_src, dep_dest);
        domain = isl_set_coalesce(domain);
        access = isl_union_map_union(access,
                                     isl_union_map_from_map(isl_map_intersect_domain(
                                         isl_map_copy(ref->access), domain)));
      }
      else if (info_i->dep->type == AUTOSA_DEP_RAW)
      {
        isl_set *domain;
        if (ref->read)
        {
          domain = dep_dest;
          isl_set_free(dep_src);
        }
        else
        {
          domain = dep_src;
          isl_set_free(dep_dest);
        }
        access = isl_union_map_union(access,
                                     isl_union_map_from_map(isl_map_intersect_domain(
                                         isl_map_copy(ref->access), domain)));
      }
      else
      {
        isl_set_free(dep_src);
        isl_set_free(dep_dest);
      }
    }
  }

  return access;
}

/* Return the access relation associated with the comm pair of the array reference
 * "ref" in the current drain group "group".
 * For each reference, domain = domain - src domain of WAW dep.
 */
__isl_give isl_union_map *autosa_drain_group_ref_access_relation(
    struct autosa_array_ref_group *group,
    struct autosa_stmt_access *ref,
    int read, int write, __isl_keep isl_union_set *domain)
{
  isl_union_map *access;
  isl_set *acc_domain;
  isl_space *space;

  access = isl_union_map_empty(isl_map_get_space(group->access));
  acc_domain = isl_map_domain(isl_map_copy(ref->access));
  space = isl_set_get_space(acc_domain);
  isl_set_free(acc_domain);
  acc_domain = isl_union_set_extract_set(domain, space);
  for (int i = 0; i < ref->n_io_info; i++)
  {
    struct autosa_io_info *info_i = ref->io_info[i];
    if (info_i->dep->type == AUTOSA_DEP_WAW)
    {
      isl_set *src_domain;
      isl_space *space, *src_space;
      isl_id *src_id;

      space = isl_basic_map_get_space(info_i->dep->isl_dep);
      src_space = isl_space_unwrap(isl_space_domain(space));
      src_id = isl_space_get_tuple_id(src_space, isl_dim_out);
      isl_space_free(src_space);
      if (src_id != ref->ref_id)
      {
        isl_id_free(src_id);
        continue;
      }
      isl_id_free(src_id);
      src_domain = isl_map_domain(isl_map_factor_domain(isl_map_from_basic_map(
          isl_basic_map_copy(info_i->dep->isl_dep))));
      acc_domain = isl_set_subtract(acc_domain, src_domain);
    }
  }
  access = isl_union_map_union(access,
                               isl_union_map_from_map(isl_map_intersect_domain(
                                   isl_map_copy(ref->access), acc_domain)));

  return access;
}

/* For each reference, if 
 * - extract copy-in access (read == 1) 
 *   - read access
 *     - RAR: extract the union of the src and dest domain of dep
 *     - RAW: extract the dest domain of dep
 * - extract copy-out access (write == 1)
 *   - write access
 *     - RAW: extract the src domain of dep 
 */
__isl_give isl_union_map *autosa_io_group_access_relation(
  struct autosa_array_ref_group *group, 
  struct autosa_kernel *kernel,
  int read, int write)
{
  isl_union_map *access;

  access = isl_union_map_empty(isl_map_get_space(group->access));
  for (int i = 0; i < group->n_ref; ++i)
  {
    struct autosa_stmt_access *ref_i = group->refs[i];

    if (!((read && group->refs[i]->read) ||
          (write && group->refs[i]->write)))
      continue;

    if (group->group_type == AUTOSA_IO_GROUP) 
    {
      access = isl_union_map_union(access,
                                   autosa_io_group_ref_access_relation(group, ref_i, read, write));
    } else if (group->group_type == AUTOSA_DRAIN_GROUP) 
    {
      access = isl_union_map_union(access,
                                   autosa_drain_group_ref_access_relation(group, ref_i, read, write,
                                                                          kernel->expanded_domain));
    }
  }

  /* Simplify the access relation. */
  access = isl_union_map_coalesce(access);

  return access;
}

/* Return the union of all tagged access relations in the group.
 */
__isl_give isl_union_map *group_tagged_access_relation(
    struct autosa_array_ref_group *group)
{
  int i;
  isl_union_map *access;

  access = isl_union_map_empty(isl_map_get_space(group->access));
  for (i = 0; i < group->n_ref; ++i)
  {
    isl_map *map_i;

    map_i = isl_map_copy(group->refs[i]->tagged_access);
    access = isl_union_map_union(access,
                                 isl_union_map_from_map(map_i));
  }

  return access;
}

/* Given a set of wrapped references "ref", return the corresponding
 * access relations based on the tagged access relations "tagged".
 *
 * The elements of "ref" are of the form
 *
 *	[D -> R]
 *
 * with D an iteration domains and R a reference.
 * The elements of "tagged" are of the form
 *
 *	[D -> R] -> A
 *
 * with A an array.
 *
 * Extend "tagged" to include the iteration domain in the range, i.e.,
 *
 *	[D -> R] -> [D -> A]
 *
 * apply the result to "ref" and then unwrap the resulting set
 * to obtain relations of the form
 *
 *	D -> A
 */
__isl_give isl_union_map *wrapped_reference_to_access(
    __isl_take isl_union_set *ref, __isl_take isl_union_map *tagged)
{
  isl_union_map *tag2access;

  tag2access = isl_union_map_copy(tagged);
  tag2access = isl_union_map_universe(tag2access);
  tag2access = isl_union_set_unwrap(isl_union_map_domain(tag2access));

  /* Construct [D -> R] -> D */
  tag2access = isl_union_map_domain_map(tag2access);

  /* Construct [D -> R] -> [D -> A] */
  tag2access = isl_union_map_range_product(tag2access, tagged);

  ref = isl_union_set_coalesce(ref);
  ref = isl_union_set_apply(ref, tag2access);

  return isl_union_set_unwrap(ref);
}

/* Given an access relation "access" from one or more array reference groups,
 * remove those reads if ("read" is 1) or writes (if "read" is 0)
 * that are only needed to communicate data within
 * the same iteration of "sched".
 * The domain of "sched" corresponds to the original statement instances,
 * i.e., those that appear in the domains of the access relations.
 * "tagged" contains all tagged access relations to all
 * the array reference groups accessed by "access" from statement
 * instances scheduled by "sched".
 *
 * If the access is a read then it is either an element of
 *
 *	live_in union (range flow)
 *
 * where live_in and flow may be overapproximations, or
 * it reads an uninitialized value (that is not live-in because
 * there is an intermediate kill) or it reads a value that was
 * written within the same (compound) statement instance.
 * If the access is a write then it is either an element of
 *
 *	live_out union (domain flow)
 *
 * or it writes a value that is never read (and is not live-out
 * because of an intermediate kill) or only
 * within the same (compound) statement instance.
 * In both cases, the access relation is also a subset of
 * the group access relation.
 *
 * The cases where an uninitialized value is read or a value is written
 * that is never read or where the dataflow occurs within a statement
 * instance are also considered local and may also be removed.
 *
 * Essentially, we compute the intersection of "access" with either
 *
 *	live_in union (range non-local-flow)
 *
 * or
 *
 *	live_out union (domain non-local-flow)
 *
 * We first construct a relation "local"
 *
 *	[[D -> R] -> [D' -> R']]
 *
 * of pairs of domain iterations accessing the reference group
 * and references in the group that are coscheduled by "sched".
 *
 * If this relation does not intersect the dataflow dependences,
 * then there is nothing we can possibly remove, unless the dataflow
 * dependences themselves only relate a subset of the accesses.
 * In particular, the accesses may not be involved in any dataflow
 * dependences, either because they are uninitialized reads/dead writes
 * or because the dataflow occurs inside a statement instance.
 *
 * Since the computation below may break up the access relation
 * into smaller pieces, we only perform the intersection with
 * the non-local dependent accesses if the local pairs
 * intersect the dataflow dependences. Otherwise, we intersect
 * with the universe of the non-local dependent accesses.
 * This should at least remove accesses from statements that
 * do not participate in any dependences.
 *
 * In particular, we remove the "local" dataflow dependences from
 * the set of all dataflow dependences, or at least those
 * that may contribute to a domain/range that intersects
 * the domain of "access".
 * Note that if the potential dataflow dependences are an overapproximation
 * of the actual dataflow dependences, then the result remains an
 * overapproximation of the non-local dataflow dependences.
 * Copying to/from global memory is only needed for the references
 * in the domain/range of the result or for accesses that are live out/in
 * for the entire scop.
 *
 * We therefore map the domain/range of the "external" relation
 * to the corresponding access relation and take the union with
 * the live out/in relation.
 */
__isl_give isl_union_map *remove_local_accesses(
    struct autosa_prog *prog, __isl_take isl_union_map *tagged,
    __isl_take isl_union_map *access, __isl_take isl_union_map *sched,
    int read)
{
  int empty;
  isl_union_pw_multi_aff *tagger;
  isl_union_set *domain, *access_domain;
  isl_union_map *local, *external, *universe;
  isl_union_set *tag_set;

  if (isl_union_map_is_empty(access))
  {
    isl_union_map_free(sched);
    isl_union_map_free(tagged);
    return access;
  }

  /* Tagger maps the tagged iteration domain to untagged iteration domain. 
   * Iteration domain is tagged to the access function.
   * e.g., [S1[i,j,k]->_pet_ref_1[]] -> S1[(i),(j),(k)]
   */
  tagger = isl_union_pw_multi_aff_copy(prog->scop->tagger);
  domain = isl_union_map_domain(isl_union_map_copy(tagged));
  tagger = isl_union_pw_multi_aff_intersect_domain(tagger,
                                                   isl_union_set_copy(domain));
  sched = isl_union_map_preimage_domain_union_pw_multi_aff(sched, tagger);

  /* Construct the relation "local"
   * [[D -> R] -> [D' -> R']]
   */
  local = isl_union_map_apply_range(sched,
                                    isl_union_map_reverse(isl_union_map_copy(sched)));
  /* Derive the local dependence set. */
  local = isl_union_map_intersect(local,
                                  isl_union_map_copy(prog->scop->tagged_dep_flow));

  empty = isl_union_map_is_empty(local);

  external = isl_union_map_copy(prog->scop->tagged_dep_flow);
  universe = isl_union_map_universe(isl_union_map_copy(access));
  access_domain = isl_union_map_domain(universe);
  domain = isl_union_set_universe(domain);
  universe = isl_union_set_unwrap(domain);
  universe = isl_union_map_intersect_domain(universe, access_domain);
  domain = isl_union_map_wrap(universe);
  if (read)
    external = isl_union_map_intersect_range(external, domain);
  else
    external = isl_union_map_intersect_domain(external, domain);
  external = isl_union_map_intersect_params(external,
                                            isl_set_copy(prog->scop->context));
  external = isl_union_map_subtract(external, local);
  /* So far external contains only access non-local RAW pairs. */

  if (read)
  {
    tag_set = isl_union_map_range(external);
    external = wrapped_reference_to_access(tag_set, tagged);
    external = isl_union_map_union(external,
                                   isl_union_map_copy(prog->scop->live_in));
  }
  else
  {
    tag_set = isl_union_map_domain(external);
    external = wrapped_reference_to_access(tag_set, tagged);
    external = isl_union_map_union(external,
                                   isl_union_map_copy(prog->scop->live_out));
  }

  if (empty < 0)
    external = isl_union_map_free(external);
  else if (empty)
    external = isl_union_map_universe(external);

  access = isl_union_map_intersect(access, external);

  return access;
}

/* Extended from remove_local_accesses.
 * Given an access relation "access" from one or more array reference groups,
 * remove those reads if ("read" is 1) or writes (if "read" is 0)
 * that are only needed to communicate data within
 * the same iteration of "sched".
 * We exclude the live-in and live-out accesses. 
 * This function only considers RAW deps.
 * 
 * "tagged" contain all tagged accesses in the group.
 * "access" contain the accesses of interest for the current group.
 * "sched" is the prefix schedule. The depth of the scheduling domain is where
 * the copy statemetns are inserted.
 */
__isl_give isl_union_map *remove_local_accesses_flow(
  struct autosa_prog *prog, __isl_take isl_union_map *tagged,
  __isl_take isl_union_map *access, __isl_take isl_union_map *sched,
  int read)
{
  int empty;
  isl_union_pw_multi_aff *tagger;
  isl_union_set *domain, *access_domain;
  isl_union_map *local, *external, *universe;
  isl_union_set *tag_set;

  if (isl_union_map_is_empty(access))
  {
    isl_union_map_free(sched);
    isl_union_map_free(tagged);
    return access;
  }

  /* Tagger maps the tagged iteration domain to untagged iteration domain. 
   * Iteration domain is tagged to the access function.
   * e.g., [S1[i,j,k]->_pet_ref_1[]] -> S1[(i),(j),(k)]
   */
  tagger = isl_union_pw_multi_aff_copy(prog->scop->tagger);
  domain = isl_union_map_domain(isl_union_map_copy(tagged));
  tagger = isl_union_pw_multi_aff_intersect_domain(tagger,
                                                   isl_union_set_copy(domain));
  sched = isl_union_map_preimage_domain_union_pw_multi_aff(sched, tagger);

  /* Construct the relation "local"
   * [[D -> R] -> [D' -> R']]
   * D contains all the iteration domains accessing the elements in the group.
   */
  local = isl_union_map_apply_range(sched,
                                    isl_union_map_reverse(isl_union_map_copy(sched)));
  /* Derive the local dependence set. */
  local = isl_union_map_intersect(local,
                                  isl_union_map_copy(prog->scop->tagged_dep_flow));
  empty = isl_union_map_is_empty(local);

  external = isl_union_map_copy(prog->scop->tagged_dep_flow);
  universe = isl_union_map_universe(isl_union_map_copy(access));
  access_domain = isl_union_map_domain(universe);
  domain = isl_union_set_universe(domain);
  universe = isl_union_set_unwrap(domain);
  universe = isl_union_map_intersect_domain(universe, access_domain);
  domain = isl_union_map_wrap(universe);
  if (read)
    external = isl_union_map_intersect_range(external, domain);
  else
    external = isl_union_map_intersect_domain(external, domain);
  external = isl_union_map_intersect_params(external,
                                            isl_set_copy(prog->scop->context));
  external = isl_union_map_subtract(external, local);
  /* So far "external" contains only iteration elements accessing the 
   * non-local RAW pairs. */

  if (read)
  {
    tag_set = isl_union_map_range(external);
    external = wrapped_reference_to_access(tag_set, tagged);
    //    /* Temporarily commented out, we don't consider live-in so far. */
    //		external = isl_union_map_union(external,
    //				isl_union_map_copy(prog->scop->live_in));
  }
  else
  {
    tag_set = isl_union_map_domain(external);
    external = wrapped_reference_to_access(tag_set, tagged);
    //    /* Temporarily commented out, we don't consider live-out so far. */
    //		external = isl_union_map_union(external,
    //				isl_union_map_copy(prog->scop->live_out));
  }

  if (empty < 0)
    external = isl_union_map_free(external);
  else if (empty)
    external = isl_union_map_universe(external);

  access = isl_union_map_intersect(access, external);

  return access;
}

/* Given an access relation "access" from "group", remove those reads
 * if ("read" is 1) or writes (if "read" is 0) that are only needed to
 * communicate data within the same iteration of the schedule "prefix"
 * at the position where the copying of the group is inserted.
 * That is, the output dimension of "prefix"
 * is equal to tile->depth.
 * The domain of "prefix" corresponds to the original statement instances,
 * i.e., those that appear in the domains of the access relations.
 *
 * Extract the tagged access relation of "group" and
 * then call remove_local_accesses.
 */
__isl_give isl_union_map *remove_local_accesses_group_flow(
  struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
  __isl_take isl_union_map *access, __isl_keep isl_union_map *prefix,
  int read)
{
  isl_union_map *sched, *tagged;

  if (isl_union_map_is_empty(access))
    return access;

  tagged = group_tagged_access_relation(group);
  sched = isl_union_map_copy(prefix);

  return remove_local_accesses_flow(kernel->prog, tagged, access, sched, read);
}

/* Given an access relation "access" from "group", remove those reads
 * if ("read" is 1) or writes (if "read" is 0) that are only needed to
 * communicate data within the same iteration of the schedule "prefix"
 * at the position where the copying of the group is inserted.
 * That is, the output dimension of "prefix"
 * is equal to tile->depth.
 * The domain of "prefix" corresponds to the original statement instances,
 * i.e., those that appear in the domains of the access relations.
 *
 * Extract the tagged access relation of "group" and
 * then call remove_local_accesses.
 */
__isl_give isl_union_map *remove_local_accesses_group(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    __isl_take isl_union_map *access, __isl_keep isl_union_map *prefix,
    int read)
{
  isl_union_map *sched, *tagged;

  if (isl_union_map_is_empty(access))
    return access;

  tagged = group_tagged_access_relation(group);
  sched = isl_union_map_copy(prefix);

  return remove_local_accesses(kernel->prog, tagged, access, sched, read);
}

/* Compute the access relation for the access "ref".
 * Return the map D -> [S -> A]
 * where D is the iteration domain, S is the scheduling domains with the depth
 * of "node".
 */
__isl_give isl_union_map *io_comm_access_ref(
    struct autosa_kernel *kernel, __isl_keep isl_schedule_node *node,
    struct autosa_array_ref_group *group,
    struct autosa_stmt_access *ref,
    int read)
{
  isl_union_map *prefix;
  isl_union_map *access;  

  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  if (group->group_type == AUTOSA_IO_GROUP) {
    access = autosa_io_group_ref_access_relation(group, ref, read, !read);
  } else if (group->group_type == AUTOSA_DRAIN_GROUP) {
    access = autosa_drain_group_ref_access_relation(
        group, ref, read, !read, kernel->expanded_domain);
  }

  if (group->local_array->array_type == AUTOSA_INT_ARRAY)
    access = remove_local_accesses_group_flow(kernel, group, access, prefix, read);

  if (group->group_type == AUTOSA_IO_GROUP && group->attached_drain_group && !read) {
    // TODO: temporary solution. We assume the io group and attached drain group
    // always share the same access. Could be buggy.
    access = isl_union_map_union(access, 
                                 autosa_drain_group_ref_access_relation(group->attached_drain_group, ref, read, !read, kernel->expanded_domain));
  }

  access = isl_union_map_range_product(prefix, access);

  return access;
}

/* Compute the access relation for the accesses in the group.
 * Return the map D -> [S -> A]
 * where D is the iteration domain, S is the scheduling domains with the depth
 * of "node".
 */
__isl_give isl_union_map *io_comm_access(
    struct autosa_kernel *kernel, __isl_keep isl_schedule_node *node,
    struct autosa_array_ref_group *group, int read)
{
  isl_union_map *prefix;
  isl_union_map *access;

  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                            isl_union_pw_multi_aff_copy(kernel->contraction));
  access = isl_union_map_empty(isl_map_get_space(group->access));
  for (int i = 0; i < group->n_ref; i++)
  {
    struct autosa_stmt_access *ref = group->refs[i];
    if (group->group_type == AUTOSA_IO_GROUP) {
      access = isl_union_map_union(access, autosa_io_group_ref_access_relation(
                                               group, ref, read, !read));      
    } else if (group->group_type == AUTOSA_DRAIN_GROUP)
      access = isl_union_map_union(access, autosa_drain_group_ref_access_relation(
                                               group, ref, read, !read, kernel->expanded_domain));
  }

  if (group->attached_drain_group) {    
    for (int i = 0; i < group->attached_drain_group->n_ref; i++) {
      struct autosa_stmt_access *ref = group->attached_drain_group->refs[i];
      access = isl_union_map_union(access, autosa_drain_group_ref_access_relation(
                                               group->attached_drain_group, ref, read, !read, kernel->expanded_domain));      
    }
  }

  if (group->local_array->array_type == AUTOSA_INT_ARRAY)
    access = remove_local_accesses_group_flow(kernel, group, access, prefix, read);

  access = isl_union_map_range_product(prefix, access);

  return access;
}

void free_group_pair(void *user)
{
  struct autosa_array_ref_group_pair *pair =
      (struct autosa_array_ref_group_pair *)user;
  free(pair);
}

/* Create a register tiling at the "node" for array reference "ref".
 */
struct autosa_array_tile *create_register_tiling(
    isl_schedule_node *node,
    struct autosa_array_ref_group *group,
    struct autosa_stmt_access *ref)
{
  isl_union_map *access;
  isl_map *access_i;
  isl_ctx *ctx;
  isl_union_map *sched;
  isl_bool ok;
  struct autosa_array_tile *tile;
  isl_union_set *domain;

  ctx = isl_schedule_node_get_ctx(node);
  access = isl_union_map_from_map(isl_map_copy(ref->access));
  tile = autosa_array_tile_create(ctx, group->array->n_index);
  sched = isl_schedule_node_get_prefix_schedule_union_map(node);
  domain = isl_schedule_node_get_domain(node);
  sched = isl_union_map_intersect_domain(sched, domain);
  access = isl_union_map_apply_domain(access, sched);
  access_i = isl_map_from_union_map(access);
  ok = can_tile(access_i, tile);
  isl_map_free(access_i);
  autosa_array_ref_group_compute_tiling(tile, group);

  return tile;
}

/* Return the extent of "array", recomputed from the bounds.
 * The recomputed extent may be simpler than the original extent.
 */
static __isl_give isl_set *array_extent(struct autosa_array_info *array)
{
  int i;
  isl_id *id;
  isl_space *space;
  isl_local_space *ls;
  isl_set *extent;

  id = isl_set_get_tuple_id(array->extent);
  space = isl_set_get_space(array->extent);
  extent = isl_set_universe(isl_space_copy(space));
  ls = isl_local_space_from_space(space);
  for (i = 0; i < array->n_index; ++i)
  {
    isl_pw_aff *bound;
    isl_aff *aff;
    isl_pw_aff *index;
    isl_set *lt;

    extent = isl_set_lower_bound_si(extent, isl_dim_set, i, 0);

    aff = isl_aff_var_on_domain(isl_local_space_copy(ls),
                                isl_dim_set, i);
    index = isl_pw_aff_from_aff(aff);
    bound = isl_multi_pw_aff_get_pw_aff(array->bound, i);
    bound = isl_pw_aff_from_range(bound);
    bound = isl_pw_aff_add_dims(bound, isl_dim_in, array->n_index);
    bound = isl_pw_aff_set_tuple_id(bound, isl_dim_in,
                                    isl_id_copy(id));
    lt = isl_pw_aff_lt_set(index, bound);
    extent = isl_set_intersect(extent, lt);
  }
  isl_local_space_free(ls);
  isl_id_free(id);

  return extent;
}

/* Return a map from the first group->local_tile->depth dimensions
 * of the computed schedule to the array tile in
 * global memory that corresponds to the local memory copy.
 *
 * In particular, return a map
 *
 *	{ D[i] -> A[a] }
 *
 * with constraints
 *
 *	tile_offset(i) <= a <= tile_offset(i) + tile_size - 1		(1)
 *
 * and
 *
 *	0 <= a <= array_size - 1					(2)
 *
 * Note that if some stride has been detected (i.e., when
 * group->local_tile->bound[i].shift is set), then a in (1) refers
 * to the shifted and scaled down version.
 *
 * Constraints (1) are obtained by mapping the size constraints on the
 * local memory tile back to the access relation.
 * Constraints (2) are obtained from the (recomputed) extent.
 */
__isl_give isl_map *group_tile(struct autosa_array_ref_group *group)
{
  int i;
  int n_index = group->array->n_index;
  isl_map *tile;
  isl_space *space;
  isl_set *local;
  isl_set *extent;

  space = isl_multi_aff_get_space(group->local_tile->tiling);
  space = isl_space_range(space);
  local = isl_set_universe(space);
  for (i = 0; i < n_index; ++i)
  {
    isl_val *bound;

    local = isl_set_lower_bound_si(local, isl_dim_set, i, 0);
    bound = isl_val_copy(group->local_tile->bound[i].size);
    bound = isl_val_sub_ui(bound, 1);
    local = isl_set_upper_bound_val(local, isl_dim_set, i, bound);
  }
  local = isl_set_preimage_multi_aff(local,
                                     isl_multi_aff_copy(group->local_tile->tiling));
  tile = isl_set_unwrap(local);
  extent = array_extent(group->array);
  tile = isl_map_intersect_range(tile, extent);

  return tile;
}

/* Return a map from the first tile->depth dimensions
 * of the computed schedule to the array tile in
 * global memory that corresponds to the local memory copy.
 *
 * In particular, return a map
 *
 *	{ D[i] -> A[a] }
 *
 * with constraints
 *
 *	tile_offset(i) <= a <= tile_offset(i) + tile_size - 1		(1)
 *
 * and
 *
 *	0 <= a <= array_size - 1					(2)
 *
 * Note that if some stride has been detected (i.e., when
 * group->local_tile->bound[i].shift is set), then a in (1) refers
 * to the shifted and scaled down version.
 *
 * Constraints (1) are obtained by mapping the size constraints on the
 * local memory tile back to the access relation.
 * Constraints (2) are obtained from the (recomputed) extent.
 */
__isl_give isl_map *group_tile_buffer(struct autosa_array_ref_group *group,
                                      struct autosa_array_tile *tile)
{
  int i;
  int n_index = group->array->n_index;
  isl_map *map;
  isl_space *space;
  isl_set *local;
  isl_set *extent;

  space = isl_multi_aff_get_space(tile->tiling);
  space = isl_space_range(space);
  local = isl_set_universe(space);

  for (i = 0; i < n_index; ++i)
  {
    isl_val *bound;

    local = isl_set_lower_bound_si(local, isl_dim_set, i, 0);
    bound = isl_val_copy(tile->bound[i].size);
    bound = isl_val_sub_ui(bound, 1);
    local = isl_set_upper_bound_val(local, isl_dim_set, i, bound);
  }
  local = isl_set_preimage_multi_aff(local,
                                     isl_multi_aff_copy(tile->tiling));
  map = isl_set_unwrap(local);
  extent = array_extent(group->array);
  map = isl_map_intersect_range(map, extent);

  return map;
}

/* Return the data packing factor used to trnasfer the data of "group" across
 * "module".
 * Specifically, we use data_pack_inter for IO modules.
 * For PE modules, if the array is an external array, it should equal to the 
 * io_group SIMD lane (group->n_lane).
 * If the array is an internal array, for the drain group, we use the SIMD lane,
 * for the io group, if the io is an exterior I/O, we use the SIMD lane, 
 * otherwise, we use the data packing factor of the local buffer 
 * (io_buffers[0]->n_lane) which is allocated inside the PE.
 */
int get_io_group_n_lane(struct autosa_hw_module *module,
                        struct autosa_pe_dummy_module *dummy_module,
                        struct autosa_array_ref_group *group)
{
  int n_lane;

  if (module && module->type == PE_MODULE || dummy_module)
  {
    n_lane = (group->local_array->array_type == AUTOSA_EXT_ARRAY) ? group->n_lane : ((group->group_type == AUTOSA_DRAIN_GROUP) ? group->n_lane : ((group->io_type == AUTOSA_EXT_IO) ? group->n_lane : group->io_buffers[0]->n_lane));
  }
  else
  {
    n_lane = module->data_pack_inter;
  }

  return n_lane;
}

/* Given a description of an array tile "tile" and the "space"
 *
 *	{ D -> A }
 *
 * where D represents the first tile->depth schedule dimensions
 * and A represents the array, construct an isl_multi_aff
 *
 *	{ [D[i] -> A[a]] -> A'[a'] }
 *
 * with A' a scaled down copy of A according to the shifts and strides
 * in "tile".  In particular,
 *
 *	a' = (a + shift(i))/stride
 *
 * "insert_array" represents
 *
 *	{ [D -> A] -> D }
 *
 * and is used to insert A into the domain of functions that only
 * reference D.
 */
static __isl_give isl_multi_aff *strided_tile_depth(
    struct autosa_array_tile *tile, __isl_keep isl_space *space,
    __isl_keep isl_multi_aff *insert_array, int depth)
{
  int i;
  isl_ctx *ctx;
  isl_multi_aff *shift;
  isl_multi_val *stride;
  isl_space *space2;
  isl_local_space *ls;
  isl_multi_aff *tiling;

  ctx = isl_space_get_ctx(space);
  space2 = isl_space_domain(isl_space_copy(space));
  ls = isl_local_space_from_space(space2);
  space2 = isl_space_range(isl_space_copy(space));
  stride = isl_multi_val_zero(space2);
  shift = isl_multi_aff_zero(isl_space_copy(space));

  for (i = 0; i < tile->n; ++i)
  {
    struct autosa_array_bound *bound = &tile->bound[i];
    isl_val *stride_i;
    isl_aff *shift_i;

    stride_i = isl_val_copy(bound->stride);
    shift_i = isl_aff_copy(bound->shift);

    shift_i = isl_aff_insert_dims(shift_i, isl_dim_in, tile->depth, depth - tile->depth);

    stride = isl_multi_val_set_val(stride, i, stride_i);
    shift = isl_multi_aff_set_aff(shift, i, shift_i);
  }
  isl_local_space_free(ls);

  shift = isl_multi_aff_pullback_multi_aff(shift,
                                           isl_multi_aff_copy(insert_array));

  tiling = isl_multi_aff_range_map(isl_space_copy(space));
  tiling = isl_multi_aff_add(tiling, shift);
  tiling = isl_multi_aff_scale_down_multi_val(tiling, stride);

  return tiling;
}

/* Recompute the tiling by extending the scheduling domain to the "depth". */
__isl_give isl_multi_aff *autosa_array_ref_group_recompute_tiling(
    struct autosa_array_tile *tile,
    struct autosa_array_ref_group *group,
    int depth)
{
  int i;
  isl_space *space;
  isl_multi_aff *tiling, *lb, *insert_array;
  isl_printer *p;
  char *local_name;

  if (tile == NULL)
    return NULL;

  space = isl_map_get_space(group->access);
  space = isl_space_from_range(isl_space_range(space));
  /* Build D[i] -> A[a] */
  space = isl_space_add_dims(space, isl_dim_in, depth);
  /* Build [D[i] -> A[a]] -> D[i] */
  insert_array = isl_multi_aff_domain_map(isl_space_copy(space));

  for (i = 0; i < tile->n; ++i)
    if (tile->bound[i].shift)
      break;

  if (i < tile->n)
    tiling = strided_tile_depth(tile, space, insert_array, depth);
  else
    tiling = isl_multi_aff_range_map(isl_space_copy(space));

  lb = isl_multi_aff_zero(space);
  for (i = 0; i < tile->n; ++i)
  {
    isl_aff *lb_i = isl_aff_copy(tile->bound[i].lb);
    lb_i = isl_aff_insert_dims(lb_i, isl_dim_in, tile->depth, depth - tile->depth);
    lb = isl_multi_aff_set_aff(lb, i, lb_i);
  }
  lb = isl_multi_aff_pullback_multi_aff(lb, insert_array);

  tiling = isl_multi_aff_sub(tiling, lb);

  p = isl_printer_to_str(isl_multi_aff_get_ctx(tiling));
  p = autosa_array_ref_group_print_name(group, p);
  local_name = isl_printer_get_str(p);
  isl_printer_free(p);
  tiling = isl_multi_aff_set_tuple_name(tiling, isl_dim_out, local_name);
  free(local_name);

  return tiling;
}

void print_io_grouping_info(FILE *fp, struct autosa_kernel *kernel)
{
  const char *io_types[] = {"AUTOSA_INT_IO", "AUTOSA_EXT_IO", "AUTOSA_UNKNOWN_IO"};

  fprintf(fp, "================= IO Grouping Information =================\n");
  for (int i = 0; i < kernel->n_array; i++) {
    struct autosa_local_array_info *local = &kernel->array[i];
    fprintf(fp, "===================================================\n");
    fprintf(fp, "Array: %s\n", local->array->name);
    fprintf(fp, "===================================================\n");
    fprintf(fp, "local: %d\n", local->array->local);
    fprintf(fp, "n_io_groups: %d\n", local->n_io_group);
    fprintf(fp, "n_drain_groups: %d\n", local->drain_group? 1 : 0);
    for (int j = 0; j < local->n_io_group; j++) {
      struct autosa_array_ref_group *group = local->io_groups[j];
      fprintf(fp, "------------------------------\n");
      fprintf(fp, "IO Group: %d\n", j);
      fprintf(fp, "------------------------------\n");
      fprintf(fp, "copy_in: %d\n", group->copy_in);
      fprintf(fp, "copy_out: %d\n", group->copy_out);
      fprintf(fp, "io_type: %s\n", io_types[group->io_type]);
      char *vec_str = isl_vec_to_str(group->dir);
      fprintf(fp, "io_dir: %s\n", vec_str);
      free(vec_str);
    }
    if (local->drain_group) {
      struct autosa_array_ref_group *group = local->drain_group;
      fprintf(fp, "------------------------------\n");
      fprintf(fp, "Drain Group\n");
      fprintf(fp, "------------------------------\n");
      fprintf(fp, "copy_in: %d\n", group->copy_in);
      fprintf(fp, "copy_out: %d\n", group->copy_out);
      fprintf(fp, "io_type: %s\n", io_types[group->io_type]);
      char *vec_str = isl_vec_to_str(group->dir);
      fprintf(fp, "io_dir: %s\n", vec_str);
      free(vec_str);      
    }
  }
  fprintf(fp, "================= IO Grouping Information =================\n");
}