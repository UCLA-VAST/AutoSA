/* This file defines functions used to manipulate the schedule trees in AutoSA.
 */
#include <isl/ctx.h>
#include <isl/schedule_node.h>

#include "autosa_common.h"
#include "autosa_utils.h"
#include "autosa_schedule_tree.h"

/* Is "node" a mark node with an identifier called "name"?
 */
int is_marked(__isl_keep isl_schedule_node *node, const char *name)
{
  isl_id *mark;
  int has_name;

  if (!node)
    return -1;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_mark)
    return 0;

  mark = isl_schedule_node_mark_get_id(node);
  if (!mark)
    return -1;

  has_name = !strcmp(isl_id_get_name(mark), name);
  isl_id_free(mark);

  return has_name;
}

static __isl_give isl_multi_val *multi_val_from_int_list(
    __isl_take isl_space *space, int *list)
{
  int i, n;
  isl_ctx *ctx;
  isl_multi_val *mv;

  if (!space)
    return NULL;

  ctx = isl_space_get_ctx(space);
  n = isl_space_dim(space, isl_dim_set);
  mv = isl_multi_val_zero(space);
  for (i = 0; i < n; ++i)
  {
    isl_val *v;

    v = isl_val_int_from_si(ctx, list[i]);
    mv = isl_multi_val_set_val(mv, i, v);
  }

  return mv;
}

/* Construct the tile sizes from int array "tile_size".
 */
__isl_give isl_multi_val *construct_band_tile_sizes(
    __isl_keep isl_schedule_node *node, int *tile_size)
{
  isl_space *space;

  if (!node)
    return NULL;

  space = isl_schedule_node_band_get_space(node);
  return multi_val_from_int_list(space, tile_size);
}

/* Extract the pe_opt, space_time, sched_pos property from the band node.
 */
struct autosa_node_band_prop *extract_node_band_prop(__isl_keep isl_schedule_node *node)
{
  struct autosa_node_band_prop *prop = isl_calloc_type(
      isl_schedule_node_get_ctx(node), struct autosa_node_band_prop);
  prop->mupa = isl_schedule_node_band_get_partial_schedule(node);
  prop->n_member = isl_schedule_node_band_n_member(node);
  prop->coincident = isl_calloc_array(isl_schedule_node_get_ctx(node), int,
                                      prop->n_member);
  for (int i = 0; i < prop->n_member; i++)
  {
    prop->coincident[i] = isl_schedule_node_band_member_get_coincident(node, i);
  }
  prop->permutable = isl_schedule_node_band_get_permutable(node);
  prop->space_time = isl_calloc_array(isl_schedule_node_get_ctx(node),
                                      enum autosa_loop_type, prop->n_member);
  prop->pe_opt = isl_calloc_array(isl_schedule_node_get_ctx(node),
                                  enum autosa_loop_type, prop->n_member);
  prop->sched_pos = isl_calloc_array(isl_schedule_node_get_ctx(node),
                                     int, prop->n_member);  
  for (int i = 0; i < prop->n_member; i++)
  {
    prop->space_time[i] = isl_schedule_node_band_member_get_space_time(node, i);
    prop->pe_opt[i] = isl_schedule_node_band_member_get_pe_opt(node, i);
    prop->sched_pos[i] = isl_schedule_node_band_member_get_sched_pos(node, i);
    prop->iter[i] = isl_schedule_node_band_member_get_iter(node, i);
  }  

  return prop;
}

struct autosa_node_band_prop *autosa_node_band_prop_free(
    __isl_take struct autosa_node_band_prop *prop)
{
  isl_multi_union_pw_aff_free(prop->mupa);
  free(prop->coincident);
  free(prop->space_time);
  free(prop->pe_opt);
  free(prop->sched_pos);  

  free(prop);

  return NULL;
}

/* Examines if the "node" is a permutable band node. */
isl_bool is_permutable_node(__isl_keep isl_schedule_node *node)
{
  if (!node)
    return isl_bool_error;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return isl_bool_false;
  if (!isl_schedule_node_band_get_permutable(node))
    return isl_bool_false;
  if (isl_schedule_node_band_n_member(node) < 1)
    return isl_bool_false;

  return isl_bool_true;
}

/* Examines if the node is a permutable band node. If so, 
 * increase the count of permutable node.
 */
static isl_bool is_permutable_node_cnt(
    __isl_keep isl_schedule_node *node, void *user)
{
  isl_val *n_permutable_node = (isl_val *)(user);
  if (!node)
    return isl_bool_error;

  if (is_permutable_node(node) == isl_bool_true)
    n_permutable_node = isl_val_add_ui(n_permutable_node, 1);

  return isl_bool_true;
}

/* Examines that if the program only contains one permutable node and there is
 * no other node beside it.
 */
isl_bool has_single_permutable_node(__isl_keep isl_schedule *schedule)
{
  isl_schedule_node *root;
  root = isl_schedule_get_root(schedule);
  isl_val *n_permutable_node = isl_val_zero(isl_schedule_get_ctx(schedule));
  isl_bool all_permutable_node = isl_schedule_node_every_descendant(root,
                                                                    &is_permutable_node_cnt, n_permutable_node);
  isl_schedule_node_free(root);

  if (all_permutable_node && isl_val_is_one(n_permutable_node))
  {
    isl_val_free(n_permutable_node);
    return isl_bool_true;
  }
  else
  {
    isl_val_free(n_permutable_node);
    return isl_bool_false;
  }
}

/* Examines if the dependence is uniform based on the partial schedule
 * in the node. We will calculate the dependence vector and examine 
 * if each dimension is a constant.
 */
isl_bool is_dep_uniform_at_node(__isl_keep isl_schedule_node *node, void *user)
{
  isl_basic_map *dep = (isl_basic_map *)(user);
  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return isl_bool_true;

  /* By this stage we know that if a node is a band node, it is a 
   * permutable band node to be analyzed. 
   */
  isl_multi_union_pw_aff *p_sc = isl_schedule_node_band_get_partial_schedule(node);
  isl_union_pw_multi_aff *contraction = isl_schedule_node_get_subtree_contraction(node);
  p_sc = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(p_sc, contraction);

  isl_bool is_uniform = isl_bool_true;
  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
  {
    isl_union_pw_aff *p_sc_hyp = isl_multi_union_pw_aff_get_union_pw_aff(p_sc, i);
    /* Obtain the schedule for the src statment. */
    isl_space *space = isl_basic_map_get_space(dep);
    isl_space *src_space = isl_space_domain(isl_space_copy(space));
    isl_space *dest_space = isl_space_range(space);

    isl_pw_aff *src_sc;
    isl_pw_aff_list *p_sc_hyp_list = isl_union_pw_aff_get_pw_aff_list(p_sc_hyp);
    for (int j = 0; j < isl_union_pw_aff_n_pw_aff(p_sc_hyp); j++)
    {
      isl_pw_aff *single_sc = isl_pw_aff_list_get_pw_aff(p_sc_hyp_list, j);
      isl_space *single_sc_stmt = isl_space_domain(isl_pw_aff_get_space(single_sc));
      if (isl_space_is_equal(src_space, single_sc_stmt))
      {
        isl_space_free(single_sc_stmt);
        src_sc = single_sc;
        break;
      }
      isl_pw_aff_free(single_sc);
      isl_space_free(single_sc_stmt);
    }
    isl_pw_aff_list_free(p_sc_hyp_list);
    isl_space_free(src_space);

    /* Obtain the schedule for the dest statement. */
    isl_pw_aff *dest_sc;
    p_sc_hyp_list = isl_union_pw_aff_get_pw_aff_list(p_sc_hyp);
    for (int j = 0; j < isl_union_pw_aff_n_pw_aff(p_sc_hyp); j++)
    {
      isl_pw_aff *single_sc = isl_pw_aff_list_get_pw_aff(p_sc_hyp_list, j);
      isl_space *single_sc_stmt = isl_space_domain(isl_pw_aff_get_space(single_sc));
      if (isl_space_is_equal(dest_space, single_sc_stmt))
      {
        isl_space_free(single_sc_stmt);
        dest_sc = single_sc;
        break;
      }
      isl_pw_aff_free(single_sc);
      isl_space_free(single_sc_stmt);
    }
    isl_pw_aff_list_free(p_sc_hyp_list);
    isl_space_free(dest_space);

    /* Compute the dependence distance at the current hyperplane. */
    /* Step 1: Extend the scheduling function. */
    isl_size src_sc_dim = isl_pw_aff_dim(src_sc, isl_dim_in);
    isl_size dest_sc_dim = isl_pw_aff_dim(dest_sc, isl_dim_in);
    src_sc = isl_pw_aff_insert_dims(src_sc, isl_dim_in, src_sc_dim, dest_sc_dim);
    dest_sc = isl_pw_aff_insert_dims(dest_sc, isl_dim_in, 0, src_sc_dim);
    for (int j = 0; j < dest_sc_dim; j++)
    {
      isl_pw_aff_set_dim_id(src_sc, isl_dim_in, src_sc_dim + j, isl_pw_aff_get_dim_id(dest_sc, isl_dim_in, src_sc_dim + j));
    }
    for (int j = 0; j < src_sc_dim; j++)
    {
      isl_pw_aff_set_dim_id(dest_sc, isl_dim_in, j, isl_pw_aff_get_dim_id(src_sc, isl_dim_in, j));
    }

    isl_pw_aff *dis_sc = isl_pw_aff_sub(dest_sc, src_sc);

    /* Step 2: Convert the basic_map into basic_set. */
    isl_mat *eq_mat = isl_basic_map_equalities_matrix(dep,
                                                      isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);
    isl_mat *ieq_mat = isl_basic_map_inequalities_matrix(dep,
                                                         isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);

    isl_basic_set *dep_set = isl_basic_set_from_constraint_matrices(
        isl_space_domain(isl_pw_aff_get_space(dis_sc)),
        eq_mat, ieq_mat,
        isl_dim_set, isl_dim_div, isl_dim_param, isl_dim_cst);

    /* Step 3: Intersect the scheduling function with the domain. */
    isl_pw_aff *dis = isl_pw_aff_intersect_domain(dis_sc,
                                                  isl_set_from_basic_set(isl_basic_set_copy(dep_set)));

    isl_union_pw_aff_free(p_sc_hyp);
    isl_basic_set_free(dep_set);

    /* Examine if the dependence distance is constant. */
    if (!isl_pw_aff_is_cst(dis))
    {
      is_uniform = isl_bool_false;
      isl_pw_aff_free(dis);
      break;
    }

    isl_pw_aff_free(dis);
  }

  isl_multi_union_pw_aff_free(p_sc);
  return is_uniform;
}

/* Apply the schedule on the dependence and check if every dimension is a constant. 
 * Dep in the form of S1[]->S2[].
 */
isl_bool is_dep_uniform(__isl_take isl_basic_map *bmap, void *user)
{
  isl_bool is_uniform;
  isl_schedule *schedule = (isl_schedule *)(user);
  isl_schedule_node *root = isl_schedule_get_root(schedule);
  isl_ctx *ctx = isl_basic_map_get_ctx(bmap);

  /* Get the full schedule and apply the schedule to both the domain and range 
   * of the dependence. Generate the set from this map, and apply a map that 
   * calculate the diff at each dimension to get the dependence vector. 
   * At last, check if the dependence vector is a constant vector.
   */
  isl_union_map *full_sched = isl_schedule_node_get_subtree_schedule_union_map(root);
  isl_union_map *dep_tmp = isl_union_map_apply_domain(
      isl_union_map_from_map(isl_map_from_basic_map(bmap)),
      isl_union_map_copy(full_sched));
  isl_union_map *dep = isl_union_map_apply_range(dep_tmp, full_sched);

  isl_schedule_node_free(root);

  isl_map *dep_map = isl_map_from_union_map(dep);
  isl_basic_map *dep_bmap = isl_basic_map_from_map(isl_map_copy(dep_map)); // TODO

  isl_set *src_dep_domain = isl_map_domain(isl_map_copy(dep_map));
  isl_map *src_dep_domain_map = isl_set_identity(src_dep_domain);
  isl_multi_pw_aff *src_mpa = isl_multi_pw_aff_identity(isl_map_get_space(src_dep_domain_map));
  isl_map_free(src_dep_domain_map);

  isl_set *dest_dep_domain = isl_map_range(dep_map);
  isl_map *dest_dep_domain_map = isl_set_identity(dest_dep_domain);
  isl_multi_pw_aff *dest_mpa = isl_multi_pw_aff_identity(isl_map_get_space(dest_dep_domain_map));
  isl_map_free(dest_dep_domain_map);

  /* Add dims */
  isl_size src_dim = isl_multi_pw_aff_dim(src_mpa, isl_dim_in);
  isl_size dest_dim = isl_multi_pw_aff_dim(dest_mpa, isl_dim_in);
  src_mpa = isl_multi_pw_aff_insert_dims(src_mpa, isl_dim_in, src_dim, dest_dim);
  dest_mpa = isl_multi_pw_aff_insert_dims(dest_mpa, isl_dim_in, 0, src_dim);

  isl_multi_pw_aff *dep_dis_mpa = isl_multi_pw_aff_sub(dest_mpa, src_mpa);

  /* Convert the basic map to basic_set */
  isl_mat *eq_mat = isl_basic_map_equalities_matrix(dep_bmap,
                                                    isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);
  isl_mat *ieq_mat = isl_basic_map_inequalities_matrix(dep_bmap,
                                                       isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);
  isl_basic_set *dep_bset = isl_basic_set_from_constraint_matrices(
      isl_space_domain(isl_multi_pw_aff_get_space(dep_dis_mpa)),
      eq_mat, ieq_mat,
      isl_dim_set, isl_dim_div, isl_dim_param, isl_dim_cst);

  dep_dis_mpa = isl_multi_pw_aff_intersect_domain(dep_dis_mpa,
                                                  isl_set_from_basic_set(dep_bset));

  is_uniform = isl_multi_pw_aff_is_cst(dep_dis_mpa);

  isl_multi_pw_aff_free(dep_dis_mpa);
  isl_basic_map_free(dep_bmap);
  return is_uniform;
}

/* Examine the dependences in the "map". If any of the dependence is non-uniform,
 * print out the detailed information.
 * Return true if all dependences are uniform.
 */
isl_bool is_dep_uniform_wrap(__isl_keep isl_map *map, void *user)
{
  isl_bool is_uniform;
  isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(map);
  for (int i = 0; i < isl_map_n_basic_map(map); i++)
  {
    is_uniform = is_dep_uniform(isl_basic_map_list_get_basic_map(bmap_list, i), user);
    if (is_uniform != isl_bool_true)
    {
      isl_basic_map *dep_i = isl_basic_map_list_get_basic_map(bmap_list, i);
      /* Print out the non-uniform dependence. */
      isl_printer *p = isl_printer_to_file(isl_map_get_ctx(map), stdout);
      p = isl_printer_print_basic_map(p, dep_i);
      printf("\n");
      isl_printer_free(p);
      isl_basic_map_free(dep_i);

      isl_basic_map_list_free(bmap_list);
      return isl_bool_false;
    }
  }
  isl_basic_map_list_free(bmap_list);
  return isl_bool_true;
}

/* Examine if all flow and RAR dependences are uniform in the program. */
isl_bool uniform_dep_check(__isl_keep isl_schedule *schedule, struct ppcg_scop *scop)
{
  isl_union_map *dep_rar = scop->dep_rar;
  //DBGUMAP(stdout, dep_rar, isl_schedule_get_ctx(schedule));

  isl_union_map *dep_flow = scop->dep_flow;

  isl_bool all_flow_dep_uniform = isl_union_map_every_map(dep_flow, &is_dep_uniform_wrap, schedule);
  if (all_flow_dep_uniform != isl_bool_true)
    return isl_bool_false;

  isl_bool all_rar_dep_uniform = isl_union_map_every_map(dep_rar, &is_dep_uniform_wrap, schedule);
  if (all_rar_dep_uniform != isl_bool_true)
    return isl_bool_false;

  return isl_bool_true;
}

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

/* Compute the dependence distance of dependence "dep" under the schedule "schedule".
 */
__isl_give isl_vec *get_dep_dis_at_schedule(__isl_keep isl_basic_map *dep,
                                            __isl_keep isl_schedule *schedule)
{
  isl_schedule_node *root = isl_schedule_get_root(schedule);
  isl_ctx *ctx = isl_basic_map_get_ctx(dep);
  isl_union_map *full_sched = isl_schedule_node_get_subtree_schedule_union_map(root);
  isl_schedule_node_free(root);

  /* Extract the iterator num. */
  int iter_num = 0;
  isl_schedule_foreach_schedule_node_top_down(schedule, &update_depth, &iter_num);

  isl_union_map *dep_sched = isl_union_map_apply_domain(isl_union_map_from_map(isl_map_from_basic_map(isl_basic_map_copy(dep))),
                                                        isl_union_map_copy(full_sched));
  dep_sched = isl_union_map_apply_range(dep_sched, full_sched);

  isl_map *dep_map = isl_map_from_union_map(dep_sched);
  isl_basic_map *dep_bmap = isl_basic_map_from_map(isl_map_copy(dep_map));

  isl_set *src_dep_domain = isl_map_domain(isl_map_copy(dep_map));
  isl_map *src_dep_domain_map = isl_set_identity(src_dep_domain);
  isl_multi_pw_aff *src_mpa = isl_multi_pw_aff_identity(isl_map_get_space(src_dep_domain_map));
  isl_map_free(src_dep_domain_map);

  isl_set *dest_dep_domain = isl_map_range(dep_map);
  isl_map *dest_dep_domain_map = isl_set_identity(dest_dep_domain);
  isl_multi_pw_aff *dest_mpa = isl_multi_pw_aff_identity(isl_map_get_space(dest_dep_domain_map));
  isl_map_free(dest_dep_domain_map);

  /* Add dims. */
  isl_size src_dim = isl_multi_pw_aff_dim(src_mpa, isl_dim_in);
  isl_size dest_dim = isl_multi_pw_aff_dim(dest_mpa, isl_dim_in);
  src_mpa = isl_multi_pw_aff_insert_dims(src_mpa, isl_dim_in, src_dim, dest_dim);
  dest_mpa = isl_multi_pw_aff_insert_dims(dest_mpa, isl_dim_in, 0, src_dim);

  isl_multi_pw_aff *dep_dis_mpa = isl_multi_pw_aff_sub(dest_mpa, src_mpa);

  /* Convert the basic map to basic_set. */
  isl_mat *eq_mat = isl_basic_map_equalities_matrix(dep_bmap,
                                                    isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);
  isl_mat *ieq_mat = isl_basic_map_inequalities_matrix(dep_bmap,
                                                       isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);
  isl_basic_set *dep_bset = isl_basic_set_from_constraint_matrices(
      isl_space_domain(isl_multi_pw_aff_get_space(dep_dis_mpa)),
      eq_mat, ieq_mat,
      isl_dim_set, isl_dim_div, isl_dim_param, isl_dim_cst);

  dep_dis_mpa = isl_multi_pw_aff_intersect_domain(dep_dis_mpa,
                                                  isl_set_from_basic_set(isl_basic_set_copy(dep_bset)));
  isl_space *space = isl_multi_pw_aff_get_space(dep_dis_mpa);
  isl_vec *dep_dis = isl_vec_zero(ctx, isl_space_dim(space, isl_dim_out));
  for (int i = 0; i < isl_vec_size(dep_dis); i++)
  {
    isl_pw_aff *pa = isl_multi_pw_aff_get_pw_aff(dep_dis_mpa, i);
    isl_val *val = isl_pw_aff_eval(pa, isl_basic_set_sample_point(isl_basic_set_copy(dep_bset)));
    dep_dis = isl_vec_set_element_val(dep_dis, i, val);
  }

  isl_space_free(space);
  isl_basic_set_free(dep_bset);
  isl_basic_map_free(dep_bmap);
  isl_multi_pw_aff_free(dep_dis_mpa);

  return dep_dis;
}

/* Compute the dependence distance vector of the dependence under the 
 * partial schedule of the band node. The dependence "dep" is untagged.
 */
__isl_give isl_vec *get_dep_dis_at_node(__isl_keep isl_basic_map *dep, __isl_keep isl_schedule_node *band)
{
  if (isl_schedule_node_get_type(band) != isl_schedule_node_band)
    return NULL;

  isl_multi_union_pw_aff *p_sc = isl_schedule_node_band_get_partial_schedule(band);
  isl_union_pw_multi_aff *contraction = isl_schedule_node_get_subtree_contraction(band);
  p_sc = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(p_sc, contraction);

  int band_w = isl_schedule_node_band_n_member(band);
  isl_vec *dep_dis = isl_vec_zero(isl_basic_map_get_ctx(dep), band_w);
  for (int i = 0; i < band_w; i++)
  {
    isl_union_pw_aff *p_sc_hyp = isl_multi_union_pw_aff_get_union_pw_aff(p_sc, i);
    /* Obtain the schedule for the src statement. */
    isl_space *space = isl_basic_map_get_space(dep);
    isl_space *src_space = isl_space_domain(isl_space_copy(space));
    isl_space *dest_space = isl_space_range(space);

    isl_pw_aff *src_sc = NULL;
    isl_pw_aff_list *p_sc_hyp_list = isl_union_pw_aff_get_pw_aff_list(p_sc_hyp);
    for (int j = 0; j < isl_union_pw_aff_n_pw_aff(p_sc_hyp); j++)
    {
      isl_pw_aff *single_sc = isl_pw_aff_list_get_pw_aff(p_sc_hyp_list, j);
      isl_space *single_sc_stmt = isl_space_domain(isl_pw_aff_get_space(single_sc));

      if (isl_space_is_equal(src_space, single_sc_stmt))
      {
        isl_space_free(single_sc_stmt);
        src_sc = single_sc;
        break;
      }
      isl_pw_aff_free(single_sc);
      isl_space_free(single_sc_stmt);
    }
    isl_pw_aff_list_free(p_sc_hyp_list);
    isl_space_free(src_space);

    /* Obtain the schedule for the dest statement. */
    isl_pw_aff *dest_sc = NULL;
    p_sc_hyp_list = isl_union_pw_aff_get_pw_aff_list(p_sc_hyp);
    for (int j = 0; j < isl_union_pw_aff_n_pw_aff(p_sc_hyp); j++)
    {
      isl_pw_aff *single_sc = isl_pw_aff_list_get_pw_aff(p_sc_hyp_list, j);
      isl_space *single_sc_stmt = isl_space_domain(isl_pw_aff_get_space(single_sc));

      if (isl_space_is_equal(dest_space, single_sc_stmt))
      {
        isl_space_free(single_sc_stmt);
        dest_sc = single_sc;
        break;
      }
      isl_pw_aff_free(single_sc);
      isl_space_free(single_sc_stmt);
    }
    isl_pw_aff_list_free(p_sc_hyp_list);
    isl_space_free(dest_space);

    /* Compute the dependence distance at the current hyperplane. */
    /* Step 1: Extend the scheduling function. */
    isl_size src_sc_dim = isl_pw_aff_dim(src_sc, isl_dim_in);
    isl_size dest_sc_dim = isl_pw_aff_dim(dest_sc, isl_dim_in);
    src_sc = isl_pw_aff_insert_dims(src_sc, isl_dim_in, src_sc_dim, dest_sc_dim);
    dest_sc = isl_pw_aff_insert_dims(dest_sc, isl_dim_in, 0, src_sc_dim);
    for (int j = 0; j < dest_sc_dim; j++)
    {
      isl_pw_aff_set_dim_id(src_sc, isl_dim_in, src_sc_dim + j, isl_pw_aff_get_dim_id(dest_sc, isl_dim_in, src_sc_dim + j));
    }
    for (int j = 0; j < src_sc_dim; j++)
    {
      isl_pw_aff_set_dim_id(dest_sc, isl_dim_in, j, isl_pw_aff_get_dim_id(src_sc, isl_dim_in, j));
    }

    isl_pw_aff *dis_sc = isl_pw_aff_sub(dest_sc, src_sc);

    /* Step 2: Convert the basic_map into basic_set. */
    isl_mat *eq_mat = isl_basic_map_equalities_matrix(dep,
                                                      isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);
    isl_mat *ieq_mat = isl_basic_map_inequalities_matrix(dep,
                                                         isl_dim_in, isl_dim_out, isl_dim_div, isl_dim_param, isl_dim_cst);

    isl_basic_set *dep_set = isl_basic_set_from_constraint_matrices(
        isl_space_domain(isl_pw_aff_get_space(dis_sc)),
        eq_mat, ieq_mat,
        isl_dim_set, isl_dim_div, isl_dim_param, isl_dim_cst);

    /* Step 3: Intersect the scheduling function with the domain. */
    isl_pw_aff *dis = isl_pw_aff_intersect_domain(dis_sc, isl_set_from_basic_set(isl_basic_set_copy(dep_set)));
    isl_val *val = isl_pw_aff_eval(dis, isl_basic_set_sample_point(dep_set));
    dep_dis = isl_vec_set_element_val(dep_dis, i, val);

    isl_union_pw_aff_free(p_sc_hyp);
  }

  isl_multi_union_pw_aff_free(p_sc);
  return dep_dis;
}

/* Interchange the loop at "level1" and "level2" in the schedule node and 
 * return the new schedule. */
__isl_give isl_schedule_node *loop_interchange_at_node(
  __isl_take isl_schedule_node *node, isl_size level1, isl_size level2)
{
  /* Obtain the partial schedule of the node. */
  isl_multi_union_pw_aff *sc = isl_schedule_node_band_get_partial_schedule(node);

  /* Exchange the schedule at level1 and level2. */
  isl_multi_union_pw_aff *new_sc = isl_multi_union_pw_aff_copy(sc);
  new_sc = isl_multi_union_pw_aff_set_union_pw_aff(new_sc, level1, isl_multi_union_pw_aff_get_union_pw_aff(sc, level2));
  new_sc = isl_multi_union_pw_aff_set_union_pw_aff(new_sc, level2, isl_multi_union_pw_aff_get_union_pw_aff(sc, level1));

  /* Insert a new schedule node with the new schedule. */
  struct autosa_node_band_prop *prop = extract_node_band_prop(node);
  node = isl_schedule_node_insert_partial_schedule(node, new_sc);

  /* Update the properties of the new node. */
  node = isl_schedule_node_band_set_permutable(node, 1);
  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
  {
    node = isl_schedule_node_band_member_set_coincident(node, i, prop->coincident[i]);
  }
  node = isl_schedule_node_band_member_set_coincident(node, level1, prop->coincident[level2]);
  node = isl_schedule_node_band_member_set_coincident(node, level2, prop->coincident[level1]);
  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
  {
    node = isl_schedule_node_band_member_set_pe_opt(node, i, prop->pe_opt[i]);
  }
  node = isl_schedule_node_band_member_set_pe_opt(node, level1, prop->pe_opt[level2]);
  node = isl_schedule_node_band_member_set_pe_opt(node, level2, prop->pe_opt[level1]);

  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
  {
    node = isl_schedule_node_band_member_set_space_time(node, i, prop->space_time[i]);
  }
  node = isl_schedule_node_band_member_set_space_time(node, level1, prop->space_time[level2]);
  node = isl_schedule_node_band_member_set_space_time(node, level2, prop->space_time[level1]);

  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
  {
    node = isl_schedule_node_band_member_set_sched_pos(node, i, prop->sched_pos[i]);
  }
  node = isl_schedule_node_band_member_set_sched_pos(node, level1, prop->sched_pos[level2]);
  node = isl_schedule_node_band_member_set_sched_pos(node, level2, prop->sched_pos[level1]);
  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++) 
  {
    node = isl_schedule_node_band_member_set_iter(node, i, prop->iter[i]);    
  }
  node = isl_schedule_node_band_member_set_iter(node, level1, prop->iter[level2]);
  node = isl_schedule_node_band_member_set_iter(node, level2, prop->iter[level1]);

  autosa_node_band_prop_free(prop);

  /* Delete the old node after the current node */
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_delete(node);

  node = isl_schedule_node_parent(node);
  isl_multi_union_pw_aff_free(sc);
  
  return node;

//  /* Obtain the schedule from the schedule node. */
//  isl_schedule *schedule = isl_schedule_node_get_schedule(node);
//
//  isl_schedule_node_free(node);
//  isl_multi_union_pw_aff_free(sc);
//
//  return schedule;
}

/* Examine if the node is a permutable band node. If so,
 * since the schedule tree is visited top-down,
 * return such a node immediately.
 */
static isl_bool is_outermost_permutable_node_update(
    __isl_keep isl_schedule_node *node, void *user)
{
  isl_schedule_node **t_node = (isl_schedule_node **)(user);
  if (!node)
    return isl_bool_error;

  if (is_permutable_node(node) == isl_bool_true)
  {
    *t_node = isl_schedule_node_copy(node);
    return isl_bool_false;
  }
  else
  {
    return isl_bool_true;
  }

  return isl_bool_true;
}

/* Extract the outermost permutable band node from the schedule tree.
 * When there are multiple nodes at the same level, extract the first one.
 */
__isl_give isl_schedule_node *get_outermost_permutable_node(
    __isl_keep isl_schedule *schedule)
{
  isl_schedule_node *root = isl_schedule_get_root(schedule);
  isl_schedule_node *t_node = NULL;
  isl_schedule_node_foreach_descendant_top_down(root,
                                                &is_outermost_permutable_node_update, &t_node);

  isl_schedule_node_free(root);
  return t_node;
}

/* Examines if the node is a permutable band node. If so,
 * since the schedule tree is visited bottom-up,
 * return the node immediately.
 */
static isl_bool is_innermost_permutable_node_update(__isl_keep isl_schedule_node *node, void *user)
{
  isl_schedule_node **t_node = (isl_schedule_node **)(user);
  if (!node)
    return isl_bool_error;

  if (is_permutable_node(node) == isl_bool_true)
  {
    /* Check if there is any other band below it. */
    isl_schedule_node *new_node = isl_schedule_node_get_child(node, 0);
    isl_bool no_inner_band = isl_schedule_node_every_descendant(new_node,
                                                                &no_permutable_node, NULL);
    if (no_inner_band)
    {
      if (*t_node == NULL)
        *t_node = isl_schedule_node_copy(node);
    }
    isl_schedule_node_free(new_node);
  }

  return isl_bool_true;
}

/* Extract the innermost permutable band node from the schedule tree.
 * When there are multiple nodes at the same level, extract the first one.
 */
__isl_give isl_schedule_node *get_innermost_permutable_node(__isl_keep isl_schedule *schedule)
{
  isl_schedule_node *root = isl_schedule_get_root(schedule);
  isl_schedule_node *t_node = NULL;
  isl_schedule_node_foreach_descendant_top_down(root,
                                                &is_innermost_permutable_node_update, &t_node);

  isl_schedule_node_free(root);
  return t_node;
}

/* Tile "band" with tile size specified by "sizes".
 */
__isl_give isl_schedule_node *tile_band(
    __isl_take isl_schedule_node *node, __isl_take isl_multi_val *sizes)
{
  isl_ctx *ctx = isl_schedule_node_get_ctx(node);
  int scale_tile;
  int shift_point;

  scale_tile = isl_options_get_tile_scale_tile_loops(ctx);
  isl_options_set_tile_scale_tile_loops(ctx, 0);
  shift_point = isl_options_get_tile_shift_point_loops(ctx);
  isl_options_set_tile_shift_point_loops(ctx, 1);

  node = isl_schedule_node_band_tile(node, sizes);

  isl_options_set_tile_scale_tile_loops(ctx, scale_tile);
  isl_options_set_tile_shift_point_loops(ctx, shift_point);

  return node;
}

/* Tile "band" with tile size specified by "sizes".
 *
 * If the tile size at the given position, is "-1", the loop
 * will not be tiled. Two band nodes are generated. The first band
 * contains the tile loops and the untiled loops. The second band
 * contains the point loops.
 */
__isl_give isl_schedule_node *autosa_tile_band(
    __isl_take isl_schedule_node *node, __isl_keep int *sizes)
{
  int full_tile = 1;
  int n;

  /* Examine of the band needs to be completedly tiled. */
  n = isl_schedule_node_band_n_member(node);
  for (int i = 0; i < n; i++)
  {
    if (sizes[i] == -1)
    {
      full_tile = 0;
      break;
    }
  }

  if (full_tile)
  {
    isl_multi_val *tile_sizes;
    tile_sizes = construct_band_tile_sizes(node, sizes);
    node = tile_band(node, isl_multi_val_copy(tile_sizes));
    /* Reset the space_time in the tile band */
    for (int i = 0; i < n; i++)
    {
      node = isl_schedule_node_band_member_set_space_time(node, i, autosa_loop_time);
    }
    isl_multi_val_free(tile_sizes);
  }
  else
  {
    // TODO: tile on demand
    isl_die(isl_schedule_node_get_ctx(node), isl_error_unsupported,
            "on-demand tiling not supported", return node);
  }

  return node;
}

/* Given two nested nodes,
 * N1
 * |
 * N2
 * Merge them into one node.
 * N
 * The input "node" points to N1.
 * Return a pointer to N.
 */
static __isl_give isl_schedule_node *autosa_node_merge(
    __isl_take isl_schedule_node *node)
{
  if (isl_schedule_node_n_children(node) == 0 || isl_schedule_node_n_children(node) > 1)
    return node;

  isl_schedule_node *parent = node;
  isl_schedule_node *child = isl_schedule_node_child(isl_schedule_node_copy(node), 0);
  if (isl_schedule_node_get_type(parent) != isl_schedule_node_band ||
      isl_schedule_node_get_type(child) != isl_schedule_node_band)
    return node;

  /* Save the node properties. */
  struct autosa_node_band_prop *parent_prop = extract_node_band_prop(parent);
  struct autosa_node_band_prop *child_prop = extract_node_band_prop(child);

  /* Merge the partial schedules of two nodes. */
  isl_union_pw_aff_list *upa_list = isl_union_pw_aff_list_alloc(
      isl_schedule_node_get_ctx(node), 0);
  isl_space *parent_space = isl_multi_union_pw_aff_get_space(parent_prop->mupa);
  isl_space *child_space = isl_multi_union_pw_aff_get_space(child_prop->mupa);

  for (int i = 0; i < parent_prop->n_member; i++)
  {
    isl_union_pw_aff *upa = isl_multi_union_pw_aff_get_union_pw_aff(parent_prop->mupa, i);
    upa_list = isl_union_pw_aff_list_add(
        upa_list, upa);
  }
  for (int i = 0; i < child_prop->n_member; i++)
  {
    isl_union_pw_aff *upa = isl_multi_union_pw_aff_get_union_pw_aff(child_prop->mupa, i);
    upa_list = isl_union_pw_aff_list_add(
        upa_list, upa);
  }

  isl_space *mupa_space = isl_space_add_dims(parent_space, isl_dim_set, isl_space_dim(child_space, isl_dim_set));
  isl_space_free(child_space);

  isl_multi_union_pw_aff *mupa = isl_multi_union_pw_aff_from_union_pw_aff_list(
      mupa_space,
      upa_list);

  /* Insert one new node. */
  node = isl_schedule_node_insert_partial_schedule(node, mupa);

  /* Restore the node properties. */
  node = isl_schedule_node_band_set_permutable(node, 1);
  for (int i = 0; i < parent_prop->n_member; i++)
  {
    node = isl_schedule_node_band_member_set_coincident(
        node, i, parent_prop->coincident[i]);
  }
  for (int i = 0; i < parent_prop->n_member; i++)
  {
    node = isl_schedule_node_band_member_set_space_time(
        node, i, parent_prop->space_time[i]);
    node = isl_schedule_node_band_member_set_pe_opt(
        node, i, parent_prop->pe_opt[i]);
    node = isl_schedule_node_band_member_set_sched_pos(
        node, i, parent_prop->sched_pos[i]);
    node = isl_schedule_node_band_member_set_iter(
        node, i, parent_prop->iter[i]);
  }
  for (int i = 0; i < child_prop->n_member; i++)
  {
    node = isl_schedule_node_band_member_set_coincident(
        node, i + parent_prop->n_member, child_prop->coincident[i]);
  }
  for (int i = 0; i < child_prop->n_member; i++)
  {
    node = isl_schedule_node_band_member_set_space_time(
        node, i + parent_prop->n_member, child_prop->space_time[i]);
    node = isl_schedule_node_band_member_set_pe_opt(
        node, i + parent_prop->n_member, child_prop->pe_opt[i]);
    node = isl_schedule_node_band_member_set_sched_pos(
        node, i + parent_prop->n_member, child_prop->sched_pos[i]);
    node = isl_schedule_node_band_member_set_iter(
        node, i + parent_prop->n_member, child_prop->iter[i]);
  }

  /* Delete the old nodes. */
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_delete(node);
  node = isl_schedule_node_delete(node);
  node = isl_schedule_node_parent(node);

  free(parent_prop->coincident);
  free(parent_prop->pe_opt);
  free(parent_prop->space_time);
  free(parent_prop->sched_pos);  
  isl_multi_union_pw_aff_free(parent_prop->mupa);
  free(parent_prop);
  free(child_prop->coincident);
  free(child_prop->pe_opt);
  free(child_prop->space_time);  
  free(child_prop->sched_pos);  
  isl_multi_union_pw_aff_free(child_prop->mupa);
  free(child_prop);
  isl_schedule_node_free(child);

  return node;
}

/* Tile the loop at the "pos" position of the band with the size "tile_size".
 * The original band
 * B
 * is first splitted to
 * B1
 * |
 * p
 * |
 * B2
 * The loop p is then tiled, and four band nodes are generated.
 * B1
 * |
 * p_tile
 * |
 * B2
 * |
 * p_point
 * The first three bands are then merged together.
 * B'
 * |
 * p_point
 * A pointer to B' is returned.
 */
__isl_give isl_schedule_node *autosa_node_band_tile_loop(
    __isl_take isl_schedule_node *node, int tile_size, int pos)
{
  isl_multi_val *tile_sizes;
  int n = isl_schedule_node_band_n_member(node);
  int size[1];

  size[0] = tile_size;
  node = isl_schedule_node_band_split(node, pos);
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_band_split(node, 1);

  tile_sizes = construct_band_tile_sizes(node, size);
  node = tile_band(node, isl_multi_val_copy(tile_sizes));
  isl_multi_val_free(tile_sizes);

  /* Swap the order of the point band and the next band. */
  node = isl_schedule_node_child(node, 0);
  node = autosa_node_interchange(node);

  /* Merge the first three bands. */
  node = isl_schedule_node_parent(node);
  node = autosa_node_merge(node);
  node = isl_schedule_node_parent(node);
  node = autosa_node_merge(node);

  return node;
}

/* Reset the pe_opt properties of all the band opts back to default. */
__isl_give isl_schedule_node *clear_pe_opt_prop(
    __isl_take isl_schedule_node *node, void *user)
{
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  {
    for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
    {
      node = isl_schedule_node_band_member_set_pe_opt(node, i,
                                                      autosa_loop_default);
    }
  }

  return node;
}

/* Extract the partial schedule, restore the rest band node properties from "prop". 
 */
__isl_give isl_schedule_node *restore_node_band_prop(
    __isl_take isl_schedule_node *node,
    __isl_take struct autosa_node_band_prop *prop)
{
  node = isl_schedule_node_band_set_permutable(node, prop->permutable);
  for (int i = 0; i < prop->n_member; i++)
  {
    node = isl_schedule_node_band_member_set_coincident(node, i, prop->coincident[i]);
  }
  for (int i = 0; i < prop->n_member; i++)
  {
    node = isl_schedule_node_band_member_set_space_time(node, i, prop->space_time[i]);
    node = isl_schedule_node_band_member_set_pe_opt(node, i, prop->pe_opt[i]);
    node = isl_schedule_node_band_member_set_sched_pos(node, i, prop->sched_pos[i]);
    node = isl_schedule_node_band_member_set_iter(node, i, prop->iter[i]);
  }

  free(prop->coincident);
  free(prop->pe_opt);
  free(prop->space_time);
  free(prop->sched_pos);  
  isl_multi_union_pw_aff_free(prop->mupa);
  free(prop);

  return node;
}

/* Given two nested nodes,
 * N1
 * |
 * N2
 * Interchange the two nodes to
 * N2
 * |
 * N1
 * The input "node" points to N1.
 * return a pointer to node N2.
 */
__isl_give isl_schedule_node *autosa_node_interchange(
    __isl_take isl_schedule_node *node)
{
  if (isl_schedule_node_n_children(node) == 0 || isl_schedule_node_n_children(node) > 1)
  {
    return node;
  }

  /* Save the current node. */
  struct autosa_node_band_prop *prop = extract_node_band_prop(node);

  /* Delete the current node. */
  node = isl_schedule_node_delete(node);

  /* Insert the old node. */
  node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_insert_partial_schedule(node,
                                                   isl_multi_union_pw_aff_copy(prop->mupa));

  /* Restore the node properties. */
  node = restore_node_band_prop(node, prop);
  node = isl_schedule_node_parent(node);

  return node;
}

/* Given two nested nodes,
 * N2
 * |
 * N1
 * Interchange the two nodes to
 * N1
 * |
 * N2
 * The input "node" points to N1.
 * Return a pointer to node N1.
 * Besides, currently we only support interchanging band nodes and mark nodes.
 */
__isl_give isl_schedule_node *autosa_node_interchange_up(
    __isl_take isl_schedule_node *node)
{
  enum isl_schedule_node_type t;
  enum isl_schedule_node_type parent_t;
  isl_schedule_node *parent_node;
  struct autosa_node_band_prop *prop;
  isl_id *id;

  if (!isl_schedule_node_has_parent(node))
  {
    return node;
  }
  t = isl_schedule_node_get_type(node);
  if (!(t == isl_schedule_node_band || t == isl_schedule_node_mark))
  {
    isl_die(isl_schedule_node_get_ctx(node), isl_error_unsupported,
            "only band and mark nodes are supported", return node);
  }
  parent_node = isl_schedule_node_parent(isl_schedule_node_copy(node));
  parent_t = isl_schedule_node_get_type(parent_node);
  if (!(parent_t == isl_schedule_node_band || parent_t == isl_schedule_node_mark))
  {
    isl_die(isl_schedule_node_get_ctx(node), isl_error_unsupported,
            "only band and mark nodes are supported", return node);
  }
  isl_schedule_node_free(parent_node);

  /* Save the current node. */
  if (t == isl_schedule_node_band)
  {
    prop = extract_node_band_prop(node);
  }
  else if (t == isl_schedule_node_mark)
  {
    id = isl_schedule_node_mark_get_id(node);
  }

  /* Delete the current node. */
  node = isl_schedule_node_delete(node);

  /* Insert the old node. */
  node = isl_schedule_node_parent(node);
  if (t == isl_schedule_node_band)
  {
    node = isl_schedule_node_insert_partial_schedule(node,
                                                     isl_multi_union_pw_aff_copy(prop->mupa));
    node = restore_node_band_prop(node, prop);
  }
  else if (t == isl_schedule_node_mark)
  {
    node = isl_schedule_node_insert_mark(node, id);
  }

  return node;
}

/* If the "node" is a permutable band node, return false.
 */
isl_bool no_permutable_node(__isl_keep isl_schedule_node *node, void *user)
{
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    return isl_bool_false;
  else
    return isl_bool_true;
}

/* If any band member is non-parallel, return false. 
 */
isl_bool all_parallel_node(__isl_keep isl_schedule_node *node, void *user)
{
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  {
    int n = isl_schedule_node_band_n_member(node);
    for (int i = 0; i < n; i++)
    {
      if (!isl_schedule_node_band_member_get_coincident(node, i))
        return isl_bool_false;
    }
  }
  return isl_bool_true;
}

/* This function tests if the loops above the "array" mark carry any flow
 * dependence that is assoicated with the I/O group "group".
 */
isl_bool is_flow_dep_carried_by_array_part_loops(__isl_keep isl_schedule *schedule,
                                                 struct autosa_array_ref_group *group, struct autosa_kernel *kernel)
{
  isl_bool carried = isl_bool_false;
  isl_schedule_node *node;
  isl_union_map *umap;

  if (!group->local_array->array_type == AUTOSA_INT_ARRAY)
    return carried;
  node = isl_schedule_get_root(schedule);
  node = autosa_tree_move_down_to_array(node, kernel->core);
  while (node && isl_schedule_node_has_parent(node))
  {
    if (autosa_tree_node_is_kernel(node))
      break;
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
      umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
      for (int i = 0; i < group->n_ref; i++)
      {
        struct autosa_stmt_access *ref = group->refs[i];
        for (int j = 0; j < ref->n_io_info; j++)
        {
          struct autosa_io_info *io_info = ref->io_info[j];
          if (io_info->io_type == group->io_type &&
              !isl_vec_cmp(io_info->dir, group->dir))
          {
            isl_map *test;
            isl_map *schedule_dep;
            int dim;
            int is_parallel;

            isl_union_map *dep = isl_union_map_from_map(
                isl_map_factor_domain(
                    isl_map_from_basic_map(isl_basic_map_copy(io_info->dep->isl_dep))));
            dep = isl_union_map_apply_range(dep, isl_union_map_copy(umap));
            dep = isl_union_map_apply_domain(dep, isl_union_map_copy(umap));
            if (isl_union_map_is_empty(dep))
            {
              isl_union_map_free(dep);
              break;
            }
            schedule_dep = isl_map_from_union_map(dep);
            test = isl_map_universe(isl_map_get_space(schedule_dep));
            dim = isl_schedule_node_band_n_member(node);
            for (int n = 0; n < dim; n++)
            {
              test = isl_map_equate(test, isl_dim_in, n, isl_dim_out, n);
            }
            is_parallel = isl_map_is_subset(schedule_dep, test);
            isl_map_free(schedule_dep);
            isl_map_free(test);

            if (!is_parallel)
            {
              /* Dependence is carried by the array part loops. */
              carried = isl_bool_true;
              break;
            }
          }
        }
      }
      isl_union_map_free(umap);
    }
    node = isl_schedule_node_parent(node);
  }

  isl_schedule_node_free(node);
  return carried;
}

/* Test if the dependence is carried by the current schedule node. */
int is_dep_carried_by_node(__isl_keep isl_basic_map *dep, __isl_keep isl_schedule_node *node)
{
  if (!node || isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return -1;
  if (isl_schedule_node_band_n_member(node) != 1)
    return -1;
  if (!dep)
    return -1;

  isl_union_map *umap, *umap_dep;
  isl_map *map_dep, *test;
  int is_carried;

  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  umap_dep = isl_union_map_from_map(isl_map_factor_domain(isl_map_from_basic_map(isl_basic_map_copy(dep))));
  umap_dep = isl_union_map_apply_range(umap_dep, isl_union_map_copy(umap));
  umap_dep = isl_union_map_apply_domain(umap_dep, umap);
  if (isl_union_map_is_empty(umap_dep)) {
    isl_union_map_free(umap_dep);
    return -1;
  }
  map_dep = isl_map_from_union_map(umap_dep);
  test = isl_map_universe(isl_map_get_space(map_dep));
  test = isl_map_equate(test, isl_dim_in, 0, isl_dim_out, 0);
  is_carried = !isl_map_is_subset(map_dep, test);
  isl_map_free(map_dep);
  isl_map_free(test);
  
  return is_carried;
}

struct insert_node_at_depth_data {
  isl_multi_union_pw_aff *mupa;
  struct autosa_node_band_prop *prop;
  int depth;
};

static isl_bool has_inserted_mark(__isl_keep isl_schedule_node *node, void *user)
{
  if (is_marked(node, "inserted"))
    return isl_bool_false;
  
  return isl_bool_true;
}

static __isl_give isl_schedule_node *delete_inserted_mark(__isl_take isl_schedule_node *node, void *user)
{
  if (is_marked(node, "inserted"))
    node = isl_schedule_node_delete(node);
  
  return node;
}

static isl_bool has_band_node(__isl_keep isl_schedule_node *node, void *user)
{
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)    
    return isl_bool_false;
  
  return isl_bool_true;
}

/* Insert the node at the "depth" position. To prevent inserting the node 
 * multiple times, a "inserted" mark will be inserted before the node.
 * After the insertion, we will delete this "inserted" mark.
 * This function is not complete, might have bugs.
 */
static __isl_give isl_schedule_node *insert_node_at_depth(
  __isl_take isl_schedule_node *node, void *user)
{
  struct insert_node_at_depth_data *data = (struct insert_node_at_depth_data *)user;
  isl_id *id;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return node;

  /* Examine the subtree contains the "inserted" mark node */
  if (!isl_schedule_node_every_descendant(node, &has_inserted_mark, NULL)) {    
    return node;
  }

  if (isl_schedule_node_get_schedule_depth(node) < data->depth) {
    /* Split the node and insert at certain position. However, 
     * currently, we simply put it below the cureretn node.
     * TODO: fix it
     */
    node = isl_schedule_node_child(node, 0);
  }

  if (isl_schedule_node_get_schedule_depth(node) != data->depth) {
//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif        
    return node;
  }

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  /* Check if the node is right under the "latency" node.
   * If true, move the node to the mark node.
   */
  node = isl_schedule_node_parent(node);
  if (!is_marked(node, "latency"))
    node = isl_schedule_node_child(node, 0);
  node = isl_schedule_node_parent(node);
  if (!is_marked(node, "simd"))
    node = isl_schedule_node_child(node, 0);

  /* Insert the node at current position */
  node = isl_schedule_node_insert_partial_schedule(node, isl_multi_union_pw_aff_copy(data->mupa));
  node = isl_schedule_node_band_set_permutable(node, data->prop->permutable);
  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++) {
    node = isl_schedule_node_band_member_set_coincident(node, i, data->prop->coincident[i]);
    node = isl_schedule_node_band_member_set_pe_opt(node, i, data->prop->pe_opt[i]);
    node = isl_schedule_node_band_member_set_space_time(node, i, data->prop->space_time[i]);
    node = isl_schedule_node_band_member_set_sched_pos(node, i, data->prop->sched_pos[i]);
    node = isl_schedule_node_band_member_set_iter(node, i, data->prop->iter[i]);
  }

  /* Insert a "inserted" mark */
  id = isl_id_alloc(isl_schedule_node_get_ctx(node), "inserted", NULL);
  node = isl_schedule_node_insert_mark(node, id);

//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif

  return node;
}

/* This function sinks the node to the schedule depth "depth". */
__isl_give isl_schedule_node *autosa_node_sink_to_depth(
  __isl_take isl_schedule_node *node, int depth)
{
  isl_multi_union_pw_aff *mupa;
  struct autosa_node_band_prop *prop;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return node;
  
  mupa = isl_schedule_node_band_get_partial_schedule(node);
  prop = extract_node_band_prop(node);
  /* Delete the current node */
  node = isl_schedule_node_delete(node);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif  
  struct insert_node_at_depth_data data = {mupa, prop, depth};
  node = isl_schedule_node_map_descendant_bottom_up(node, &insert_node_at_depth, &data);
//#ifdef _DEBUG
//  DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));
//#endif
  /* Delete the inserted mark */
  node = isl_schedule_node_map_descendant_bottom_up(node, &delete_inserted_mark, NULL);

  autosa_node_band_prop_free(prop);
  isl_multi_union_pw_aff_free(mupa);

  return node;
}

struct sink_node_to_mark_data {
  isl_multi_union_pw_aff *mupa;
  struct autosa_node_band_prop *prop;
  const char *name;  
  bool inserted;
};

static __isl_give isl_schedule_node *sink_node_to_mark(
  __isl_take isl_schedule_node *node, void *user)
{
  struct sink_node_to_mark_data *data = (struct sink_node_to_mark_data *)user;
  isl_id *id;
  isl_schedule_node *node_tmp;  

  //if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
  //  return node;
  
  /* Examine the subtree contains the "inserted" mark node */
  if (!isl_schedule_node_every_descendant(node, &has_inserted_mark, NULL)) {    
    return node;
  }

  //DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node));

  if (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
    /* If this is a band node, then insert it under the band node. */
    node = isl_schedule_node_child(node, 0);
  } else if (isl_schedule_node_get_type(node) == isl_schedule_node_leaf) {
    /* If this is a leaf node, check:
     * 1. There is a band node in the parent tree.
     * 2. There is a sequence node, and there is no bands under any children.
     * If the above criteria meet, we will skip this node because we will insert the node in the other positions. 
     */    
    bool insert = 1;
    node_tmp = isl_schedule_node_copy(node);
    //DBGSCHDNODE(stdout, node_tmp, isl_schedule_node_get_ctx(node_tmp));
    while (!autosa_tree_node_is_mark(node_tmp, "stop") && isl_schedule_node_has_parent(node_tmp)) {
      node_tmp = isl_schedule_node_parent(node_tmp);
      if (isl_schedule_node_get_type(node_tmp) == isl_schedule_node_band) {
        insert = 0;        
        break;
      }
      if (isl_schedule_node_get_type(node_tmp) == isl_schedule_node_sequence) {
        // TODO: We haven't considered other nodes such as set yet.
        int n_child = 0;
        for (n_child = 0; n_child < isl_schedule_node_n_children(node_tmp); n_child++) {
          isl_schedule_node *node_child = isl_schedule_node_child(isl_schedule_node_copy(node_tmp), n_child);
          /* Check if there is any band node under this child node. */
          if (!isl_schedule_node_every_descendant(node_child, &has_band_node, NULL)) {                        
            isl_schedule_node_free(node_child);
            break;
          }          
          isl_schedule_node_free(node_child);
        }
        if (n_child == isl_schedule_node_n_children(node_tmp)) {
          insert = 0;          
          break;
        }        
      } 
    }    
    isl_schedule_node_free(node_tmp);
    if (insert == 0)
      return node;
  } else {
    return node;
  }

  //node = isl_schedule_node_child(node, 0);
  /* Check if the node is under any exisiting "name" node.
   * If true, move the node to the mark node.
   */
  int mark_cnt = 0;
  node_tmp = isl_schedule_node_copy(node);
  while (isl_schedule_node_has_parent(node_tmp)) {
    node_tmp = isl_schedule_node_parent(node_tmp);
    if (is_marked(node_tmp, data->name))
      mark_cnt++;
  }
  isl_schedule_node_free(node_tmp);
  
  while (mark_cnt > 0) {
    node = isl_schedule_node_parent(node);
    if (is_marked(node, data->name))
      mark_cnt--;
  }

  /* Insert the node at current position */
  node = isl_schedule_node_insert_partial_schedule(node, isl_multi_union_pw_aff_copy(data->mupa));
  node = isl_schedule_node_band_set_permutable(node, data->prop->permutable);
  for (int i = 0; i < isl_schedule_node_band_n_member(node); i++) {
    node = isl_schedule_node_band_member_set_coincident(node, i, data->prop->coincident[i]);
    node = isl_schedule_node_band_member_set_pe_opt(node, i, data->prop->pe_opt[i]);
    node = isl_schedule_node_band_member_set_space_time(node, i, data->prop->space_time[i]);
    node = isl_schedule_node_band_member_set_sched_pos(node, i, data->prop->sched_pos[i]);
    node = isl_schedule_node_band_member_set_iter(node, i, data->prop->iter[i]);
  }

  /* Insert a "name" mark */
  id = isl_id_alloc(isl_schedule_node_get_ctx(node), data->name, NULL);
  node = isl_schedule_node_insert_mark(node, id);

  /* Insert a "inserted" mark */
  id = isl_id_alloc(isl_schedule_node_get_ctx(node), "inserted", NULL);
  node = isl_schedule_node_insert_mark(node, id);
  
  data->inserted = true;

  return node;
}

/* Sink the node innermost, but above the mark name with "name" if set. */
__isl_give isl_schedule_node *autosa_node_sink_to_mark(
  __isl_take isl_schedule_node *node, const char *name)
{
  isl_multi_union_pw_aff *mupa;
  struct autosa_node_band_prop *prop;
  isl_id *id;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return node;

  /* Insert a stop mark. */
  id = isl_id_alloc(isl_schedule_node_get_ctx(node), "stop", NULL);
  node = isl_schedule_node_insert_mark(node, id);
  node = isl_schedule_node_child(node, 0);

  mupa = isl_schedule_node_band_get_partial_schedule(node);
  prop = extract_node_band_prop(node);
  /* Delete the current node */
  node = isl_schedule_node_delete(node);

  struct sink_node_to_mark_data data = {mupa, prop, name, false};
  node = isl_schedule_node_map_descendant_bottom_up(node, &sink_node_to_mark, &data);
  if (!data.inserted) {
    
    /* Insert the node at current position */
    node = isl_schedule_node_insert_partial_schedule(node, isl_multi_union_pw_aff_copy(data.mupa));
    node = isl_schedule_node_band_set_permutable(node, data.prop->permutable);
    for (int i = 0; i < isl_schedule_node_band_n_member(node); i++) {
      node = isl_schedule_node_band_member_set_coincident(node, i, data.prop->coincident[i]);
      node = isl_schedule_node_band_member_set_pe_opt(node, i, data.prop->pe_opt[i]);
      node = isl_schedule_node_band_member_set_space_time(node, i, data.prop->space_time[i]);
      node = isl_schedule_node_band_member_set_sched_pos(node, i, data.prop->sched_pos[i]);
      node = isl_schedule_node_band_member_set_iter(node, i, data.prop->iter[i]);
    }

    /* Insert a "name" mark */
    id = isl_id_alloc(isl_schedule_node_get_ctx(node), data.name, NULL);
    node = isl_schedule_node_insert_mark(node, id);
  }
  /* Delete the "inserted" mark */
  node = isl_schedule_node_map_descendant_bottom_up(node, &delete_inserted_mark, NULL);
  
  /* Delete the stop mark */
  node = isl_schedule_node_parent(node);
  node = isl_schedule_node_delete(node);

  autosa_node_band_prop_free(prop);
  isl_multi_union_pw_aff_free(mupa);

  return node;
}

/* Reorder the schedule dims in the band based on the dependence distance.
 */
__isl_give isl_schedule_node *reorder_band_by_dep_dis(__isl_take isl_schedule_node *node)
{
  int n = isl_schedule_node_band_n_member(node);
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      int sched_pos = isl_schedule_node_band_member_get_sched_pos(node, j);
      if (sched_pos == i) {
        /* Permute the j-th dim to i-th dim */
        node = loop_interchange_at_node(node, j, i);
      }
    }
  }

  return node;
}

static __isl_give isl_schedule_node *band_sched_pos_setup(
  __isl_take isl_schedule_node *node, void *user)
{
  if (!node)
    return NULL;

  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
  {
    int n = isl_schedule_node_band_n_member(node);
    for (int i = 0; i < n; i++) {
      node = isl_schedule_node_band_member_set_sched_pos(node, i, i);
    }
  }

  return node;
}

/* Set up the sched_pos properties.
 */
__isl_give isl_schedule_node *sched_pos_setup(__isl_take isl_schedule_node *node)
{
    node = isl_schedule_node_map_descendant_bottom_up(node,
                                                      &band_sched_pos_setup, NULL);

//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node))    
//#endif
    return node;
}

/* Check if the band is single dimension and the schedule value is a constant.
 * Return the constant value, or -1.
 */
int get_band_single_schedule_val(__isl_keep isl_schedule_node *node)
{
  isl_union_map *umap;
  isl_union_set *domain;
  isl_set *set;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return -1;
  if (isl_schedule_node_band_n_member(node) != 1)
    return -1;
  
  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  domain = isl_schedule_node_get_domain(node);
  umap = isl_union_map_intersect_domain(umap, domain);
  domain = isl_union_map_range(umap);
  set = isl_set_from_union_set(domain);
  if (isl_set_is_singleton(set)) {
    isl_val *val;    
    int ret;
    val = isl_set_plain_get_val_if_fixed(set, isl_dim_set, 0);    
    ret = isl_val_get_num_si(val);    
    isl_set_free(set);
    isl_val_free(val);
    return ret;
  } else {
    isl_set_free(set);
    return -1;
  }
}

/* Compute the prefix schedule of the current node and check if the last 
 * schedule dimension only contains single values. If so, return the value.
 */
int get_last_sched_dim_val(__isl_keep isl_schedule_node *node)
{
  isl_union_map *prefix;
  isl_set *range;

  prefix = isl_schedule_node_get_prefix_schedule_relation(node);
  range = isl_set_from_union_set(isl_union_map_range(prefix));  

  if (isl_set_dim(range, isl_dim_set) > 1)
    range = isl_set_project_out(range, isl_dim_set, 0, isl_set_dim(range, isl_dim_set) - 1);  

  range = isl_set_coalesce(range);
  if (isl_set_is_singleton(range)) {
    isl_val *val;
    int ret;
    val = isl_set_plain_get_val_if_fixed(range, isl_dim_set, 0);
    if (isl_val_is_nan(val)) {
      isl_set_free(range);
      isl_val_free(val);
      return -1;
    }    
    ret = isl_val_get_num_si(val);    
    isl_set_free(range);
    isl_val_free(val);
    return ret;
  } else {
    isl_set_free(range);
    return -1;
  }
}

/* Mark all dimensions in the current band node atomic.
 */
static __isl_give isl_schedule_node *atomic(__isl_take isl_schedule_node *node)
{
  return ppcg_set_schedule_node_type(node, isl_ast_loop_atomic);
}

/* Mark "node" atomic, if it is a band node.
 * Do the same for all ancestors.
 * Return a pointer to "node" (in the updated schedule tree).
 */
__isl_give isl_schedule_node *autosa_atomic_ancestors(
  __isl_take isl_schedule_node *node)
{
  int pos;

  if (!node)
    return NULL;
  if (!isl_schedule_node_has_parent(node))
    return node;

  pos = isl_schedule_node_get_child_position(node);
  node = isl_schedule_node_parent(node);
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    node = atomic(node);
  node = autosa_atomic_ancestors(node);
  node = isl_schedule_node_child(node, pos);

  return node;
}

/* Examines if the current schedule node is a io mark at the level "io_level".
 * Specifically, the io mark at the level "io_level" has the name as "io_L[io_level]".
 */
isl_bool isl_schedule_node_is_io_mark(__isl_keep isl_schedule_node *node, int io_level)
{
  isl_id *mark;
  const char *name;
  isl_printer *p;
  char *io_mark;

  if (!node)
    return isl_bool_error;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_mark)
    return isl_bool_false;

  mark = isl_schedule_node_mark_get_id(node);
  if (!mark)
    return isl_bool_error;

  name = isl_id_get_name(mark);
  p = isl_printer_to_str(isl_schedule_node_get_ctx(node));
  p = isl_printer_print_str(p, "io_L");
  p = isl_printer_print_int(p, io_level);
  io_mark = isl_printer_get_str(p);
  p = isl_printer_free(p);
  isl_id_free(mark);
  if (!strcmp(name, io_mark))
  {
    free(io_mark);
    return isl_bool_true;
  }
  else
  {
    free(io_mark);
    return isl_bool_false;
  }
}

/* Examine if the "node" is under the "simd" mark. 
 */
int is_node_under_simd(__isl_keep isl_schedule_node *node)
{
  isl_schedule_node *cur_node;

  cur_node = isl_schedule_node_copy(node);
  while (isl_schedule_node_has_parent(cur_node))
  {
    if (isl_schedule_node_get_type(cur_node) == isl_schedule_node_mark)
    {
      isl_id *id = isl_schedule_node_mark_get_id(cur_node);
      if (!strcmp(isl_id_get_name(id), "simd"))
      {
        isl_id_free(id);
        isl_schedule_node_free(cur_node);
        return 1;
      }
      isl_id_free(id);
    }
    cur_node = isl_schedule_node_parent(cur_node);
  }

  isl_schedule_node_free(cur_node);

  return 0;
}

/* Examine if the "node" is under the "latency" mark. */
int is_node_under_latency(__isl_keep isl_schedule_node *node)
{
  isl_schedule_node *cur_node;

  cur_node = isl_schedule_node_copy(node);
  while (isl_schedule_node_has_parent(cur_node))
  {
    if (isl_schedule_node_get_type(cur_node) == isl_schedule_node_mark)
    {
      isl_id *id = isl_schedule_node_mark_get_id(cur_node);
      if (!strcmp(isl_id_get_name(id), "latency"))
      {
        isl_id_free(id);
        isl_schedule_node_free(cur_node);
        return 1;
      }
      isl_id_free(id);
    }
    cur_node = isl_schedule_node_parent(cur_node);
  }

  isl_schedule_node_free(cur_node);

  return 0;
}

/* Compute a box hull of the time domain of the schedule node, and return the 
 * box dimensions in an array.
 */
int *extract_band_upper_bounds(__isl_keep isl_schedule_node *node)
{
  isl_union_map *umap;
  isl_union_set *uset;
  isl_map *map;  
  isl_set *set;
  int *ubs;
  int n;

  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  uset = isl_schedule_node_get_domain(node);
  umap = isl_union_map_intersect_domain(umap, uset);
  uset = isl_union_map_range(umap);
  set = isl_set_from_union_set(uset);

  n = isl_schedule_node_band_n_member(node);
  ubs = (int *)malloc(n * sizeof(int));
  for (int i = 0; i < n; i++) {
    ubs[i] = compute_set_max(set, i) + 1;
  }
  isl_set_free(set);

  return ubs;
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

/* Return constraints on the domain elements that equate a sequence of
 * parameters called "names", to the partial schedule of "node".
 * The number of members of the band node "node" should be smaller
 * than or equal to the number of elements in "names". 
 * If it is smaller, then the first elements of "names" are equated to zero.
 */
__isl_give isl_union_set *set_schedule_eq(
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
  /* Map the domain elements to "n_zero" zeros. */
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(
      isl_union_set_copy(domain), ma);
  /* Build a new mupa that mupa2 -> mupa */
  mupa = isl_multi_union_pw_aff_range_product(mupa2, mupa);
  space = isl_multi_union_pw_aff_get_space(mupa);
  ma = parameter_vector(space, names);
  mupa2 = isl_multi_union_pw_aff_multi_aff_on_domain(domain, ma);
  mupa = isl_multi_union_pw_aff_sub(mupa, mupa2);

  return isl_multi_union_pw_aff_zero_union_set(mupa);
}

__isl_give isl_union_set *set_schedule_neq(
    __isl_keep isl_schedule_node *node, __isl_keep isl_id_list *names)
{
  isl_union_set *uset, *domain;
  isl_union_map *umap;

  if (!node)
    return NULL;
  
  uset = set_schedule_eq(node, names);
  umap = isl_schedule_node_band_get_partial_schedule_union_map(node);
  domain = isl_union_map_domain(umap);
  uset = isl_union_set_subtract(domain, uset);

  return uset;
}

/* Construct schedule constraints from the dependences in prog->scop and
 * the array order dependences in prog->array_order.
 *
 * If live range reordering is allowed, then we need to make sure
 * that live ranges on arrays are not run in parallel since doing
 * so would require array expansion.  We therefore add the array
 * order dependences to the coincidence dependences.  Non-zero array
 * order dependences will then prevent a schedule dimension from being
 * considered parallel.
 * Live ranges derived from scalars are allowed to be run in parallel
 * since we force the scalars to be mapped to private memory in
 * check_scalar_live_ranges.
 * If live range reordering is allowed, then the false dependences
 * are not added to the validity constraints as that would prevent
 * reordering.  Instead, the external false dependences that enforce that reads
 * from potentially live-in data precede any later write and
 * that writes of potentially live-out data follow any other earlier write
 * are added to the validity and the coincidence constraints.
 * The false dependences are still added to the proximity constraints
 * for consistency with the case where live range reordering is not allowed.
 * The coincidence constraints then consist of flow dependences,
 * external false dependences and array order dependences.
 * The independences can be filtered out from the first two sets.
 * They have already been filtered out from the array order dependences
 * on a per array basis in collect_order_dependences.
 * There is no need for a per array handling of the other two sets
 * as there should be no flow or external false dependence on local
 * variables that can be filtered out.
 */
static __isl_give isl_schedule_constraints *construct_schedule_constraints(
    struct autosa_prog *prog)
{
  isl_union_set *domain;
  isl_union_map *dep_raw, *dep;
  isl_union_map *validity, *proximity, *coincidence;
  isl_schedule_constraints *sc;

  domain = isl_union_set_copy(prog->scop->domain);
  sc = isl_schedule_constraints_on_domain(domain);
  sc = isl_schedule_constraints_set_context(sc,
                                            isl_set_copy(prog->scop->context));
  if (prog->scop->options->live_range_reordering)
  {
    sc = isl_schedule_constraints_set_conditional_validity(sc,
                                                           isl_union_map_copy(prog->scop->tagged_dep_flow),
                                                           isl_union_map_copy(prog->scop->tagged_dep_order));
    proximity = isl_union_map_copy(prog->scop->dep_flow);
    validity = isl_union_map_copy(proximity);
    validity = isl_union_map_union(validity,
                                   isl_union_map_copy(prog->scop->dep_forced));
    proximity = isl_union_map_union(proximity,
                                    isl_union_map_copy(prog->scop->dep_false));
    coincidence = isl_union_map_copy(validity);
    coincidence = isl_union_map_subtract(coincidence,
                                         isl_union_map_copy(prog->scop->independence));
    coincidence = isl_union_map_union(coincidence,
                                      isl_union_map_copy(prog->array_order));
    /* Add the RAR into the validity constraints for AutoSA. */
    if (prog->scop->options->autosa->autosa)
    {
      validity = isl_union_map_union(validity,
                                     isl_union_map_copy(prog->scop->dep_rar));
    }
  }
  else
  {
//#ifdef _DEBUG
//    std::cout << "FLOW DEPs" << std::endl;
//    DBGUMAP(stdout, prog->scop->dep_flow, isl_union_map_get_ctx(prog->scop->dep_flow));    
//    std::cout << "FALSE DEPs" << std::endl;
//    DBGUMAP(stdout, prog->scop->dep_false, isl_union_map_get_ctx(prog->scop->dep_false));
//    std::cout << "RAR DEPs" << std::endl;
//    DBGUMAP(stdout, prog->scop->dep_rar, isl_union_map_get_ctx(prog->scop->dep_rar));
//#endif
    dep_raw = isl_union_map_copy(prog->scop->dep_flow);
    dep = isl_union_map_copy(prog->scop->dep_false);
    dep = isl_union_map_union(dep, dep_raw);    
    dep = isl_union_map_coalesce(dep);
    proximity = isl_union_map_copy(dep);
    coincidence = isl_union_map_copy(dep);
    validity = dep;
    /* Add the RAR into the validity constraints for AutoSA. */
    if (prog->scop->options->autosa->autosa)
    {
      validity = isl_union_map_union(validity,
                                     isl_union_map_copy(prog->scop->dep_rar));
    }
  }
  sc = isl_schedule_constraints_set_validity(sc, validity);
  sc = isl_schedule_constraints_set_coincidence(sc, coincidence);
  sc = isl_schedule_constraints_set_proximity(sc, proximity);

  return sc;
}

/* Compute an appropriate schedule based on the accesses in
 * gen->read and gen->write.
 *
 * We derive schedule constraints from the dependences in gen->prog->scop
 * and then use isl to compute a schedule that has a parallel loop
 * in each tilable band.
 * During the schedule construction, some statement instances
 * may be grouped first based on the input schedule.
 */
__isl_give isl_schedule *compute_schedule(struct autosa_gen *gen)
{
  isl_schedule_constraints *sc;
  isl_schedule *schedule;

  sc = construct_schedule_constraints(gen->prog);
  schedule = gen->prog->scop->schedule;
  schedule = ppcg_compute_schedule(sc, schedule, gen->options);

  return schedule;
}

/* If the band node "node" has exactly one member then mark it permutable.
 */
static __isl_give isl_schedule_node *band_set_permutable(
    __isl_take isl_schedule_node *node,
    __isl_keep isl_schedule_constraints *sc)
{
  if (isl_schedule_node_band_n_member(node) == 1)
    node = isl_schedule_node_band_set_permutable(node, 1);

  return node;
}

/* Return the coincidence constraints between pairs of instances
 * that are scheduled together by the ancestors of "node".
 * That is, select those coincidence constraints that relate
 * pairs of instances that have the same value for the prefix schedule.
 * If the schedule depth is zero, then the prefix schedule does not
 * contain any information, so we intersect domain and range
 * of the schedule constraints with the reaching domain elements instead.
 */
static __isl_give isl_union_map *get_local_coincidence(
    __isl_keep isl_schedule_node *node,
    __isl_keep isl_schedule_constraints *sc)
{
  isl_union_map *coincidence;
  isl_multi_union_pw_aff *prefix;
  isl_union_pw_multi_aff *contraction;

  coincidence = isl_schedule_constraints_get_coincidence(sc);
  contraction = isl_schedule_node_get_subtree_contraction(node);
  if (isl_schedule_node_get_schedule_depth(node) == 0)
  {
    isl_union_set *domain;

    domain = isl_schedule_node_get_domain(node);
    domain = isl_union_set_preimage_union_pw_multi_aff(domain,
                                                       contraction);
    coincidence = isl_union_map_intersect_domain(coincidence,
                                                 isl_union_set_copy(domain));
    coincidence = isl_union_map_intersect_range(coincidence,
                                                domain);
    return coincidence;
  }

  prefix = isl_schedule_node_get_prefix_schedule_multi_union_pw_aff(node);
  prefix = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(prefix,
                                                              contraction);
  return isl_union_map_eq_at_multi_union_pw_aff(coincidence, prefix);
}

/* For each member in the band node "node", determine whether
 * it is coincident with respect to the outer nodes and mark
 * it accordingly.
 *
 * That is, for each coincidence constraint between pairs
 * of instances that are scheduled together by the outer nodes,
 * check that domain and range are assigned the same value
 * by the band member.  This test is performed by checking
 * that imposing the same value for the band member does not
 * remove any elements from the set of coincidence constraints.
 */
static __isl_give isl_schedule_node *band_set_coincident(
    __isl_take isl_schedule_node *node,
    __isl_keep isl_schedule_constraints *sc)
{
  isl_union_map *coincidence;
  isl_union_pw_multi_aff *contraction;
  isl_multi_union_pw_aff *partial;
  int i, n;

  coincidence = get_local_coincidence(node, sc);

  partial = isl_schedule_node_band_get_partial_schedule(node);
  contraction = isl_schedule_node_get_subtree_contraction(node);
  partial = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(partial,
                                                               contraction);
  n = isl_schedule_node_band_n_member(node);
  for (i = 0; i < n; ++i)
  {
    isl_union_map *coincidence_i;
    isl_union_pw_aff *upa;
    isl_multi_union_pw_aff *partial_i;
    int subset;

    upa = isl_multi_union_pw_aff_get_union_pw_aff(partial, i);
    partial_i = isl_multi_union_pw_aff_from_union_pw_aff(upa);
    coincidence_i = isl_union_map_copy(coincidence);
    coincidence_i = isl_union_map_eq_at_multi_union_pw_aff(
        coincidence_i, partial_i);
    subset = isl_union_map_is_subset(coincidence, coincidence_i);
    isl_union_map_free(coincidence_i);

    if (subset < 0)
      break;
    node = isl_schedule_node_band_member_set_coincident(node, i,
                                                        subset);
  }
  if (i < n)
    node = isl_schedule_node_free(node);
  isl_multi_union_pw_aff_free(partial);
  isl_union_map_free(coincidence);

  return node;
}

/* If "node" is a band, then set its properties.
 *
 * In particular, if the band has exactly one member, then mark it permutable.
 * Mark the band members coincident based on the coincidence constraints
 * of "sc".
 */
static __isl_give isl_schedule_node *set_band_properties(
    __isl_take isl_schedule_node *node, void *user)
{
  isl_schedule_constraints *sc = (isl_schedule_constraints *)user;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return node;
  if (isl_schedule_node_band_n_member(node) == 0)
    return node;

  node = band_set_permutable(node, sc);
  node = band_set_coincident(node, sc);

  return node;
}

/* Return the original schedule with all bands marked permutable and
 * all band members marked coincident based on the coincidence constraints.
 * The bands are explicitly marked permutable so that they will be considered
 * by mark_outer_permutable.
 */
static __isl_give isl_schedule *determine_properties_original_schedule(
    struct autosa_gen *gen)
{
  isl_schedule *schedule;
  isl_schedule_constraints *sc;

  schedule = isl_schedule_copy(gen->prog->scop->schedule);
  sc = construct_schedule_constraints(gen->prog);
  schedule = isl_schedule_map_schedule_node_bottom_up(schedule,
                                                      &set_band_properties, sc);
  isl_schedule_constraints_free(sc);

  return schedule;
}

/* Compute a schedule or determine the properties of the original schedule
 * depending on the value of the "reschedule" option.
 */
static __isl_give isl_schedule *compute_or_set_properties(void *user)
{
  struct autosa_gen *gen = (struct autosa_gen *)user;

  if (gen->options->reschedule)
    return compute_schedule(gen);
  else
    return determine_properties_original_schedule(gen);
}

/* Obtain a schedule for the scop, by reading it from
 * a file, by computing one or by determining the properties
 * of the original schedule. 
 */
__isl_give isl_schedule *get_schedule(struct autosa_gen *gen)
{
  return ppcg_get_schedule(gen->ctx, gen->options,
                           &compute_or_set_properties, gen);
}

/* Since we are merging for the outermost band node, 
 * we will check if for each validity constraint if the domain is lexicographically 
 * less or equal to the range. 
 * Note that this function only considers the outermost node.
 */
static isl_bool is_dep_non_neg_at_node(
  __isl_keep isl_schedule_node *node, __isl_keep isl_schedule_constraints *sc)
{
  if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    return isl_bool_false;
  if (isl_schedule_node_band_n_member(node) == 0)
    return isl_bool_false;

  isl_union_map *validity;
  isl_union_pw_multi_aff *contraction;
  isl_multi_union_pw_aff *partial;
  isl_union_set *domain;
  int i, n;

  validity = isl_schedule_constraints_get_validity(sc);
  contraction = isl_schedule_node_get_subtree_contraction(node);
  domain = isl_schedule_node_get_domain(node);
  domain = isl_union_set_preimage_union_pw_multi_aff(domain, contraction);
  validity = isl_union_map_intersect_domain(validity, isl_union_set_copy(domain));
  validity = isl_union_map_intersect_range(validity, domain);
  //DBGUMAP(stdout, validity, isl_schedule_node_get_ctx(node));

  partial = isl_schedule_node_band_get_partial_schedule(node);
  contraction = isl_schedule_node_get_subtree_contraction(node);
  partial = isl_multi_union_pw_aff_pullback_union_pw_multi_aff(partial,
                                                               contraction);
  n = isl_schedule_node_band_n_member(node);
  for (i = 0; i < n; i++)
  {
    isl_union_map *validity_i, *validity_i_eq, *validity_i_lt;
    isl_union_pw_aff *upa;
    isl_multi_union_pw_aff *partial_i;
    int subset;

    upa = isl_multi_union_pw_aff_get_union_pw_aff(partial, i);
    partial_i = isl_multi_union_pw_aff_from_union_pw_aff(upa);    
    validity_i_eq = isl_union_map_eq_at_multi_union_pw_aff(
      isl_union_map_copy(validity), isl_multi_union_pw_aff_copy(partial_i));
    validity_i_lt = isl_union_map_lex_lt_at_multi_union_pw_aff(
      isl_union_map_copy(validity), partial_i);
    validity_i = isl_union_map_union(validity_i_eq, validity_i_lt);
    subset = isl_union_map_is_subset(validity, validity_i);
    isl_union_map_free(validity_i);

    if (subset <= 0)
      break;    
  }

  isl_multi_union_pw_aff_free(partial);
  isl_union_map_free(validity);

  return (i == n) ? isl_bool_true : isl_bool_false;
}

/* Try to merge the outer bands of the schedule as much as possible as 
 * long as they can form a permutable band.
 * Start from the outermost band, if the dependence distance on the current band 
 * is non-zero, merge it with the parent band node. 
 * This process stops until a non-band node is encoutnered.
 */
__isl_give isl_schedule *merge_outer_bands(__isl_take isl_schedule *schedule, struct autosa_gen *gen)
{
  isl_schedule_node *node;
  isl_schedule_constraints *sc;
  isl_bool is_first_band = isl_bool_true;

  node = isl_schedule_get_root(schedule); // points to the domain node
  isl_schedule_free(schedule);
  sc = construct_schedule_constraints(gen->prog);

  node = isl_schedule_node_child(node, 0); // points to the first band band
  while (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
    /* Examine if all dependence distances at this band are non-negative */    
    isl_bool nneg = is_dep_non_neg_at_node(node, sc);
    //std::cout << nneg << std::endl;
    if (nneg) {
      if (is_first_band)
        is_first_band = isl_bool_false;
      else {
        /* Merge the node with the parent band node. */
        node = isl_schedule_node_parent(node);
        node = autosa_node_merge(node); // TODO: delete the partial schedule space name
      }
    }
    node = isl_schedule_node_child(node, 0);
  }

  /* Set the coincidence. */
  node = isl_schedule_node_parent(node);
  if (isl_schedule_node_get_type(node) == isl_schedule_node_band) {
    node = band_set_coincident(node, sc);
  }

  schedule = isl_schedule_node_get_schedule(node);
  isl_schedule_node_free(node);
  isl_schedule_constraints_free(sc);

  return schedule;
}

/* Is "node" a mark node with an identifier called "array"?
 */
static int node_is_array(__isl_keep isl_schedule_node *node)
{
  return is_marked(node, "array");
}

/* Is "node" a mark node with an identifier called "anchor"?
 */
static int node_is_anchor(__isl_keep isl_schedule_node *node)
{
  return is_marked(node, "anchor");
}

/* Is "node" a mark node with an identifier called "local"?
 */
static int node_is_local(__isl_keep isl_schedule_node *node)
{
  return is_marked(node, "local");
}

/* Is "node" a mark node with an identifier called "pe"?
 */
static int node_is_pe(__isl_keep isl_schedule_node *node)
{
  return is_marked(node, "pe");
}

/* Is "node" a mark node with an identifier called "kernel"?
 */
static int node_is_kernel(__isl_keep isl_schedule_node *node)
{
  return is_marked(node, "kernel");
}

/* Is "node" a mark node with an identifier called "mark"?
 */
static int node_is_mark(__isl_keep isl_schedule_node *node, const char *mark)
{
  return is_marked(node, mark);
}

/* Is "node" a mark node with an identifier called "io_L[x]"?
 */
static int node_is_io_mark(__isl_keep isl_schedule_node *node)
{
  isl_id *mark;
  const char *name;
  int has_name;

  if (!node)
    return -1;

  if (isl_schedule_node_get_type(node) != isl_schedule_node_mark)
    return 0;

  mark = isl_schedule_node_mark_get_id(node);
  if (!mark)
    return -1;

  name = isl_id_get_name(mark);
  has_name = strncmp(name, "io_L", strlen("io_L"));

  isl_id_free(mark);

  return has_name;
}

/* Assuming "node" is a filter node, does it correspond to the branch
 * that contains the "array" mark, i.e., does it contain any elements in
 * "core"?
 */
static int node_is_core(__isl_keep isl_schedule_node *node,
                        __isl_keep isl_union_set *core)
{
  int disjoint;
  isl_union_set *filter;

  filter = isl_schedule_node_filter_get_filter(node);
  disjoint = isl_union_set_is_disjoint(filter, core);
  isl_union_set_free(filter);
  if (disjoint < 0)
    return -1;

  return !disjoint;
}

/* Move to the only child of "node" where the branch containing 
 * the domain elements in "core".
 *
 * If "node" is not a sequence, then it only has one child and we move
 * to that single child.
 * Otherwise, we check each of the filters in the children, pick
 * the one that corresponds to "core" and return a pointer to the child
 * of the filter node.
 */
static __isl_give isl_schedule_node *core_child(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
  int i, n;
  
  if (isl_schedule_node_get_type(node) != isl_schedule_node_sequence)
    return isl_schedule_node_child(node, 0);
  
  n = isl_schedule_node_n_children(node);
  for (i = 0; i < n; ++i)
  {
    int is_core;

    node = isl_schedule_node_child(node, i);
    is_core = node_is_core(node, core);

    if (is_core < 0)
      return isl_schedule_node_free(node);
    if (is_core)
      return isl_schedule_node_child(node, 0);

    node = isl_schedule_node_parent(node);
  }  

  isl_die(isl_schedule_node_get_ctx(node), isl_error_internal,
          "core child not found", return isl_schedule_node_free(node));
}

/* Move down from the "kernel" mark (or at least a node with schedule
 * depth smaller than or equal to "depth") to a band node at schedule
 * depth "depth".  The "array" mark is assumed to have a schedule
 * depth greater than or equal to "depth".  The branch containing the
 * "array" mark is identified by the domain elements in "core".
 *
 * If the desired schedule depth is in the middle of band node,
 * then the band node is split into two pieces, the second piece
 * at the desired schedule depth.
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_depth(
    __isl_take isl_schedule_node *node, int depth,
    __isl_keep isl_union_set *core)
{
  int is_local;
  int is_array = 0;

  while (node && isl_schedule_node_get_schedule_depth(node) < depth)
  {
    if (isl_schedule_node_get_type(node) ==
        isl_schedule_node_band)
    {
      int node_depth, node_dim;
      node_depth = isl_schedule_node_get_schedule_depth(node);
      node_dim = isl_schedule_node_band_n_member(node);
      if (node_depth + node_dim > depth)
        node = isl_schedule_node_band_split(node,
                                            depth - node_depth);
    }
    node = core_child(node, core);
  }
  while ((is_local = node_is_local(node)) == 0 &&
         (is_array = node_is_array(node)) == 0 &&
         isl_schedule_node_get_type(node) != isl_schedule_node_band)
    node = core_child(node, core);
  if (is_local < 0 || is_array < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch until the "array" mark is reached,
 * where the branch containing the "array" mark is 
 * identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_array(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
  int is_array;

  while ((is_array = node_is_array(node)) == 0)
    node = core_child(node, core);

  if (is_array < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move up the tree underneath the "array" mark until the "array" mark is reached. 
 */
__isl_give isl_schedule_node *autosa_tree_move_up_to_array(
    __isl_take isl_schedule_node *node)
{
  int is_array;

  while ((is_array = node_is_array(node)) == 0)
    node = isl_schedule_node_parent(node);

  if (is_array < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch between "kernel" and "local" until
 * the "local" mark is reached, where the branch containing the "local"
 * mark is identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_local(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
  int is_local;

  while ((is_local = node_is_local(node)) == 0)
    node = core_child(node, core);

  if (is_local < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch until the "kernel" mark is reached. 
 * In AutoSA, only one single kernel is identified, and it lies on the 
 * linear branch below the domain node. Therefore, we can safely
 * traverse down the branch until the "kernel" mark is found.
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_kernel(
    __isl_take isl_schedule_node *node)
{
  int is_kernel;

  while ((is_kernel = node_is_kernel(node)) == 0)
    node = isl_schedule_node_child(node, 0);

  if (is_kernel < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move up the tree underneath the "kernel" mark until
 * the "kernel" mark is reached.
 */
__isl_give isl_schedule_node *autosa_tree_move_up_to_kernel(
    __isl_take isl_schedule_node *node)
{
  int is_kernel;

  while ((is_kernel = autosa_tree_node_is_kernel(node)) == 0)
  {
    node = isl_schedule_node_parent(node);
  }
  if (is_kernel < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch between "kernel" and "pe" until
 * the "pe" mark is reached, where the branch containing the "pe"
 * mark is identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_pe(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
  int is_pe;

  while ((is_pe = node_is_pe(node)) == 0)
    node = core_child(node, core);

  if (is_pe < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move up the tree underneath the "array" mark until the "pe" mark is reached. 
 */
__isl_give isl_schedule_node *autosa_tree_move_up_to_pe(
    __isl_take isl_schedule_node *node)
{
  int is_pe;

  while ((is_pe = node_is_pe(node)) == 0)
    node = isl_schedule_node_parent(node);

  if (is_pe < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch between "kernel" and "mark" until
 * the "mark" mark is reached, where the branch containing the "mark"
 * mark is identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_mark(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core, const char *mark)
{
  int is_mark;

  while ((is_mark = node_is_mark(node, mark)) == 0)
    node = core_child(node, core);

  if (is_mark < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move up the tree underneath the "mark" mark until the "mark" mark is reached. 
 */
__isl_give isl_schedule_node *autosa_tree_move_up_to_mark(
    __isl_take isl_schedule_node *node, const char *mark)
{
  int is_mark;

  while ((is_mark = node_is_mark(node, mark)) == 0)
    node = isl_schedule_node_parent(node);

  if (is_mark < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch between "kernel" and "pe" until
 * the first "io_L[x]" mark is reached, where the branch containing the "io_L[x]"
 * mark is identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_first_io_mark(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core)
{
  int is_io_mark;

  while ((is_io_mark = node_is_io_mark(node)) == 0)
    node = core_child(node, core);

  if (is_io_mark < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Move down the branch between "kernel" and "pe" until
 * the "io_L[io_level]" mark is reached, where the branch containing the io
 * mark is identified by the domain elements in "core".
 */
__isl_give isl_schedule_node *autosa_tree_move_down_to_io_mark(
    __isl_take isl_schedule_node *node, __isl_keep isl_union_set *core, int io_level)
{
  int is_mark;
  isl_printer *p;
  char *mark;  

  p = isl_printer_to_str(isl_schedule_node_get_ctx(node));
  p = isl_printer_print_str(p, "io_L");
  p = isl_printer_print_int(p, io_level);
  mark = isl_printer_get_str(p);
  p = isl_printer_free(p);


  while ((is_mark = node_is_mark(node, mark)) == 0) {
    if (!isl_schedule_node_has_children(node))
      break;
    node = core_child(node, core);
  }

  if (is_mark <= 0)
    node = isl_schedule_node_free(node);  
  free(mark);

  return node;
}

/* Move up the tree underneath the "anchor" mark until the "anchor" mark is reached. 
 */
__isl_give isl_schedule_node *autosa_tree_move_up_to_anchor(
    __isl_take isl_schedule_node *node)
{
  int is_anchor;

  while ((is_anchor = node_is_anchor(node)) == 0)
    node = isl_schedule_node_parent(node);

  if (is_anchor < 0)
    node = isl_schedule_node_free(node);

  return node;
}

/* Is "node" a mark node with an identifier called "kernel"?
 */
int autosa_tree_node_is_kernel(__isl_keep isl_schedule_node *node)
{
  return is_marked(node, "kernel");
}

/* Is "node" a mark node with an identifier called "mark"?
 */
int autosa_tree_node_is_mark(__isl_keep isl_schedule_node *node, const char *mark)
{
  if (mark == NULL)
    return (isl_schedule_node_get_type(node) == isl_schedule_node_mark);

  return is_marked(node, mark);
}

/* Insert a mark node with identifier "local" in front of "node".
 */
static __isl_give isl_schedule_node *insert_local(
    __isl_take isl_schedule_node *node)
{
  isl_ctx *ctx;
  isl_id *id;

  ctx = isl_schedule_node_get_ctx(node);
  id = isl_id_alloc(ctx, "local", NULL);
  node = isl_schedule_node_insert_mark(node, id);

  return node;
}

/* Insert a "local" mark in front of the "array" mark 
 * provided the linear branch between "node" and the "array" mark
 * does not contain such a "local" mark already.
 *
 * As a side effect, this function checks that the subtree at "node"
 * actually contains a "array" mark and that there is no branching
 * in between "node" and this "array" mark.
 * The new node at the original position of "node" is returned.
 */
__isl_give isl_schedule_node *autosa_tree_insert_local_before_array(
    __isl_take isl_schedule_node *node)
{
  int depth0, depth;
  int any_local = 0;

  if (!node)
    return NULL;

  depth0 = isl_schedule_node_get_tree_depth(node);

  for (;;)
  {
    int is_array;
    int n;

    if (!any_local)
    {
      any_local = node_is_local(node);
      if (any_local < 0)
        return isl_schedule_node_free(node);
    }
    is_array = node_is_array(node);
    if (is_array < 0)
      return isl_schedule_node_free(node);
    if (is_array)
      break;
    n = isl_schedule_node_n_children(node);
    if (n == 0)
      isl_die(isl_schedule_node_get_ctx(node),
              isl_error_invalid,
              "no array marker found",
              return isl_schedule_node_free(node));
    if (n > 1)
      isl_die(isl_schedule_node_get_ctx(node),
              isl_error_invalid,
              "expecting single array marker",
              return isl_schedule_node_free(node));

    node = isl_schedule_node_child(node, 0);
  }

  if (!any_local)
    node = insert_local(node);
  depth = isl_schedule_node_get_tree_depth(node);
  node = isl_schedule_node_ancestor(node, depth - depth0);

  return node;
}
