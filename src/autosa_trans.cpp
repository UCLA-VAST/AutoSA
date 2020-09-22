#include <string>
#include <exception>
//#include <chrono>
//using namespace std::chrono;

#include "autosa_trans.h"
#include "autosa_utils.h"
#include "autosa_schedule_tree.h"
#include "autosa_comm.h"
#include "autosa_codegen.h"

/* A program is legal to be transformed to systolic array if and only if 
 * it satisfies the following constraints:
 * - one single fully permutable outermost band
 * - uniform dependency
 */
isl_bool sa_legality_check(__isl_keep isl_schedule *schedule, struct ppcg_scop *scop)
{
    isl_bool single_band;
    enum isl_schedule_node_type type;

    /* Check if the root node point to a band node */
    isl_schedule_node *node = isl_schedule_get_root(schedule);
    node = isl_schedule_node_child(node, 0);
    type = isl_schedule_node_get_type(node);
    single_band = (type == isl_schedule_node_band) ? isl_bool_true : isl_bool_false;
    isl_schedule_node_free(node);
    if (!single_band)
    {
        throw std::runtime_error("[AutoSA] Error: Single outermost permutable band not found.");
    }

    //DBGSCHD(stdout, schedule, isl_schedule_get_ctx(schedule))

    /* Check if all flow and rar dependences are uniform. */
    isl_bool all_uniform_dep = uniform_dep_check(schedule, scop);
    if (all_uniform_dep < 1)
    {
        throw std::runtime_error("[AutoSA] Error: Non-uniform dependence detected.");
    }    

    return isl_bool_true;
}

/* Load the tuning configuration file.  
 */
static cJSON *load_tuning_config(char *config_file)
{
    FILE *f;
    char *buffer = NULL;
    cJSON *config = NULL;
    long length;

    f = fopen(config_file, "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = (char *)malloc(length + 1);
        if (buffer)
        {
            buffer[length] = '\0';
            int r = fread(buffer, 1, length, f);
        }
        fclose(f);
    }
    else
    {
        printf("[AutoSA] Error: Can't open configuration file: %s\n", config_file);
        exit(1);
    }

    if (buffer)
    {
        config = cJSON_Parse(buffer);
        free(buffer);
    }

    return config;
}

/* Generate asyncrhonized systolic arrays with the given dimension.
 * For sync arrays, time loops are placed inside the space loops.
 * We will first select space loop candidates from the outermost loop band 
 * which carry dependences with distance less than or equal to 1. 
 * Then we will enumerate different space loop combinations by picking up "dim" 
 * space loops from the candidate pool.
 */
struct autosa_kernel **sa_space_time_transform_at_dim_async(
    __isl_keep isl_schedule *schedule, struct ppcg_scop *scop,
    isl_size dim, isl_size *num_sa)
{
    struct autosa_kernel **sas = NULL;

    /* Select space loop candidates.
     * Space loops carry dependences with distance less or equal to 1.
     */
    isl_schedule_node *band = get_outermost_permutable_node(schedule);
    isl_size band_w = isl_schedule_node_band_n_member(band);
    isl_size *is_space_loop = (isl_size *)malloc(band_w * sizeof(isl_size));
    isl_union_map *dep_flow = scop->dep_flow;
    isl_union_map *dep_rar = scop->dep_rar;
    isl_union_map *dep_total = isl_union_map_union(isl_union_map_copy(dep_flow),
                                                   isl_union_map_copy(dep_rar));
    isl_basic_map_list *deps = isl_union_map_get_basic_map_list(dep_total);
    isl_size ndeps = isl_union_map_n_basic_map(dep_total);

    for (int h = 0; h < band_w; h++)
    {
        int n;
        for (n = 0; n < ndeps; n++)
        {
            isl_basic_map *dep = isl_basic_map_list_get_basic_map(deps, n);
            isl_vec *dep_dis = get_dep_dis_at_node(dep, band);
            isl_val *val = isl_vec_get_element_val(dep_dis, h);
            if (!(isl_val_is_one(val) || isl_val_is_zero(val)))
            {
                isl_vec_free(dep_dis);
                isl_val_free(val);
                isl_basic_map_free(dep);
                break;
            }

            isl_val_free(val);
            isl_vec_free(dep_dis);
            isl_basic_map_free(dep);
        }
        is_space_loop[h] = (n == ndeps);
    }

    /* Perform loop permutation to generate all candidates. */
    if (dim == 1)
    {
        for (int i = 0; i < band_w; i++)
        {
            if (is_space_loop[i])
            {
                isl_schedule *new_schedule = isl_schedule_dup(schedule);
                isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                isl_schedule_free(new_schedule);

                /* Make the loop i the outermost loop. */
                for (int d = i; d > 0; d--)
                {                    
                    band = loop_interchange_at_node(band, d, d - 1);
                }
                new_schedule = isl_schedule_node_get_schedule(band);
                isl_schedule_node_free(band);

                /* Update the hyperplane types. */
                struct autosa_kernel *sa = autosa_kernel_from_schedule(new_schedule);
                sa->scop = scop;
                sa->type = AUTOSA_SA_TYPE_ASYNC;

                /* Update the array dimension. */
                sa->n_sa_dim = dim;
                sa->array_part_w = 0;
                sa->space_w = dim;
                // TODO: incorrect, to fix.
                sa->time_w = band_w - dim;

                /* Add the new variant into the list. */
                sas = (struct autosa_kernel **)realloc(sas, (*num_sa + 1) *
                                                                sizeof(struct autosa_kernel *));
                sas[*num_sa] = sa;
                *num_sa = *num_sa + 1;
            }
        }
    }
    else if (dim == 2)
    {
        for (int i = 0; i < band_w; i++)
        {
            if (is_space_loop[i])
            {
                for (int j = i + 1; j < band_w; j++)
                {
                    if (is_space_loop[j])
                    {
                        isl_schedule *new_schedule = isl_schedule_dup(schedule);
                        isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                        isl_schedule_free(new_schedule);

                        /* Make the loop i, j the outermost loops. */
                        for (int d = j; d > 0; d--)
                        {
                            //isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                            //isl_schedule_free(new_schedule);
                            //new_schedule = loop_interchange_at_node(band, d, d - 1);
                            band = loop_interchange_at_node(band, d, d - 1);
                        }
                        for (int d = i + 1; d > 0; d--)
                        {
                            //isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                            //isl_schedule_free(new_schedule);
                            //new_schedule = loop_interchange_at_node(band, d, d - 1);
                            band = loop_interchange_at_node(band, d, d - 1);
                        }
                        new_schedule = isl_schedule_node_get_schedule(band);
                        isl_schedule_node_free(band);

                        /* Update the hyperplane types. */
                        struct autosa_kernel *sa = autosa_kernel_from_schedule(new_schedule);
                        sa->scop = scop;
                        sa->type = AUTOSA_SA_TYPE_ASYNC;

                        /* Update the array dimension. */
                        sa->n_sa_dim = dim;
                        sa->array_part_w = 0;
                        sa->space_w = dim;
                        // TODO: incorrect, to fix.
                        sa->time_w = band_w - dim;

                        /* Add the new variant into the list. */
                        sas = (struct autosa_kernel **)realloc(sas, (*num_sa + 1) *
                                                                        sizeof(struct autosa_kernel *));
                        sas[*num_sa] = sa;
                        *num_sa = *num_sa + 1;
                    }
                }
            }
        }
    }
    else if (dim == 3)
    {
        for (int i = 0; i < band_w; i++)
        {
            if (is_space_loop[i])
            {
                for (int j = i + 1; j < band_w; j++)
                {
                    if (is_space_loop[j])
                    {
                        for (int k = j + 1; k < band_w; k++)
                        {
                            if (is_space_loop[k])
                            {
                                isl_schedule *new_schedule = isl_schedule_dup(schedule);
                                isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                                isl_schedule_free(new_schedule);

                                /* Make the loop i, j, k the outermost loops. */
                                for (int d = k; d > 0; d--)
                                {
                                    //isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                                    //isl_schedule_free(new_schedule);
                                    //new_schedule = loop_interchange_at_node(band, d, d - 1);
                                    band = loop_interchange_at_node(band, d, d - 1);
                                }
                                for (int d = j + 1; d > 0; d--)
                                {
                                    //isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                                    //isl_schedule_free(new_schedule);
                                    //new_schedule = loop_interchange_at_node(band, d, d - 1);
                                    band = loop_interchange_at_node(band, d, d - 1);
                                }
                                for (int d = i + 2; d > 0; d--)
                                {
                                    //isl_schedule_node *band = get_outermost_permutable_node(new_schedule);
                                    //isl_schedule_free(new_schedule);
                                    //new_schedule = loop_interchange_at_node(band, d, d - 1);
                                    band = loop_interchange_at_node(band, d, d - 1);
                                }
                                new_schedule = isl_schedule_node_get_schedule(band);
                                isl_schedule_node_free(band);

                                /* Update the hyperplane types. */
                                struct autosa_kernel *sa = autosa_kernel_from_schedule(new_schedule);
                                sa->scop = scop;
                                sa->type = AUTOSA_SA_TYPE_ASYNC;

                                /* Update the array dimension. */
                                sa->n_sa_dim = dim;
                                sa->array_part_w = 0;
                                sa->space_w = dim;
                                // TODO: incorrect, to fix.
                                sa->time_w = band_w - dim;

                                /* Add the new variant into the list. */
                                sas = (struct autosa_kernel **)realloc(sas, (*num_sa + 1) *
                                                                                sizeof(struct autosa_kernel *));
                                sas[*num_sa] = sa;
                                *num_sa = *num_sa + 1;
                            }
                        }
                    }
                }
            }
        }
    }

    isl_basic_map_list_free(deps);
    isl_union_map_free(dep_total);
    isl_schedule_node_free(band);
    free(is_space_loop);

    return sas;
}

/* Generate syncrhonized systolic arrays with the given dimension.
 * For sync arrays, time loops are placed outside the space loops.
 * We will first select space loop candidates from the innermost loop band 
 * which carry dependences with distance less than or equal to 1. 
 * Then we will enumerate different space loop combinations by picking up "dim" 
 * space loops from the candidate pool.
 */
struct autosa_kernel **sa_space_time_transform_at_dim_sync(
    __isl_keep isl_schedule *schedule, struct ppcg_scop *scop,
    isl_size dim, isl_size *num_sa)
{
    struct autosa_kernel **sas = NULL;

    /* Select space loop candidates.
   * Space loops carry dependences with distance less or equal to 1.
   */
    isl_schedule_node *band = get_innermost_permutable_node(schedule);
    isl_size band_w = isl_schedule_node_band_n_member(band);
    isl_size *is_space_loop = (isl_size *)malloc(band_w * sizeof(isl_size));
    isl_union_map *dep_flow = scop->dep_flow;
    isl_union_map *dep_rar = scop->dep_rar;
    isl_union_map *dep_total = isl_union_map_union(isl_union_map_copy(dep_flow),
                                                   isl_union_map_copy(dep_rar));
    isl_basic_map_list *deps = isl_union_map_get_basic_map_list(dep_total);
    isl_size ndeps = isl_union_map_n_basic_map(dep_total);

    for (int h = 0; h < band_w; h++)
    {
        int n;
        for (n = 0; n < ndeps; n++)
        {
            isl_basic_map *dep = isl_basic_map_list_get_basic_map(deps, n);
            isl_vec *dep_dis = get_dep_dis_at_node(dep, band);
            isl_val *val = isl_vec_get_element_val(dep_dis, h);
            if (!(isl_val_is_one(val) || isl_val_is_zero(val)))
            {
                isl_vec_free(dep_dis);
                isl_val_free(val);
                isl_basic_map_free(dep);
                break;
            }

            isl_val_free(val);
            isl_vec_free(dep_dis);
            isl_basic_map_free(dep);
        }
        is_space_loop[h] = (n == ndeps);
    }

    /* Perform loop permutation to generate all candidates. */
    if (dim == 1)
    {
        for (int i = 0; i < band_w; i++)
        {
            if (is_space_loop[i])
            {
                isl_schedule *new_schedule = isl_schedule_dup(schedule);
                isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                isl_schedule_free(new_schedule);

                /* Make the loop i the innermost loop. */
                for (int d = i; d < band_w - 1; d++)
                {
                    //isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                    //isl_schedule_free(new_schedule);
                    //new_schedule = loop_interchange_at_node(band, d, d + 1);
                    band = loop_interchange_at_node(band, d, d + 1);
                }
                new_schedule = isl_schedule_node_get_schedule(band);
                isl_schedule_node_free(band);

                /* Update the hyperplane types. */
                struct autosa_kernel *sa = autosa_kernel_from_schedule(new_schedule);
                sa->scop = scop;
                sa->type = AUTOSA_SA_TYPE_SYNC;

                /* Update the array dimension. */
                sa->n_sa_dim = dim;
                sa->array_part_w = 0;
                sa->space_w = dim;
                // TODO: this is incorrect, we need to consider other loop bands.
                sa->time_w = band_w - dim;

                /* Add the new variant into the list. */
                sas = (struct autosa_kernel **)realloc(sas, (*num_sa + 1) *
                                                                sizeof(struct autosa_kernel *));
                sas[*num_sa] = sa;
                *num_sa = *num_sa + 1;
            }
        }
    }
    else if (dim == 2)
    {
        for (int i = 0; i < band_w; i++)
        {
            if (is_space_loop[i])
            {
                for (int j = i + 1; j < band_w; j++)
                {
                    if (is_space_loop[j])
                    {
                        isl_schedule *new_schedule = isl_schedule_dup(schedule);
                        isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                        isl_schedule_free(new_schedule);

                        /* Make the loop i, j the innermost loops. */
                        for (int d = i; d < band_w - 1; d++)
                        {
                            //isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                            //isl_schedule_free(new_schedule);
                            //new_schedule = loop_interchange_at_node(band, d, d + 1);
                            band = loop_interchange_at_node(band, d, d + 1);
                        }
                        for (int d = j - 1; d < band_w - 1; d++)
                        {
                            //isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                            //isl_schedule_free(new_schedule);
                            //new_schedule = loop_interchange_at_node(band, d, d + 1);
                            band = loop_interchange_at_node(band, d, d + 1);
                        }
                        new_schedule = isl_schedule_node_get_schedule(band);
                        isl_schedule_node_free(band);

                        /* Update the hyperplane types. */
                        struct autosa_kernel *sa = autosa_kernel_from_schedule(new_schedule);
                        sa->scop = scop;
                        sa->type = AUTOSA_SA_TYPE_SYNC;

                        /* Update the array dimension. */
                        sa->n_sa_dim = dim;
                        sa->array_part_w = 0;
                        sa->space_w = dim;
                        // TODO: incorrect, to fix.
                        sa->time_w = band_w - dim;

                        /* Add the new variant into the list. */
                        sas = (struct autosa_kernel **)realloc(sas, (*num_sa + 1) *
                                                                        sizeof(struct autosa_kernel *));
                        sas[*num_sa] = sa;
                        *num_sa = *num_sa + 1;
                    }
                }
            }
        }
    }
    else if (dim == 3)
    {
        for (int i = 0; i < band_w; i++)
        {
            if (is_space_loop[i])
            {
                for (int j = i + 1; j < band_w; j++)
                {
                    if (is_space_loop[j])
                    {
                        for (int k = j + 1; k < band_w; k++)
                        {
                            if (is_space_loop[k])
                            {
                                isl_schedule *new_schedule = isl_schedule_dup(schedule);
                                isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                                isl_schedule_free(new_schedule);

                                /* Make the loop i, j, k the innermost loops. */
                                for (int d = i; d < band_w - 1; d++)
                                {
                                    //isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                                    //isl_schedule_free(new_schedule);
                                    //new_schedule = loop_interchange_at_node(band, d, d + 1);
                                    band = loop_interchange_at_node(band, d, d + 1);
                                }
                                for (int d = j - 1; d < band_w - 1; d++)
                                {
                                    //isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                                    //isl_schedule_free(new_schedule);
                                    //new_schedule = loop_interchange_at_node(band, d, d + 1);
                                    band = loop_interchange_at_node(band, d, d + 1);
                                }
                                for (int d = k - 2; d < band_w - 1; d++)
                                {
                                    //isl_schedule_node *band = get_innermost_permutable_node(new_schedule);
                                    //isl_schedule_free(new_schedule);
                                    //new_schedule = loop_interchange_at_node(band, d, d + 1);
                                    band = loop_interchange_at_node(band, d, d + 1);
                                }
                                new_schedule = isl_schedule_node_get_schedule(band);
                                isl_schedule_node_free(band);

                                /* Update the hyperplane types. */
                                struct autosa_kernel *sa = autosa_kernel_from_schedule(new_schedule);
                                sa->scop = scop;
                                sa->type = AUTOSA_SA_TYPE_SYNC;

                                /* Update the array dimension. */
                                sa->n_sa_dim = dim;
                                sa->array_part_w = 0;
                                sa->space_w = dim;
                                sa->time_w = band_w - dim;

                                /* Add the new variant into the list. */
                                sas = (struct autosa_kernel **)realloc(sas, (*num_sa + 1) *
                                                                                sizeof(struct autosa_kernel *));
                                sas[*num_sa] = sa;
                                *num_sa = *num_sa + 1;
                            }
                        }
                    }
                }
            }
        }
    }

    isl_basic_map_list_free(deps);
    isl_union_map_free(dep_total);
    isl_schedule_node_free(band);
    free(is_space_loop);

    return sas;
}

/* Generate systolic array with "dim" space dimensions. 
 * Depending on the systolic array type set by users, we will generate 
 * async or sync arrays.
 */
struct autosa_kernel **sa_space_time_transform_at_dim(
    __isl_keep isl_schedule *schedule, struct ppcg_scop *scop,
    isl_size dim, isl_size *num_sa)
{
    if (scop->options->autosa->sa_type == AUTOSA_SA_TYPE_ASYNC)
    {
        return sa_space_time_transform_at_dim_async(schedule, scop, dim, num_sa);
    }
    else if (scop->options->autosa->sa_type == AUTOSA_SA_TYPE_SYNC)
    {
        return sa_space_time_transform_at_dim_sync(schedule, scop, dim, num_sa);
    }

    return NULL;
}

/* Apply space-time transformation to generate different systolic array candidates. */
struct autosa_kernel **sa_space_time_transform(__isl_take isl_schedule *schedule,
                                               struct ppcg_scop *scop, isl_size *num_sa)
{
    struct autosa_kernel **sa_list = NULL;
    isl_size n_sa = 0;

    isl_schedule_node *band = get_outermost_permutable_node(schedule);
    isl_size band_w = isl_schedule_node_band_n_member(band);
    /* Explore 1D systolic array */
    if (scop->options->autosa->max_sa_dim >= 1 && band_w >= 1)
    {
        if (scop->options->autosa->verbose)
        {
            printf("AutoSA] Explore 1D systolic array.\n");
        }
        isl_size n_sa_dim = 0;
        struct autosa_kernel **sa_dim_list = sa_space_time_transform_at_dim(
            schedule, scop, 1, &n_sa_dim);
        if (scop->options->autosa->verbose)
        {
            printf("[AutoSA] %d candidates generated.\n", n_sa_dim);
        }
        sa_list = (struct autosa_kernel **)realloc(sa_list,
                                                   (n_sa + n_sa_dim) * sizeof(struct autosa_kernel *));
        for (int i = 0; i < n_sa_dim; i++)
        {
            sa_list[n_sa + i] = sa_dim_list[i];
            sa_list[n_sa + i]->space_time_id = n_sa + i;
        }
        free(sa_dim_list);
        n_sa += n_sa_dim;
    }
    /* Explore 2D systolic array */
    if (scop->options->autosa->max_sa_dim >= 2 && band_w >= 2)
    {
        if (scop->options->autosa->verbose)
        {
            printf("[AutoSA] Explore 2D systolic array.\n");
        }
        isl_size n_sa_dim = 0;
        struct autosa_kernel **sa_dim_list = sa_space_time_transform_at_dim(
            schedule, scop, 2, &n_sa_dim);
        if (scop->options->autosa->verbose)
        {
            printf("[AutoSA] %d candidates generated.\n", n_sa_dim);
        }
        sa_list = (struct autosa_kernel **)realloc(sa_list,
                                                   (n_sa + n_sa_dim) * sizeof(struct autosa_kernel *));
        for (int i = 0; i < n_sa_dim; i++)
        {
            sa_list[n_sa + i] = sa_dim_list[i];
            sa_list[n_sa + i]->space_time_id = n_sa + i;
        }
        free(sa_dim_list);
        n_sa += n_sa_dim;
    }
    /* Explore 3D systolic array */
    if (scop->options->autosa->max_sa_dim >= 3 && band_w >= 3)
    {
        if (scop->options->autosa->verbose)
        {
            printf("[AutoSA] Explore 3D systolic array.\n");
        }
        isl_size n_sa_dim = 0;
        struct autosa_kernel **sa_dim_list = sa_space_time_transform_at_dim(
            schedule, scop, 3, &n_sa_dim);
        if (scop->options->autosa->verbose)
        {
            printf("[AutoSA] %d candidates generated.\n", n_sa_dim);
        }
        sa_list = (struct autosa_kernel **)realloc(sa_list,
                                                   (n_sa + n_sa_dim) * sizeof(struct autosa_kernel *));
        for (int i = 0; i < n_sa_dim; i++)
        {
            sa_list[n_sa + i] = sa_dim_list[i];
            sa_list[n_sa + i]->space_time_id = n_sa + i;
        }
        free(sa_dim_list);
        n_sa += n_sa_dim;
    }

    isl_schedule_free(schedule);
    isl_schedule_node_free(band);
    *num_sa = n_sa;
    /* Assign the kernel id */
    for (int i = 0; i < n_sa; i++)
    {
        sa_list[i]->id = i;
    }

    return sa_list;
}

/* Initialize the space_time to autosa_loop_time, 
 * and pe_opt to autosa_loop_default for all band nodes. */
static __isl_give isl_schedule_node *init_band_node_sa_properties(
    __isl_take isl_schedule_node *node, void *user)
{
    if (!node)
        return NULL;

    struct autosa_kernel *sa = (struct autosa_kernel *)(user);

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        int band_w = isl_schedule_node_band_n_member(node);
        /* Initialize the SA properties. */
        for (int i = 0; i < band_w; i++)
        {
            node = isl_schedule_node_band_member_set_space_time(node, i, autosa_loop_time);
            node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_default);
            //node = isl_schedule_node_band_member_set_sched_pos(node, i, -1);
        }
    }

    return node;
}

/* Initialize the fields of time_space and pe_opt for each band node in the 
 * schedule tree. */
isl_stat sa_loop_init(struct autosa_kernel *sa)
{
    isl_schedule *schedule = sa->schedule;
    isl_schedule_node *root = isl_schedule_get_root(schedule);
    root = isl_schedule_node_map_descendant_bottom_up(root,
                                                      &init_band_node_sa_properties, sa);

    schedule = isl_schedule_node_get_schedule(root);
    isl_schedule_node_free(root);
    isl_schedule_free(sa->schedule);
    sa->schedule = schedule;

    return isl_stat_ok;
}

/* Set up the space_time properties. 
 * As all the loops are initialized to be the time loop in the sa_loop_init(),
 * only the space loops are to be set.
 */
isl_stat sa_space_time_loop_setup(struct autosa_kernel *sa)
{
    isl_schedule_node *node;
    if (sa->type == AUTOSA_SA_TYPE_SYNC)
    {
        node = get_innermost_permutable_node(sa->schedule);
        for (int i = isl_schedule_node_band_n_member(node) - sa->space_w;
             i < isl_schedule_node_band_n_member(node); i++)
        {
            node = isl_schedule_node_band_member_set_space_time(node, i, autosa_loop_space);
        }
    }
    else if (sa->type == AUTOSA_SA_TYPE_ASYNC)
    {
        node = get_outermost_permutable_node(sa->schedule);
        for (int i = 0; i < sa->space_w; i++)
        {
            node = isl_schedule_node_band_member_set_space_time(node, i, autosa_loop_space);
        }
    }

    isl_schedule *schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);
    isl_schedule_free(sa->schedule);
    sa->schedule = schedule;

    return isl_stat_ok;
}

/* Internal struct used for sa_candidates_smart_pick. */
struct sa_candidates_smart_pick_update_data
{
    int score;
    struct autosa_kernel *sa;
    enum autosa_dep_type dep_type;
};

/* Internal struct used for not_carrried_at_space. */
struct dep_space_test_internal_data
{
    isl_vec *dirvec;
    isl_basic_map *dep;
};

/* This function tests if the current node contains any space loop.
 * If so, test if the dependence is carried by the space loops, and update the 
 * dependence distance vector. 
 * If the dependence is carried at the space loop, return false,
 * else return true.
 */
static isl_bool not_carried_at_space(__isl_keep isl_schedule_node *node, void *user)
{
    struct dep_space_test_internal_data *data =
        (struct dep_space_test_internal_data *)user;
    isl_basic_map *dep = data->dep;
    isl_basic_map *untagged_dep = isl_basic_map_from_map(
        isl_map_factor_domain(isl_map_from_basic_map(isl_basic_map_copy(dep))));
    if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
    {
        isl_basic_map_free(untagged_dep);
        return isl_bool_true;
    }

    /* Examine if there is any space loop in the current loop band. */
    int n_dim = isl_schedule_node_band_n_member(node);
    int n_space_dim, space_dim_start;
    n_space_dim = 0;
    for (int i = 0; i < n_dim; i++)
    {
        if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_space)
        {
            if (n_space_dim == 0)
                space_dim_start = i;
            n_space_dim++;
        }
    }

    if (n_space_dim > 0)
    {
        isl_vec *disvec = get_dep_dis_at_node(untagged_dep, node);
        isl_vec *dirvec = isl_vec_zero(isl_schedule_node_get_ctx(node), n_space_dim);
        int carried = 0;
        for (int i = 0; i < n_space_dim; i++)
        {
            isl_val *val = isl_vec_get_element_val(disvec, space_dim_start + i);
            dirvec = isl_vec_set_element_si(dirvec, i, isl_val_get_num_si(val));
            if (isl_val_get_num_si(val) > 0)
                carried = 1;
            isl_val_free(val);
        }
        data->dirvec = dirvec;
        isl_vec_free(disvec);
        isl_basic_map_free(untagged_dep);
        if (carried)
            return isl_bool_false;
        else
            return isl_bool_true;
    }
    isl_basic_map_free(untagged_dep);
    return isl_bool_true;
}

/* Update the score for the array. 
 * Specifically, add one credit if RAR is carried by space loops or 
 * RAW is carried by time loops.
 */
static isl_bool sa_candidates_smart_pick_update(__isl_keep isl_map *map, void *user)
{
    isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(map);
    struct sa_candidates_smart_pick_update_data *data =
        (struct sa_candidates_smart_pick_update_data *)user;
    struct autosa_kernel *sa = data->sa;
    isl_schedule_node *node = isl_schedule_get_root(sa->schedule);

    for (int i = 0; i < isl_map_n_basic_map(map); i++)
    {
        isl_basic_map *dep = isl_basic_map_list_get_basic_map(bmap_list, i);
        struct dep_space_test_internal_data internal_data = {NULL, dep};
        int is_carried_at_space = !isl_schedule_node_every_descendant(node,
                                                                      not_carried_at_space, &internal_data);
        if (is_carried_at_space && data->dep_type == AUTOSA_DEP_RAR)
            data->score += 1;
        else if (!is_carried_at_space && data->dep_type == AUTOSA_DEP_RAW)
            data->score += 1;

        isl_vec_free(internal_data.dirvec);
        isl_basic_map_free(dep);
    }
    isl_schedule_node_free(node);
    isl_basic_map_list_free(bmap_list);
    return isl_bool_true;
}

/* Select one systolic array design based on heuristics. 
 * Heuristic:
 * We favor designs with the following features:
 * - RAR carried by space loops. 
 * - RAW carried by time loops. 
 * We compute a score for each design and select the one with the highest score.
 * The score is computed as :
 * score = 1 * (RAR carried by space || RAW carried by time loop)
 * Namely, for each dependnece, if it is a RAR carried by space or a RAW carried by 
 * time loops, it will contriute one credit to the total score.
 * Besides, between 1D and 2D systolic arrays, we prefer 2D systolic arrays for now.
 */
struct autosa_kernel *sa_candidates_smart_pick(
    struct autosa_kernel **sa_list, __isl_keep isl_size num_sa)
{
    assert(num_sa > 0);
    int max_score = -1;
    struct autosa_kernel *sa_opt;
    int opt_id;
    isl_union_map *dep_rar, *dep_flow;

    for (int i = 0; i < num_sa; i++)
    {
        struct autosa_kernel *sa = sa_list[i];
        struct sa_candidates_smart_pick_update_data data;
        data.score = 0;
        data.sa = sa;
        /* Initialize the autosa_loop_types. */
        sa_loop_init(sa);
        /* Set up the space_time properties. */
        sa_space_time_loop_setup(sa);

        dep_rar = sa->scop->tagged_dep_rar;
        dep_flow = sa->scop->tagged_dep_flow;

        data.dep_type = AUTOSA_DEP_RAR;
        isl_union_map_every_map(dep_rar, &sa_candidates_smart_pick_update, &data);
        data.dep_type = AUTOSA_DEP_RAW;
        isl_union_map_every_map(dep_flow, &sa_candidates_smart_pick_update, &data);
        /* Add one more credit for 2D arrays. */
        if (sa->n_sa_dim == 2)
            data.score += 1;
        if (data.score > max_score)
        {
            opt_id = i;
            max_score = data.score;
        }
        //#ifdef _DEBUG
        //    DBGVAR(std::cout, i);
        //    DBGVAR(std::cout, data.score);
        //#endif
    }

    sa_opt = autosa_kernel_copy(sa_list[opt_id]);

    for (int i = 0; i < num_sa; i++)
        autosa_kernel_free(sa_list[i]);
    free(sa_list);

    return sa_opt;
}

/* Return the selected systolic array design and free the rest. */
struct autosa_kernel *sa_candidates_manual_pick(struct autosa_kernel **sa_list,
                                                isl_size num_sa, int sa_id)
{
    struct autosa_kernel *sa_opt = autosa_kernel_copy(sa_list[sa_id]);

    for (int i = 0; i < num_sa; i++)
        autosa_kernel_free(sa_list[i]);
    free(sa_list);

    return sa_opt;
}

/* Create the array of autosa_local_array_info structures "array"
 * inside "kernel". The number of elements in this array is 
 * the same as the number of arrays in "prog".
 * Initialize the "array" field of each local array to point 
 * to the corresponding array in "prog".
 */
static struct autosa_kernel *autosa_kernel_create_local_arrays(
    struct autosa_kernel *kernel, struct autosa_prog *prog)
{
    int i;
    isl_ctx *ctx;

    if (!kernel)
        return NULL;

    ctx = isl_set_get_ctx(prog->context);
    kernel->array = isl_calloc_array(ctx,
                                     struct autosa_local_array_info, prog->n_array);
    if (!kernel->array)
        return (struct autosa_kernel *)autosa_kernel_free(kernel);
    kernel->n_array = prog->n_array;

    for (i = 0; i < prog->n_array; i++)
    {
        kernel->array[i].array = &prog->array[i];
        prog->array[i].local_array = &kernel->array[i];
        /* Initialize the fields. */
        kernel->array[i].n_io_group_refs = 0;
        kernel->array[i].n_mem_ports = 0;
        kernel->array[i].host_serialize = 0;
        kernel->array[i].serialize_bound = NULL;
    }

    return kernel;
}

/* Internal data struct used for sa_io_update. */
struct data_transfer_opt_data
{
    struct autosa_stmt_access *access;
    struct autosa_kernel *kernel;
    enum autosa_dep_type dep_type;
    isl_bool is_update;
};

/* If dependence is carried by the space loop, then mark it with the access 
 * as exterior I/O; otherwise, mark it as the interior I/O.
 * In addition, update the dependence vector.
 */
isl_stat data_transfer_update(__isl_keep isl_basic_map *dep, struct data_transfer_opt_data *data)
{
    struct autosa_stmt_access *access = data->access;
    struct autosa_kernel *kernel = data->kernel;
    isl_id *src_id, *dest_id;
    isl_space *space;
    isl_space *src_space, *dest_space;
    isl_schedule_node *node;

    /* Test if the access is associated with the current dep. */
    space = isl_basic_map_get_space(dep);
    src_space = isl_space_unwrap(isl_space_domain(isl_space_copy(space)));
    dest_space = isl_space_unwrap(isl_space_range(space));
    src_id = isl_space_get_tuple_id(src_space, isl_dim_out);
    dest_id = isl_space_get_tuple_id(dest_space, isl_dim_out);
    isl_space_free(src_space);
    isl_space_free(dest_space);

    if (src_id != access->ref_id && dest_id != access->ref_id)
    {
        isl_id_free(src_id);
        isl_id_free(dest_id);
        return isl_stat_ok;
    }
    isl_id_free(src_id);
    isl_id_free(dest_id);

    /* Test if the dependence is carried at the space loop. */
    struct dep_space_test_internal_data internal_data = {NULL, dep};
    node = isl_schedule_get_root(kernel->schedule);
    int is_carried_at_space = !isl_schedule_node_every_descendant(
        node, not_carried_at_space, &internal_data);
    if (is_carried_at_space)
    {
        access->io_info = (struct autosa_io_info **)realloc(
            access->io_info, sizeof(struct autosa_io_info *) * (++access->n_io_info));
        access->io_info[access->n_io_info - 1] =
            (struct autosa_io_info *)malloc(sizeof(struct autosa_io_info));
        access->io_info[access->n_io_info - 1]->io_type = AUTOSA_EXT_IO;
        access->io_info[access->n_io_info - 1]->dep =
            (struct autosa_dep *)calloc(1, sizeof(struct autosa_dep));
        access->io_info[access->n_io_info - 1]->dep->isl_dep = isl_basic_map_copy(dep);
        access->io_info[access->n_io_info - 1]->dep->type = data->dep_type;
        access->io_info[access->n_io_info - 1]->dir = internal_data.dirvec;
        access->io_info[access->n_io_info - 1]->old_dir = isl_vec_dup(internal_data.dirvec);        
    }
    else
    {
        access->io_info = (struct autosa_io_info **)realloc(
            access->io_info, sizeof(struct autosa_io_info *) * (++access->n_io_info));
        access->io_info[access->n_io_info - 1] =
            (struct autosa_io_info *)malloc(sizeof(struct autosa_io_info));
        access->io_info[access->n_io_info - 1]->io_type = AUTOSA_INT_IO;
        access->io_info[access->n_io_info - 1]->dep =
            (struct autosa_dep *)calloc(1, sizeof(struct autosa_dep));
        access->io_info[access->n_io_info - 1]->dep->isl_dep = isl_basic_map_copy(dep);
        access->io_info[access->n_io_info - 1]->dep->type = data->dep_type;
        access->io_info[access->n_io_info - 1]->dir = internal_data.dirvec;
        access->io_info[access->n_io_info - 1]->old_dir = isl_vec_dup(internal_data.dirvec);        
    }

    isl_schedule_node_free(node);
    data->is_update = isl_bool_true;

    return isl_stat_ok;
}

/* Examine each dependence as basic maps in the "map".
 */
static isl_bool data_transfer_update_wrap(__isl_keep isl_map *map, void *user)
{
    isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(map);
    for (int i = 0; i < isl_map_n_basic_map(map); i++)
    {
        isl_basic_map *dep = isl_basic_map_list_get_basic_map(bmap_list, i);
        struct data_transfer_opt_data *opt_data = (struct data_transfer_opt_data *)user;
        data_transfer_update(dep, opt_data);
        isl_basic_map_free(dep);
    }
    isl_basic_map_list_free(bmap_list);
    return isl_bool_true;
}

/* This function extracts the communication pairs from the kernel.
 * Each access is paired with the dependence it is associated with.
 * We consider three types of deps: RAR, RAW, WAW.
 * For each comm pair <access, dep>, we update two properties:
 * - I/O type: exterior I/O or interior I/O.
 * - I/O direction: the dependence vector on the space loops.
 */
static isl_stat sa_io_update(struct autosa_kernel *sa)
{
    struct autosa_local_array_info *local_array;
    /* Initialize the IO info */
    for (int i = 0; i < sa->n_array; i++)
    {
        local_array = &sa->array[i];
        for (int j = 0; j < sa->array[i].array->n_ref; j++)
        {
            struct autosa_stmt_access *access = sa->array[i].array->refs[j];
            access->n_io_info = 0;
            access->io_info = NULL;
        }
        local_array->n_lane = 0;
        local_array->array->n_lane = 0;
    }

    /* Update the IO information */
    for (int i = 0; i < sa->n_array; i++)
    {
        local_array = &sa->array[i];
        for (int j = 0; j < local_array->array->n_ref; j++)
        {
            struct autosa_stmt_access *access = local_array->array->refs[j];
            isl_union_map *dep_rar = sa->scop->tagged_dep_rar;
            isl_union_map *dep_flow = sa->scop->tagged_dep_flow;
            isl_union_map *dep_waw = sa->scop->tagged_dep_waw;
            struct data_transfer_opt_data opt_data =
                {access, sa, AUTOSA_DEP_UNKNOWN, isl_bool_false};

            opt_data.dep_type = AUTOSA_DEP_RAR;
            isl_union_map_every_map(dep_rar, &data_transfer_update_wrap, &opt_data);
            if (opt_data.is_update == isl_bool_true)
            {
                local_array->array_type = AUTOSA_EXT_ARRAY;
                opt_data.is_update = isl_bool_false;
            }

            opt_data.dep_type = AUTOSA_DEP_RAW;
            isl_union_map_every_map(dep_flow, &data_transfer_update_wrap, &opt_data);
            if (opt_data.is_update == isl_bool_true)
            {
                local_array->array_type = AUTOSA_INT_ARRAY;
                opt_data.is_update = isl_bool_false;
            }

            opt_data.dep_type = AUTOSA_DEP_WAW;
            isl_union_map_every_map(dep_waw, &data_transfer_update_wrap, &opt_data);
        }
    }

    return isl_stat_ok;
}

void extract_sa_dims_from_node(__isl_keep isl_schedule_node *node, int *sa_dims, int n_sa_dim)
{
    int *ubs;
    ubs = extract_band_upper_bounds(node);
    for (int i = 0; i < n_sa_dim; i++) {
        sa_dims[i] = ubs[i];
    }
    free(ubs);    
}

/* Apply array partitioning.
 * Apply loop tiling on the band that contains the space loops.
 * In addition, if L2 array partitioning is abled, we will tile the tile loops
 * from the previous array partitioning again to generate two-level tiling.
 * TODO: Reorganize the array partitioning loops and place them following the
 * ascending order of the dependence distances. 
 * 
 * en: enable signal for array partitioning.
 * mode: opt mode for array partitioning.
 * L2_en: enable signal for L2 array partitioning.
 * L2_mode: opt mode for L2 array partitioning.
 */
isl_stat sa_array_partitioning_optimize(struct autosa_kernel *sa,
                                        bool en, char *mode, bool L2_en, char *L2_mode)
{
    int tile_len;
    isl_schedule *schedule;
    int *tile_size;
    isl_id *id;

    /* Fetch the band that contains the space loops. */
    isl_schedule_node *node;
    if (sa->type == AUTOSA_SA_TYPE_SYNC)
    {
        node = get_innermost_permutable_node(sa->schedule);
    }
    else if (sa->type == AUTOSA_SA_TYPE_ASYNC)
    {
        node = get_outermost_permutable_node(sa->schedule);
    }
    else
    {
        isl_die(sa->ctx, isl_error_invalid,
                "systolic array type not supported", return isl_stat_error);
    }

//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node))
//#endif

    if (!en)
    {
        /* Array partitioning is disabled, we will simply add an "array" mark before
         * the space band and return.
         */
        id = isl_id_alloc(sa->ctx, "array", NULL);
        node = isl_schedule_node_insert_mark(node, id);

        isl_schedule_free(sa->schedule);
        sa->schedule = isl_schedule_node_get_schedule(node);
        isl_schedule_node_free(node);
        return isl_stat_ok;
    }

    printf("[AutoSA] Apply array partitioning.\n");

    /* Mark the loop properties. */
    for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
    {
        node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_array_part);
    }
    schedule = isl_schedule_node_get_schedule(node);

    if (sa->scop->options->autosa->verbose)
    {
        /* Display the candidate loops. */
        isl_printer *p = isl_printer_to_file(sa->ctx, stdout);
        p = isl_printer_set_yaml_style(p, ISL_YAML_STYLE_BLOCK);
        p = isl_printer_print_schedule(p, schedule);
        printf("\n");
        isl_printer_free(p);
    }
    isl_schedule_free(schedule);

    tile_len = isl_schedule_node_band_n_member(node);
    if (!strcmp(mode, "manual"))
    {
        /* Manual mode */
        tile_size = read_array_part_tile_sizes(sa, tile_len);
        if (!tile_size)
        {
            /* User hasn't specified the tiling factors for array partitioning yet,
             * we will dump out the number and upper bounds of array_part loops 
             * and exit the program. */
            int *ubs = extract_band_upper_bounds(node);
            FILE *fp;
            char *content;
            cJSON *tuning, *array_part_json, *loops_json, *n_sa_dim_json;
            isl_printer *p_str;
            char *tuning_path;

            tuning = cJSON_CreateObject();
            array_part_json = cJSON_CreateObject();
            cJSON_AddItemToObject(tuning, "array_part", array_part_json);
            loops_json = cJSON_CreateArray();
            cJSON_AddItemToObject(array_part_json, "tilable_loops", loops_json);
            for (int i = 0; i < tile_len; i++)
            {
                cJSON *loop = cJSON_CreateNumber(ubs[i]);
                cJSON_AddItemToArray(loops_json, loop);
            }
            /* Add the sa_dim */
            n_sa_dim_json = cJSON_CreateNumber(sa->n_sa_dim);
            cJSON_AddItemToObject(array_part_json, "n_sa_dim", n_sa_dim_json);
            p_str = isl_printer_to_str(sa->ctx);
            p_str = isl_printer_print_str(p_str, sa->options->autosa->output_dir);
            p_str = isl_printer_print_str(p_str, "/tuning.json");
            tuning_path = isl_printer_get_str(p_str);
            fp = fopen(tuning_path, "w");
            content = cJSON_Print(tuning);
            fprintf(fp, "%s", content);
            cJSON_Delete(tuning);
            isl_printer_free(p_str);
            free(tuning_path);
            exit(0);
        }
    }
    else
    {
        /* Auto mode.
         * Perform the array partitioning following the default policy. */
        tile_size = read_default_array_part_tile_sizes(sa, tile_len);
    }

    /* Tile the band. */
    if (!tile_size)
    {
        isl_schedule_node_free(node);
        return isl_stat_error;
    }    
    /* Examine if all tiling factors are -1, in that case, we will skip array 
     * partitioning. 
     */
    int c;
    for (c = 0; c < tile_len; c++) {
        if (tile_size[c] != -1)
            break;
    }
    if (c == tile_len) {
        id = isl_id_alloc(sa->ctx, "array", NULL);
        node = isl_schedule_node_insert_mark(node, id);
        node = isl_schedule_node_child(node, 0);
        extract_sa_dims_from_node(node, sa->sa_dim, sa->n_sa_dim);

        free(tile_size);
        isl_schedule_free(sa->schedule);
        sa->schedule = isl_schedule_node_get_schedule(node);
        isl_schedule_node_free(node);
        return isl_stat_ok;
    }
    /* For now, our codegen doesn't support arrays with size one at any dim. 
     * We will examine if array size is one at any dimension, and return if found. 
     */
    for (int i = 0; i < sa->n_sa_dim; i++)
    {
        if ((sa->type == AUTOSA_SA_TYPE_SYNC && tile_size[tile_len - sa->n_sa_dim + i] == 1) ||
           (sa->type == AUTOSA_SA_TYPE_ASYNC && tile_size[i] == 1)) {            
            /* Skip the array partition. */
            id = isl_id_alloc(sa->ctx, "array", NULL);
            node = isl_schedule_node_insert_mark(node, id);
            node = isl_schedule_node_child(node, 0);
            extract_sa_dims_from_node(node, sa->sa_dim, sa->n_sa_dim);

            free(tile_size);
            isl_schedule_free(sa->schedule);
            sa->schedule = isl_schedule_node_get_schedule(node);
            isl_schedule_node_free(node);
            return isl_stat_ok;
        }
    }

    /* Update the systolic aray dimensions. 
     * TODO: should use barvinok to handle this case.
     */
    //if (sa->type == AUTOSA_SA_TYPE_SYNC)
    //{
    //    for (int i = 0; i < sa->n_sa_dim; i++)
    //    {
    //        sa->sa_dim[i] = tile_size[tile_len - sa->n_sa_dim + i];
    //    }
    //}
    //else
    //{
    //    for (int i = 0; i < sa->n_sa_dim; i++)
    //    {
    //        sa->sa_dim[i] = tile_size[i];
    //    }
    //}
    sa->array_part_w = tile_len;

    node = autosa_tile_band(node, tile_size);
    free(tile_size);
    node = isl_schedule_node_child(node, 0);
    extract_sa_dims_from_node(node, sa->sa_dim, sa->n_sa_dim);
    node = isl_schedule_node_parent(node);

    /* Reorder the array part loops based on the dependence distance. 
     */
    node = reorder_band_by_dep_dis(node);

    /* Add the array marker */
    node = isl_schedule_node_child(node, 0);
    id = isl_id_alloc(sa->ctx, "array", NULL);
    node = isl_schedule_node_insert_mark(node, id);
    node = isl_schedule_node_parent(node);

    /* Examine if there is any flow dep carried in the array_part band. 
     * For this case, we need to implement a credit-based dependence queue to 
     * force the possible data dependence between two array partitions. 
     * TODO: implement this feature. 
     */
    if (!sa->options->autosa->credit_control)
    {
        for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
        {
            if (!isl_schedule_node_band_member_get_coincident(node, i))
            {
                printf("[AutoSA] Warning: Flow deps carried in the array partitioning band.\n");
                printf("[AutoSA] Warning: Using simple task pipelining could lead to potential data hazards.\n");
                printf("[AutoSA] Warning: The program will proceed as usual. You could consider enabling credit control.\n");
                break;
            }
        }
    }
    else
    {
        printf("[AutoSA] Error: Credit control is not supported yet!\n");
        exit(1);
        // TODO: modify the schedule to add credit rd/wr for I/O modules
        // TODO: modify the module decls and fifo decls for credit fifos
        // TODO: disable double buffering.
        //    /* Disable double-buffering */
        //    sa->options->autosa->double_buffer = 0;
    }

    /* If two-level buffering is enabled, we will need to apply a second-level tiling
   * on the tile band from the previous array partitioning. 
   * Namely, after array partitioning, we get two bands:
   * T
   * |
   * P
   * To support two-level buffering, we will tile the band T again:
   * T1
   * |
   * T2
   * |
   * P
   */
    if (sa->options->autosa->two_level_buffer)
    {
        if (L2_en)
        {
            /* Tile the band again */
            printf("[AutoSA] Two-level buffering is set. Apply second-level array partitioning.\n");
            tile_len = isl_schedule_node_band_n_member(node);
            if (!strcmp(mode, "manual"))
            {
                tile_size = read_array_part_L2_tile_sizes(sa, tile_len);
                if (!tile_size)
                {
                    /* Dump out the number of and upper bounds of array_part loops and exit the program. */
                    int *ubs = extract_band_upper_bounds(node);
                    int *loop_coincident = (int *)malloc(sizeof(int) * tile_len);
                    FILE *fp;
                    char *content;
                    cJSON *tuning, *array_part_json, *loops_json;
                    isl_printer *p_str;
                    char *tuning_path;

                    for (int i = 0; i < tile_len; i++)
                    {
                        loop_coincident[i] = isl_schedule_node_band_member_get_coincident(node, i);
                    }

                    tuning = cJSON_CreateObject();
                    array_part_json = cJSON_CreateObject();
                    cJSON_AddItemToObject(tuning, "array_part_L2", array_part_json);
                    loops_json = cJSON_CreateArray();
                    cJSON_AddItemToObject(array_part_json, "tilable_loops", loops_json);
                    for (int i = 0; i < tile_len; i++)
                    {
                        cJSON *loop = cJSON_CreateNumber(ubs[i]);
                        cJSON_AddItemToArray(loops_json, loop);
                    }
                    loops_json = cJSON_CreateArray();
                    cJSON_AddItemToObject(array_part_json, "coincident", loops_json);
                    for (int i = 0; i < tile_len; i++)
                    {
                        cJSON *loop = cJSON_CreateNumber(loop_coincident[i]);
                        cJSON_AddItemToArray(loops_json, loop);
                    }
                    p_str = isl_printer_to_str(sa->ctx);
                    p_str = isl_printer_print_str(p_str, sa->options->autosa->output_dir);
                    p_str = isl_printer_print_str(p_str, "/tuning.json");
                    tuning_path = isl_printer_get_str(p_str);
                    fp = fopen(tuning_path, "w");
                    content = cJSON_Print(tuning);
                    fprintf(fp, "%s", content);
                    cJSON_Delete(tuning);
                    free(tuning_path);
                    free(loop_coincident);
                    isl_printer_free(p_str);
                    free(ubs);
                    exit(0);
                }
            }
            else
            {
                /* Perform second-level array partitioning following the default policy. */
                // tile_size = read_default_array_part_L2_tile_sizes(sa, tile_len);
                int *ubs = extract_band_upper_bounds(node);
                tile_size = isl_alloc_array(sa->ctx, int, tile_len);
                for (int i = 0; i < tile_len; i++)
                {
                    tile_size[i] = ubs[i];
                }
                free(ubs);
            }

            if (!tile_size)
            {
                isl_schedule_node_free(node);
                return isl_stat_error;
            }
            node = autosa_tile_band(node, tile_size);
            free(tile_size);

            /* Add the second-level array mark */
            node = isl_schedule_node_child(node, 0);
            id = isl_id_alloc(sa->ctx, "array_L2", NULL);
            node = isl_schedule_node_insert_mark(node, id);
            node = isl_schedule_node_parent(node);
        }
        else
        {
            /* Disable the L2 array partitioning */
            sa->options->autosa->two_level_buffer = 0;
        }
    }

    /* Clean up the band pe_opt properties. */
    schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);
    schedule = isl_schedule_map_schedule_node_bottom_up(
        schedule, &clear_pe_opt_prop, NULL);

    isl_schedule_free(sa->schedule);
    sa->schedule = schedule;

    return isl_stat_ok;
}

/* Insert an "hls_pipeline" mark under the last time loop */
static __isl_give isl_schedule_node *add_hls_pipeline(
    __isl_take isl_schedule_node *node, void *user)
{
    struct autosa_kernel *sa = (struct autosa_kernel *)user;
    isl_ctx *ctx = sa->ctx;

    if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
        return node;

    /* Examine if the node is innermost */
    node = isl_schedule_node_child(node, 0);
    isl_bool no_inner_band = isl_schedule_node_every_descendant(node,
                                                                &no_permutable_node, NULL);
    node = isl_schedule_node_parent(node);
    if (!no_inner_band)
        return node;

    int n = isl_schedule_node_band_n_member(node);

    if (sa->type == AUTOSA_SA_TYPE_ASYNC)
    {
        if (isl_schedule_node_band_member_get_space_time(node, n - 1) == autosa_loop_time)
        {
            isl_id *id;
            id = isl_id_alloc(ctx, "hls_pipeline", NULL);
            node = isl_schedule_node_child(node, 0);
            node = isl_schedule_node_insert_mark(node, id);
            node = isl_schedule_node_parent(node);
        }
    }
    else if (sa->type == AUTOSA_SA_TYPE_SYNC)
    {
        /* Go to the innermost band with time loops. */
        if (isl_schedule_node_band_member_get_space_time(node, 0) != autosa_loop_time)
        {
            node = isl_schedule_node_parent(node);
            while (isl_schedule_node_get_type(node) != isl_schedule_node_band &&
                   isl_schedule_node_has_parent(node))
            {
                node = isl_schedule_node_parent(node);
            }
        }
        if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
        {
            n = isl_schedule_node_band_n_member(node);
            for (int i = n - 1; i >= 0; i--)
            {
                if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_time)
                {
                    isl_id *id = isl_id_alloc(ctx, "hls_pipeline", NULL);
                    if (i != n - 1)
                    {
                        node = isl_schedule_node_band_split(node, i + 1);
                    }
                    node = isl_schedule_node_child(node, 0);
                    node = isl_schedule_node_insert_mark(node, id);
                    node = isl_schedule_node_parent(node);
                    break;
                }
            }
        }
    }

    return node;
}

/* Internal struct used for latency_opt_check */
struct latency_opt_check_data
{
    struct autosa_kernel *kernel;
    int is_required;
};

/* Check if the innermost time loop is parallel.
 * If this loop is parallel, it can be used for latency hiding and 
 * there is no need for further optimization.
 * We will split off this loop from the band, and attach a "latency"
 * marker above it.
 */
static __isl_give isl_schedule_node *latency_opt_check(
    __isl_take isl_schedule_node *node, void *user)
{
    struct latency_opt_check_data *data = (struct latency_opt_check_data *)user;
    struct autosa_kernel *sa = data->kernel;
    isl_ctx *ctx = sa->ctx;

    if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
        return node;

    /* Examine if the node is innermost */
    node = isl_schedule_node_child(node, 0);
    isl_bool no_inner_band = isl_schedule_node_every_descendant(node,
                                                                &no_permutable_node, NULL);
    node = isl_schedule_node_parent(node);
    if (!no_inner_band)
        return node;

    int n = isl_schedule_node_band_n_member(node);

    if (sa->type == AUTOSA_SA_TYPE_ASYNC)
    {
        if (isl_schedule_node_band_member_get_coincident(node, n - 1) &&
            isl_schedule_node_band_member_get_space_time(node, n - 1) == autosa_loop_time)
        {
            //isl_id *id;
            data->is_required = 0;
            ///* Split off the loop and attach a "latency" mark */
            //if (n > 1)
            //{
            //    node = isl_schedule_node_band_split(node, n - 1);
            //    node = isl_schedule_node_child(node, 0);
            //}
            //id = isl_id_alloc(ctx, "latency", NULL);
            //node = isl_schedule_node_insert_mark(node, id);
            //node = isl_schedule_node_parent(node);
        }
    }
    else if (sa->type == AUTOSA_SA_TYPE_SYNC)
    {        
        if (isl_schedule_node_band_member_get_space_time(node, 0) != autosa_loop_time)
        {
            node = isl_schedule_node_parent(node);
            while (isl_schedule_node_get_type(node) != isl_schedule_node_band &&
                   isl_schedule_node_has_parent(node))
            {
                node = isl_schedule_node_parent(node);
            }
        }
        if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
        {
            n = isl_schedule_node_band_n_member(node);
            for (int i = n - 1; i >= 0; i--)
            {
                if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_time)
                {
                    if (isl_schedule_node_band_member_get_coincident(node, i))
                    {
                        //isl_id *id;
                        data->is_required = 0;
                        ///* Split off the time loop */
                        //if (i > 1)
                        //{
                        //    node = isl_schedule_node_band_split(node, i);
                        //    node = isl_schedule_node_child(node, 0);
                        //}
                        //if (n - i - 1 > 0)
                        //{
                        //    node = isl_schedule_node_band_split(node, 1);
                        //}
                        //id = isl_id_alloc(ctx, "latency", NULL);
                        //node = isl_schedule_node_insert_mark(node, id);
                        //node = isl_schedule_node_parent(node);
                    }
                    break;
                }
            }
        }
    }

    return node;
}

/* Mark parallel loop as latency_hiding candidate loop. 
 */
static isl_schedule_node *detect_latency_hiding_loop(__isl_take isl_schedule_node *node, void *user)
{
    struct autosa_kernel *sa = (struct autosa_kernel *)user;

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
        {
            if (isl_schedule_node_band_member_get_coincident(node, i))
            {
                node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_latency);
            }
        }
    }

    return node;
}

/* Examine if the node is the last band node.
 * If so, add a "latency" mark before the node. 
 */
static __isl_give isl_schedule_node *add_latency_mark(
    __isl_take isl_schedule_node *node, void *user)
{
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        node = isl_schedule_node_child(node, 0);
        isl_bool no_inner_band = isl_schedule_node_every_descendant(node,
                                                                    &no_permutable_node, NULL);
        node = isl_schedule_node_parent(node);
        if (no_inner_band)
        {
            /* Insert the "latency" mark. */
            isl_id *id = isl_id_alloc(isl_schedule_node_get_ctx(node), "latency", NULL);
            node = isl_schedule_node_insert_mark(node, id);
        }
    }

    return node;
}

/* Sink the current node (latency hiding loop) as the last time loop. 
 * If the array is async, then sink the node to the bottom.
 * If the array is sync, then lift it up and insert it as the last loop 
 * in the time band.
 */
__isl_give isl_schedule_node *autosa_latency_node_band_sink_time(
    __isl_take isl_schedule_node *node, struct autosa_kernel *sa)
{
    if (sa->type == AUTOSA_SA_TYPE_ASYNC)
    {
#ifdef ISL_SINK        
        node = isl_schedule_node_band_sink(node);
        /* Add the "latency" mark. */
        node = isl_schedule_node_map_descendant_bottom_up(
            node, &add_latency_mark, NULL);
#else            
        node = autosa_node_sink_to_mark(node, "latency");
#endif
    }
    else if (sa->type == AUTOSA_SA_TYPE_SYNC)
    {
        /* Move up to the node that contains the space loop.
     * The current node should be right below the space band.
     */
        node = isl_schedule_node_parent(node);

        /* Find the position of the first space loop. */
        int n_member = isl_schedule_node_band_n_member(node);
        int space_pos;
        for (int i = 0; i < n_member; i++)
        {
            if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_space)
            {
                space_pos = i;
                break;
            }
        }
        if (space_pos == 0)
        {
            /* Interchange the current node with the child node. */
            node = autosa_node_interchange(node);
            /* Insert the "latency" mark. */
            isl_id *id = isl_id_alloc(sa->ctx, "latency", NULL);
            node = isl_schedule_node_insert_mark(node, id);
            node = isl_schedule_node_child(node, 0);
            node = isl_schedule_node_child(node, 0);
        }
        else
        {
            node = isl_schedule_node_band_split(node, space_pos);
            node = isl_schedule_node_child(node, 0);
            /* Interchange the current node with the child node. */
            node = autosa_node_interchange(node);
            /* Insert the "latency" mark. */
            isl_id *id = isl_id_alloc(sa->ctx, "latency", NULL);
            node = isl_schedule_node_insert_mark(node, id);
            node = isl_schedule_node_child(node, 0);
            node = isl_schedule_node_child(node, 0);
        }
    }

    return node;
}

/* Given each node band, tile the candidate loop and permute it innermost in the time
 * loop band. 
 * If the tile size is no greater than 1, the candidate loop is skipped.
 * For each point loop, a "latency" mark is added.
 */
static __isl_give isl_schedule_node *autosa_latency_tile_band_loop(
    __isl_take isl_schedule_node *node, void *user)
{
    struct autosa_pe_opt_tile_data *data = (struct autosa_pe_opt_tile_data *)user;
    if (isl_schedule_node_get_type(node) != isl_schedule_node_band)
        return node;

// Hack: For 2D GEMM, reverse the latency hiding order
    int n;
    isl_id *id;
    n = isl_schedule_node_band_n_member(node);

#ifndef REVERSE_ORDER    
    for (int i = 0; i < n; i++)
#else    
    for (int i = n - 1; i >= 0; i--)
#endif    
    {
        if (isl_schedule_node_band_member_get_pe_opt(node, i) == autosa_loop_latency)
        {
#ifdef REVERSE_ORDER
            int loop_tile_size = data->tile_size[data->tile_len - data->n_touched_loop - 1];            
#else
            int loop_tile_size = data->tile_size[data->n_touched_loop];
#endif            
            (data->n_touched_loop)++;
            /* If latency hiding is applied on the space loops, we need to update
             * the SA dimensions. 
             */
            if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_space)
            {
                /* Figure out the dim position. */
                int touched_space_loop = 0;
                for (int j = 0; j < i; j++)
                {
                    if (isl_schedule_node_band_member_get_space_time(node, j) == autosa_loop_space)
                        touched_space_loop++;
                }
                data->sa->sa_dim[touched_space_loop] /= loop_tile_size;
                if (data->sa->sa_dim[touched_space_loop] == 1) {
                    throw std::runtime_error("[AutoSA] Error: Array dimension as 1 is not supported!");
                }
            }

            /* Skip loop tile size as 1 */
            if (loop_tile_size > 1)
            {                
                /* Tile the current loop and permute it to be the innermost time loop.
                 * Specifically, tile the loop in the band at "i"th position with the 
                 * size "loop_tile_size".
                 * The returned node points at the tile loop. */
                node = autosa_node_band_tile_loop(node, loop_tile_size, i);
                /* Reset the candidate loop in the tile loop the pe_opt property to default. */
                node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_default);
                /* Reset the point loop space_time property to time loop. */
                node = isl_schedule_node_child(node, 0);
                node = isl_schedule_node_band_member_set_space_time(node, 0, autosa_loop_time);
                /* Reset the point loop pe_opt property to default .*/
                node = isl_schedule_node_band_member_set_pe_opt(node, 0, autosa_loop_default);
                /* Move the single loop node to the bottom of the time band. */
                node = autosa_latency_node_band_sink_time(node, data->sa);                
                (data->n_tiled_loop)++;
                return node;
            }
            else
            {
                /* Reset the pe_opt property */
                node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_default);
            }
        }
    }

    return node;
}

/* Internal struct for count_latency_hiding_loop. */
struct count_latency_hiding_loop_data
{
    int tile_len;
    int *ubs;
    struct autosa_kernel *kernel;
};

/* Count the number of latency hiding candidate loops.
 * Extract the loop upper bounds of the candidate loops.
 */
static isl_bool count_latency_hiding_loop(
    __isl_keep isl_schedule_node *node, void *user)
{
    struct count_latency_hiding_loop_data *data =
        (struct count_latency_hiding_loop_data *)user;
    isl_schedule_node *node_copy;

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        int n = isl_schedule_node_band_n_member(node);
        for (int i = 0; i < n; i++)
        {
            if (isl_schedule_node_band_member_get_pe_opt(node, i) == autosa_loop_latency)
            {
                data->tile_len = data->tile_len + 1;
                /* Extract the loop upper bound */
                node_copy = isl_schedule_node_copy(node);
                if (i > 0)
                {
                    node_copy = isl_schedule_node_band_split(node_copy, i);
                    node_copy = isl_schedule_node_child(node_copy, 0);
                }
                if (n - i - 1 > 0)
                {
                    node_copy = isl_schedule_node_band_split(node_copy, 1);
                }
                int *ubs = extract_band_upper_bounds(node_copy);
                data->ubs = (int *)realloc(data->ubs, sizeof(int) * data->tile_len);
                data->ubs[data->tile_len - 1] = ubs[0];
                isl_schedule_node_free(node_copy);
                free(ubs);
            }
        }
    }

    return isl_bool_true;
}

/* Perform the latency hiding in either "Manual" or "Auto" mode.
 * We will tile each loop with a tiling factor greater than one, and place
 * the point loop as the innermost time loop. 
 * A "latency" mark is placed before this loop.
 * A "hls_pipeline" mark is placed under this loop.
 */
static __isl_give isl_schedule_node *autosa_latency_tile_loop(
    __isl_take isl_schedule_node *node, struct autosa_kernel *sa, char *mode)
{
    int tile_len;
    int *tile_size;
    struct count_latency_hiding_loop_data data;
    data.tile_len = 0;
    data.ubs = NULL;
    data.kernel = sa;
    int i;

    /* Count the candidate loop number and extract the loop upper bounds. */
    isl_schedule_node_foreach_descendant_top_down(
        node, &count_latency_hiding_loop, &data);
    tile_len = data.tile_len;

    if (!strcmp(mode, "manual"))
    {
        tile_size = read_latency_tile_sizes(sa, tile_len);
        if (!tile_size)
        {
            /* Dump out the number and upper bounds of latency loops and exit the program. */
            int *ubs = data.ubs;
            FILE *fp;
            char *content;
            cJSON *tuning, *latency_json, *loops_json;
            char *tuning_path;
            isl_printer *p_str;

            tuning = cJSON_CreateObject();
            latency_json = cJSON_CreateObject();
            cJSON_AddItemToObject(tuning, "latency", latency_json);
            loops_json = cJSON_CreateArray();
            cJSON_AddItemToObject(latency_json, "tilable_loops", loops_json);
            for (int i = 0; i < tile_len; i++)
            {
                cJSON *loop = cJSON_CreateNumber(ubs[i]);
                cJSON_AddItemToArray(loops_json, loop);
            }
            p_str = isl_printer_to_str(sa->ctx);
            p_str = isl_printer_print_str(p_str, sa->options->autosa->output_dir);
            p_str = isl_printer_print_str(p_str, "/tuning.json");
            tuning_path = isl_printer_get_str(p_str);
            fp = fopen(tuning_path, "w");
            content = cJSON_Print(tuning);
            fprintf(fp, "%s", content);
            cJSON_Delete(tuning);
            isl_printer_free(p_str);
            free(tuning_path);
            exit(0);
        }
    }
    else
    {
        /* Perform the latency hiding following the default policy. */
        tile_size = read_default_latency_tile_sizes(sa, tile_len);
    }

    free(data.ubs);
    if (!tile_size)
    {
        isl_schedule_node_free(node);
        return NULL;
    }

    /* Examine if all the tiling factors are 1, in that case, we will
     * skip the tiling and split off the last time dimension to add a 
     * hls_pipeline mark. */
    for (i = 0; i < tile_len; i++)
    {
        if (tile_size[i] != -1)
            sa->lat_hide_len *= tile_size[i];
    }
    for (i = 0; i < tile_len; i++)
    {
        if (tile_size[i] > 1)
            break;
    }
    if (i == tile_len)
    {
        node = isl_schedule_node_map_descendant_bottom_up(node,
                                                          &add_hls_pipeline, sa);
    }
    else
    {
        /* Tile the candidate loops. */
        struct autosa_pe_opt_tile_data tile_data = {0, 0, tile_len, tile_size, sa};
        while (tile_data.n_touched_loop != tile_len)
        {
            node = isl_schedule_node_map_descendant_bottom_up(
                node, &autosa_latency_tile_band_loop, &tile_data);
        }
    }

    free(tile_size);
    return node;
}

/* Apply latency hiding. 
 * Go through all the loops, if there is any parallel loop (considering only RAW), 
 * such a loop will be identified as latency hiding loop candidate. 
 * Such loops will be tiled. The point loops will be permuted as 
 * the innermost time loops.
 * 
 * en: enable signal for the current stage.
 * mode: manual/auto
 */
isl_stat sa_latency_hiding_optimize(struct autosa_kernel *sa, bool en, char *mode)
{
    isl_bool opt_required;
    isl_schedule *schedule = sa->schedule;
    isl_schedule_node *node = isl_schedule_get_root(schedule);

    if (!en)
    {
        /* This stage is disabled.
         * We will peel off the last time loop and add an hls_pipeline mark as 
         * the innermost time loops are supposed to be pipelined on hardware. 
         */
        node = isl_schedule_node_map_descendant_bottom_up(node,
                                                          &add_hls_pipeline, sa);

        isl_schedule_free(sa->schedule);
        sa->schedule = isl_schedule_node_get_schedule(node);
        isl_schedule_node_free(node);
        return isl_stat_ok;
    }

    printf("[AutoSA] Apply latency hiding.\n");
    sa->lat_hide_len = 1;

    /* Move down to the array marker. */
    node = autosa_tree_move_down_to_array(node, sa->core);

    /* Check if the innermost time loop is parallel loop.
     * If so, there is no need to perform latency hiding, safely reutrn.
     */
    struct latency_opt_check_data data;
    data.kernel = sa;
    data.is_required = 1;
    //DBGSCHDNODE(stdout, node, sa->ctx);
    node = isl_schedule_node_map_descendant_bottom_up(node,
                                                      &latency_opt_check, &data);
    //DBGSCHDNODE(stdout, node, sa->ctx);
    if (!data.is_required)
    {
        //printf("[AutoSA] The innermost time loop is parallel. Latency hiding is skipped.\n");
        //isl_schedule_free(schedule);
        //schedule = isl_schedule_node_get_schedule(node);
        //isl_schedule_node_free(node);
        //sa->schedule = schedule;
        //// TODO: this will make the latency hiding stuck in the auto-tuning, fix it.
        //return isl_stat_ok;        
        printf("[AutoSA] The innermost time loop is parallel. Latency hiding is optional.\n");
    }

    /* Detect all candidate loops. */
    node = isl_schedule_node_map_descendant_bottom_up(
        node, &detect_latency_hiding_loop, sa);

    /* Display the candidate loops. */
    isl_schedule_free(schedule);
    schedule = isl_schedule_node_get_schedule(node);
    if (sa->scop->options->autosa->verbose)
    {
        isl_printer *p = isl_printer_to_file(sa->ctx, stdout);
        p = isl_printer_set_yaml_style(p, ISL_YAML_STYLE_BLOCK);
        p = isl_printer_print_schedule(p, schedule);
        printf("\n");
        isl_printer_free(p);
    }
    isl_schedule_free(schedule);

    /* Tile the candidate loop. 
     * For each candidate loop, if the loop is used for latency hiding,
     * it is tiled and permuted to the innermost of the time loop band. 
     * A latency hiding marker is added. */
    node = autosa_latency_tile_loop(node, sa, mode);

    /* Clean up the band pe_opt properties. */
    schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);
    schedule = isl_schedule_map_schedule_node_bottom_up(
        schedule, &clear_pe_opt_prop, NULL);

    sa->schedule = schedule;

    return isl_stat_ok;
}

/* Internal struct used in SIMD vectorization. */
struct simd_vectorization_data
{
    struct autosa_kernel *kernel;
    float *scores;
    int *legal;
    float best_score;
    int layout_trans;
    int n_loops;
    int loop_cnt;
    char *mode;
    int *ubs;
    int *tile_size;
    char *buffer;
    int buffer_offset;
    int has_space_candidate;
};

/* Internal struct used in is_stride_coalesced. */
struct stride_coalesced_data
{
    struct autosa_kernel *kernel;
    isl_union_map *prefix;
    float score;
    float num_accs;
    float num_layout_trans;
};

/* Examine if all the array references of the statement with the domain "set" 
 * has stride-0/stride-1 access.
 */
static isl_bool is_stride_coalesced_stmt(__isl_keep isl_set *set, void *user)
{
    isl_space *space;
    isl_id *id;
    struct autosa_stmt *stmt;
    struct stride_coalesced_data *data = (struct stride_coalesced_data *)user;
    struct autosa_stmt_access *accesses, *access;
    isl_map *prefix;

    space = isl_set_get_space(set);
    id = isl_space_get_tuple_id(space, isl_dim_set);
    isl_space_free(space);
    prefix = isl_map_from_union_map(isl_union_map_intersect_domain(
        isl_union_map_copy(data->prefix), isl_union_set_from_set(isl_set_copy(set))));
    stmt = find_stmt(data->kernel->prog, id);
    isl_id_free(id);
    accesses = stmt->accesses;
    for (access = accesses; access; access = access->next)
    {
        isl_map *acc;
        int n;
        isl_bool is_zero = isl_bool_false, is_one = isl_bool_false;
        isl_pw_multi_aff *pma;
        int i;

        /* Skip the scalar access */
        if (access->n_index == 0)
            continue;

        /* Transform the domain of access function to scheduling domains. */
        acc = isl_map_copy(access->access);
        acc = isl_map_apply_domain(acc, isl_map_copy(prefix));

        /* Try each dimension of the array. */
        for (i = access->n_index - 1; i >= 0; i--)
        {
            is_zero = access_is_stride_zero(acc, i);
            if (is_zero)
                break;
        }
        if (!is_zero)
        {
            for (i = access->n_index - 1; i >= 0; i--)
            {
                is_one = access_is_stride_one(acc, i);
                if (is_one)
                    break;
            }
        }

        isl_map_free(acc);

        if (!(is_zero || is_one))
        {
            isl_map_free(prefix);
            return isl_bool_false;
        }
        else
        {
            /* Log if layout transformation is required and the dim to be permuted. */
            if (i == access->n_index - 1)
            {
                access->layout_trans = 0;
                access->simd_dim = i;
            }
            else
            {
                access->layout_trans = 1;
                access->simd_dim = i;
            }
            /* Update the score. */
            data->score = data->score + (1 - access->layout_trans);
            data->num_accs = data->num_accs + 1;
            data->num_layout_trans = data->num_layout_trans + access->layout_trans;
        }
    }

    isl_map_free(prefix);
    return isl_bool_true;
}

/* This function examines if the access function of the statements under 
 * the current "node" has only stride-0/1 access.
 */
static isl_bool is_stride_coalesced_at_node(__isl_keep isl_schedule_node *node,
                                            void *user)
{
    struct stride_coalesced_data *data = (struct stride_coalesced_data *)user;
    struct autosa_kernel *kernel = data->kernel;
    isl_union_set *domain;
    isl_union_map *prefix;
    isl_bool one_or_zero;

    if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
        return isl_bool_true;

    domain = isl_schedule_node_get_domain(node);
    prefix = isl_schedule_node_get_prefix_schedule_union_map(node);
    data->prefix = prefix;

    /* Examine each statment under the loop */
    one_or_zero = isl_union_set_every_set(domain, &is_stride_coalesced_stmt, data);

    isl_union_map_free(data->prefix);
    isl_union_set_free(domain);

    return one_or_zero;
}

/* This function examines if all the array references under the current "node"
 * are stride-0/stride-1.
 * We also give a score to the loop calculated by:
 * score = Sum_{all_array_references_under_the_loop}{
 *           (is_access_stride-0/1 * (1 - is_layout_transformation_required)              
 *              + num_of_accs / num_of_required_layout_transform}
 * When examining each array reference, we will try all different layout by 
 * permuting each array dimension innermost to make sure we don't miss any
 * opportunity. 
 * When layout transformation is required, we will log the dimension to be 
 * permuted innermost.
 * The calculated score is returned.
 */
static float is_stride_coalesced(__isl_keep isl_schedule_node *node,
                                 struct autosa_kernel *kernel, int *layout_transform)
{
    float score = 0;
    struct stride_coalesced_data data;
    isl_bool coalesced;

    data.kernel = kernel;
    data.score = score;
    data.num_accs = 0;
    data.num_layout_trans = 0;
    coalesced = isl_schedule_node_every_descendant(node,
                                                   &is_stride_coalesced_at_node, &data);

    /* We penalize the loop with more layout transformation required. */
    if (data.num_layout_trans == 0)
    {
        data.score += (data.num_accs + 1);
    }
    else
    {
        data.score += (data.num_accs / data.num_layout_trans);
    }

    /* Examine and make sure all the array references of the same array 
     * have the same dimenison for layout transformation.
     */
    if (coalesced)
    {
        struct autosa_kernel *kernel = data.kernel;
        for (int i = 0; i < kernel->n_array; i++)
        {
            struct autosa_local_array_info *local_array;
            int simd_dim = -1;
            local_array = &kernel->array[i];
            for (int j = 0; j < local_array->array->n_ref; j++)
            {
                struct autosa_stmt_access *acc = local_array->array->refs[j];
                if (acc->layout_trans == 1)
                {
                    if (simd_dim == -1)
                        simd_dim = acc->simd_dim;
                    else
                    {
                        if (simd_dim != acc->simd_dim)
                        {
                            coalesced = isl_bool_false;
                            return coalesced ? data.score : -1;
                        }
                    }
                }
            }
        }
    }

    /* Print out the layout transform information. */
    if (coalesced)
    {
        struct autosa_kernel *kernel = data.kernel;
        isl_printer *p;

        p = isl_printer_to_file(kernel->ctx, stdout);
        for (int i = 0; i < kernel->n_array; i++)
        {
            struct autosa_local_array_info *local_array;
            local_array = &kernel->array[i];
            for (int j = 0; j < local_array->array->n_ref; j++)
            {
                struct autosa_stmt_access *acc = local_array->array->refs[j];

                if (acc->layout_trans != -1)
                {
                    if (acc->layout_trans == 1)
                    {
                        printf("[AutoSA] Array reference ");
                        if (acc->read)
                            printf("(R): ");
                        else
                            printf("(W): ");
                        p = isl_printer_print_map(p, acc->access);
                        printf("\n");
                        printf("[AutoSA] Layout transform: Permute dim (%d) to the innermost\n", acc->simd_dim);
                        *layout_transform = 1;
                    }
                    acc->layout_trans = -1;
                    acc->simd_dim = -1;
                }
            }
        }
        isl_printer_free(p);
    }

    return coalesced ? data.score : -1;
}

/* A loop is identified to be vectorizable if it is:
 * - a parallel or reduction loop
 * - with stride-0/1 access.
 * Only time loops are considered.
 * For each candidate loop, we compute the score:
 * score = 2 * is_loop_parallel + 4 * is_loop_reduction)
 *           + Sum_{all_array_references_under_the_loop}{
 *              (is_access_stride-0/1 * (1 - is_layout_transformation_required)
 *              + num_of_accs / num_of_required_layout_transform}
 * The heuristics are:
 * - We prefer reduction loop to parallel loop. 
 * - We prefer array references without requirements of layout transformation.
 */
static isl_schedule_node *detect_simd_vectorization_loop(
    __isl_take isl_schedule_node *node, void *user)
{
    struct simd_vectorization_data *data = (struct simd_vectorization_data *)user;
    struct autosa_kernel *sa = data->kernel;
    isl_ctx *ctx = isl_schedule_node_get_ctx(node);
    float score;
    isl_schedule_node *cur_node;
    int is_latency;
    int n_member;
    int simd_touch_space;

    /* If the currrent node is under the latency mark, return
     * as we don't use latency hiding loop as candidates. 
     */
    is_latency = is_node_under_latency(node);
    if (is_latency)
        return node;

    simd_touch_space = sa->options->autosa->simd_touch_space;    

//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, ctx);
//#endif

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        n_member = isl_schedule_node_band_n_member(node);
        for (int i = 0; i < n_member; i++)
        {
            if (!simd_touch_space && isl_schedule_node_band_member_get_space_time(node, i) != autosa_loop_time) {
                /* We consider only time loops */
                continue;
            } else {
                /* We consider both space and time loops */            
                /* Two types of loops that we are interested in:
                 * - Parallel loop.
                 * - Reduction loop in the innermost loop band.
                 *   This limit is currently relaxed, we will look at all loop bands 
                 *   for reduction loops as the current isl dep analysis can't 
                 *   differentiate reduction dependences and might seperate one 
                 *   permutable loop band into two loop bands.
                 */
                int is_parallel = 0;
                int is_reduction = 0;
                int layout_transform = 0;
                float score_i;

                if (!isl_schedule_node_band_member_get_coincident(node, i) && !strcmp(data->mode, "manual"))
                {
                    /* At present, we can't analyze reduction loop by AutoSA.
                     * We will print each node and follow the user guidance.
                     * Besides, reduction loops are only examined in the manual mode.
                     * In the auto mode, only parallel loops are examined.
                     */
                    size_t bufsize = 100;
                    size_t characters;
                    printf("[AutoSA] Detecting the reduction loop.\n");
                    printf("[AutoSA] Band member position: %d\n", i);
                    /* If the SIMD info is pre-loaded, we don't ask for user inputs. */
                    if (data->buffer == NULL)
                    {
                        isl_printer *p;
                        p = isl_printer_to_file(ctx, stdout);
                        p = isl_printer_end_line(p);
                        p = isl_printer_set_yaml_style(p, ISL_YAML_STYLE_BLOCK);
                        p = isl_printer_print_schedule_node(p, node);
                        isl_printer_free(p);
                        printf("[AutoSA] Please input if the current loop is a reduction loop [y/n]: ");
                    }
                    if (data->buffer == NULL)
                    {
                        char *buffer = (char *)malloc(bufsize * sizeof(char));
                        data->buffer = buffer;
                        data->buffer_offset = 0;
                        characters = getline(&buffer, &bufsize, stdin);
                    }
                    printf("[AutoSA] Reduction property: %c\n", data->buffer[data->buffer_offset]);
                    is_reduction = (data->buffer[data->buffer_offset] == 'y') ? 1 : 0;
                    if (data->buffer[data->buffer_offset + 1] == 'y' ||
                        data->buffer[data->buffer_offset + 1] == 'n')
                    {
                        data->buffer_offset += 1;
                    }
                    else
                    {
                        free(data->buffer);
                        data->buffer = NULL;
                        data->buffer_offset = 0;
                    }
                }
                else
                {
                    is_parallel = isl_schedule_node_band_member_get_coincident(node, i);
                }

                /* Test if all the array references under the current loop 
                 * has only stride-0/1 access. 
                 */
                if (is_parallel || is_reduction)
                {
                    cur_node = node;
                    node = isl_schedule_node_dup(cur_node);

                    if (i > 0)
                    {
                        node = isl_schedule_node_band_split(node, i);
                        node = isl_schedule_node_child(node, 0);
                    }
                    if (n_member - i - 1 > 0)
                    {
                        node = isl_schedule_node_band_split(node, 1);
                    }

                    /* Sink the band innermost. */
                    node = isl_schedule_node_band_sink(node);
                    score = 2 * is_parallel + 4 * is_reduction;
                    printf("[AutoSA] -----------------------------------------------\n");
                    printf("[AutoSA] Current band member position: %d\n", i);
                    printf("[AutoSA] -----------------------------------------------\n");
                    score_i = is_stride_coalesced(node, sa, &layout_transform);
                    isl_schedule_node_free(node);
                    node = cur_node;
                    if (score_i < 0)
                    {
                        /* The array references are not coalesced. */
                        score = -1;
                        continue;
                    }
                    else
                    {
                        score += score_i;
                        printf("[AutoSA] -----------------------------------------------\n");
                        printf("[AutoSA] The loop is legal to be vectorized with score: %f\n",
                               score);
                        if (layout_transform)
                            printf("[AutoSA] Layout transformation is required to proceed.\n");
                        printf("[AutoSA] -----------------------------------------------\n");
                        node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_simd);
                        if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_space)
                            data->has_space_candidate = 1;

                        if (score >= data->best_score)
                        {
                            data->best_score = score;
                            data->layout_trans = layout_transform;
                        }
                        data->n_loops = data->n_loops + 1;
                        data->scores = (float *)realloc(data->scores, sizeof(float) * data->n_loops);
                        data->scores[data->n_loops - 1] = score;
                        data->legal = (int *)realloc(data->legal, sizeof(int) * data->n_loops);
                        data->legal[data->n_loops - 1] = !layout_transform;

                        /* Extract the loop upper bounds */
                        int *ubs = extract_band_upper_bounds(node);
                        data->ubs = (int *)realloc(data->ubs, sizeof(int) * data->n_loops);
                        data->ubs[data->n_loops - 1] = ubs[i];
                        free(ubs);
                    }
                }
            }
        }
    }

    return node;
}

/* Examine if the node is the last band node, 
 * If so, add a "simd" mark before the node. */
static __isl_give isl_schedule_node *add_simd_mark(
    __isl_take isl_schedule_node *node, void *user)
{
    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        node = isl_schedule_node_child(node, 0);
        isl_bool no_inner_band = isl_schedule_node_every_descendant(node,
                                                                    &no_permutable_node, NULL);
        node = isl_schedule_node_parent(node);
        if (no_inner_band)
        {
            /* Insert the "simd" mark. */
            isl_id *id = isl_id_alloc(isl_schedule_node_get_ctx(node), "simd", NULL);
            node = isl_schedule_node_insert_mark(node, id);
        }
    }

    return node;
}

/* Update the stride information for the array accesses under the SIMD loop.
 */
static isl_bool update_simd_acc_stmt(__isl_keep isl_set *set, void *user)
{
    struct stride_coalesced_data *data = (struct stride_coalesced_data *)user;
    struct autosa_stmt *stmt;
    isl_space *space;
    isl_id *id;
    struct autosa_stmt_access *accesses, *access;
    isl_map *prefix;

    space = isl_set_get_space(set);
    id = isl_space_get_tuple_id(space, isl_dim_set);
    isl_space_free(space);
    stmt = find_stmt(data->kernel->prog, id);
    isl_id_free(id);
    accesses = stmt->accesses;
    prefix = isl_map_from_union_map(isl_union_map_intersect_domain(
        isl_union_map_copy(data->prefix), isl_union_set_from_set(isl_set_copy(set))));

    for (access = accesses; access; access = access->next)
    {
        isl_map *acc;
        int n;
        isl_bool is_zero = isl_bool_false, is_one = isl_bool_false;
        isl_pw_multi_aff *pma;
        int i;

        if (access->n_index == 0)
            continue;

        acc = isl_map_copy(access->access);
        acc = isl_map_apply_domain(acc, isl_map_copy(prefix));

        for (i = access->n_index - 1; i >= 0; i--)
        {
            is_zero = access_is_stride_zero(acc, i);
            if (is_zero)
                break;
        }
        if (!is_zero)
        {
            is_one = isl_bool_true;
        }

        isl_map_free(acc);
        access->simd_stride = is_zero ? 0 : (is_one ? 1 : -1);
    }

    isl_map_free(prefix);
    return isl_bool_true;
}

/* Update the stride information for the array accesses under the SIMD loop.
 */
static isl_bool update_simd_acc(__isl_keep isl_schedule_node *node, void *user)
{
    isl_union_set *domain;
    isl_union_map *prefix;
    struct stride_coalesced_data *data = (struct stride_coalesced_data *)user;

    if (isl_schedule_node_get_type(node) != isl_schedule_node_leaf)
        return isl_bool_true;

    domain = isl_schedule_node_get_domain(node);
    prefix = isl_schedule_node_get_prefix_schedule_union_map(node);
    data->prefix = prefix;

    isl_union_set_every_set(domain, &update_simd_acc_stmt, data);

    isl_union_set_free(domain);
    isl_union_map_free(prefix);

    return isl_bool_true;
}

/* This function tiles the SIMD loop.
 * If it is executed in the auto mode, it will select the loop with the 
 * highest score.
 * Otherwise, it will select loops with positive tiling factors.
 * Loops with tiling factors of one or require layout transformation are skipped.
 * At last, it will also update the stride information for the array accesses
 * under the SIMD loop.
 */
static __isl_give isl_schedule_node *autosa_simd_tile_loop(
    __isl_take isl_schedule_node *node, void *user)
{
    struct simd_vectorization_data *data = (struct simd_vectorization_data *)user;
    struct autosa_kernel *kernel = data->kernel;
    struct stride_coalesced_data stride_data;
    stride_data.kernel = data->kernel;

    if (isl_schedule_node_get_type(node) == isl_schedule_node_band)
    {
        for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
        {
            if (isl_schedule_node_band_member_get_pe_opt(node, i) == autosa_loop_simd)
            {
                if (!strcmp(data->mode, "auto"))
                {
                    /* Perform tiling on the loop with the highest score. */
                    if (data->scores[data->loop_cnt] != data->best_score)
                    {
                        node = isl_schedule_node_band_member_set_pe_opt(node, i,
                                                                        autosa_loop_default);
                        data->loop_cnt++;
                        continue;
                    }
                }
                else
                {
                    /* Peform tiling on the loop with positive tiling factor */
                    if (data->tile_size[data->loop_cnt] <= 0)
                    {
                        node = isl_schedule_node_band_member_set_pe_opt(node, i,
                                                                        autosa_loop_default);
                        data->loop_cnt++;
                        continue;
                    }
                }
                if (data->tile_size[data->loop_cnt] == 1)
                {
                    /* Skip if the tiling factor is one. */
                    node = isl_schedule_node_band_member_set_pe_opt(node, i,
                                                                    autosa_loop_default);
                    data->loop_cnt++;
                    continue;
                }
                if (data->legal[data->loop_cnt] == 0)
                {
                    /* Layout transformation is needed to proceed.
                     * We will skip this loop. 
                     */
                    node = isl_schedule_node_band_member_set_pe_opt(node, i,
                                                                    autosa_loop_default);
                    data->loop_cnt++;
                    continue;
                }
                
                int tile_size = data->tile_size[data->loop_cnt];
                
                /* If SIMD vectorization is applied on the space loops, we need to update
                 * the SA dimensions.
                 */
                if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_space) {
                    /* Figure out the dim position */
                    int touched_space_loop = 0;
                    for (int j = 0; j < i; j++) {
                        if (isl_schedule_node_band_member_get_space_time(node, j) == autosa_loop_space)
                            touched_space_loop++;
                    }                                        
                    data->kernel->sa_dim[touched_space_loop] /= tile_size;
                    if (data->kernel->sa_dim[touched_space_loop] == 1) {
                        throw std::runtime_error("[AutoSA] Error: Array dimension as 1 is not supported!");
                    }
                }                
                /* Tile the loop */
                node = autosa_node_band_tile_loop(node, tile_size, i);
                /* Reset the candidate loop in the tile loop the pe_opt property to default */
                node = isl_schedule_node_band_member_set_pe_opt(node, i, autosa_loop_default);
                /* Reset the point loop space_time property to time loop. */
                node = isl_schedule_node_child(node, 0);
                node = isl_schedule_node_band_member_set_space_time(node, 0, autosa_loop_time);
                /* Reset the point loop pe_opt property to default. */
                node = isl_schedule_node_band_member_set_pe_opt(node, 0, autosa_loop_default);                
                /* Sink the point loop innermost */
#ifdef ISL_SINK                
                node = isl_schedule_node_band_sink(node);
                /* Add the simd marker */
                node = isl_schedule_node_map_descendant_bottom_up(node, &add_simd_mark, NULL);
#else                
                /* Sink the point loop innermost and add the simd marker */
                node = autosa_node_sink_to_mark(node, "simd");
#endif
                /* Update the stride information for array references under the SIMD loop. */
                isl_schedule_node_every_descendant(node, &update_simd_acc, &stride_data);                

                node = isl_schedule_node_parent(node);
                kernel->simd_w = tile_size;
                data->loop_cnt++;
                printf("[AutoSA] SIMD vectorization successfully applied.\n");
            }
        }
    }

    return node;
}

/* Load the SIMD information for the kernel. 
 */
static __isl_give char *load_simd_info(struct autosa_kernel *sa)
{
    cJSON *simd_info;
    FILE *f;
    char *buffer = NULL;
    long length;

    if (sa->options->autosa->simd_info)
    {
        f = fopen(sa->options->autosa->simd_info, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            length = ftell(f);
            fseek(f, 0, SEEK_SET);
            buffer = (char *)malloc(length + 1);
            if (buffer)
            {
                buffer[length] = '\0';
                int r = fread(buffer, 1, length, f);
            }
            fclose(f);
        }
        else
        {
            printf("[AutoSA] Error: Can't open SIMD information file: %s\n",
                   sa->options->autosa->simd_info);
            exit(1);
        }
    }

    if (buffer)
    {
        simd_info = cJSON_Parse(buffer);
        free(buffer);
        /* Load the SIMD info into a string. */
        cJSON *reduction = NULL;
        cJSON *reductions = NULL;
        int info_id = 0;
        char kernel_name[20];
        sprintf(kernel_name, "kernel%d", sa->space_time_id);
        //#ifdef _DEBUG
        //    DBGVAR(std::cout, sa->space_time_id);
        //#endif
        reductions = cJSON_GetObjectItemCaseSensitive(simd_info, kernel_name);
        if (reductions)
        {
            char *info = (char *)malloc(100 * sizeof(char));
            reductions = cJSON_GetObjectItemCaseSensitive(reductions, "reduction");
            cJSON_ArrayForEach(reduction, reductions)
            {
                char *info_i = reduction->valuestring;
                sprintf(info + info_id, "%c", info_i[0]);
                info_id++;
            }
            cJSON_Delete(simd_info);
            return info;
        }
        else
        {
            cJSON_Delete(simd_info);
            return NULL;
        }
    }
    return NULL;
}

/* Apply SIMD vectorization. 
 * We go through all the loops, if there is any vectorizable loop 
 * (parallel or reduction loop with stride-0/1 access), such a loop will 
 * be identified as SIMD loop candidates. We will rank the loops by heuristics 
 * and pick up one loop with the highest score to be tiled. 
 * The point loop will be permuated as the innermost loops.
 * At last this loop with be unrolled by HLS tools.
 */
isl_stat sa_simd_vectorization_optimize(struct autosa_kernel *sa, char *mode)
{
    float *scores = NULL;
    int n_loops = 0;
    struct simd_vectorization_data data;
    data.best_score = 0;
    data.mode = mode;
    data.ubs = NULL;
    int *tile_size = NULL;

    printf("[AutoSA] Apply SIMD vectorization.\n");
    isl_schedule *schedule = sa->schedule;
    isl_schedule_node *node = isl_schedule_get_root(schedule);
    sa->simd_w = 1;

    /* Move down to the array marker */
    node = autosa_tree_move_down_to_array(node, sa->core);

    /* Detect all candidate loops */
    data.kernel = sa;
    data.scores = scores;
    data.legal = NULL;
    data.buffer = NULL;
    data.buffer_offset = 0;
    data.n_loops = n_loops;
    data.has_space_candidate = 0;
    /* Load the SIMD information. */
    data.buffer = load_simd_info(sa);
    node = isl_schedule_node_map_descendant_bottom_up(
        node, &detect_simd_vectorization_loop, &data);

    if (data.n_loops == 0)
    {
        printf("[AutoSA] No candidate loops found!\n");
        isl_schedule_node_free(node);
        return isl_stat_ok;
    }

    /* Display the candidate loops. */
    isl_schedule_free(schedule);
    schedule = isl_schedule_node_get_schedule(node);
    if (sa->scop->options->autosa->verbose)
    {
        isl_printer *p = isl_printer_to_file(sa->ctx, stdout);
        p = isl_printer_set_yaml_style(p, ISL_YAML_STYLE_BLOCK);
        p = isl_printer_print_schedule(p, schedule);
        printf("\n");
        isl_printer_free(p);
    }
    isl_schedule_free(schedule);

    if (data.layout_trans)
    {
        printf("[AutoSA] Warning: Layout transformation is required to proceed. SIMD vectorization is skipped.\n");
    }
    else
    {
        /* Select the candidate loop with the highest score.
         * Tile the candidate loop and permute the point loop innermost. 
         * A SIMD vectorization marker is added. 
         */
        if (!strcmp(mode, "manual"))
        {
            tile_size = read_simd_tile_sizes(sa, data.n_loops);
            if (!tile_size)
            {
                /* Dump out the number, score and upper bounds of simd loops 
                 * and exit the program. 
                 */
                int *ubs = data.ubs;
                FILE *fp;
                char *content;
                cJSON *tuning, *simd_json, *loops_json, *scores_json, *legal_json;
                isl_printer *p_str;
                char *tuning_path;

                tuning = cJSON_CreateObject();
                simd_json = cJSON_CreateObject();
                cJSON_AddItemToObject(tuning, "simd", simd_json);
                loops_json = cJSON_CreateArray();
                cJSON_AddItemToObject(simd_json, "tilable_loops", loops_json);
                for (int i = 0; i < data.n_loops; i++)
                {
                    cJSON *loop = cJSON_CreateNumber(ubs[i]);
                    cJSON_AddItemToArray(loops_json, loop);
                }
                scores_json = cJSON_CreateArray();
                cJSON_AddItemToObject(simd_json, "scores", scores_json);
                for (int i = 0; i < data.n_loops; i++)
                {
                    cJSON *loop = cJSON_CreateNumber(data.scores[i]);
                    cJSON_AddItemToArray(scores_json, loop);
                }
                legal_json = cJSON_CreateArray();
                cJSON_AddItemToObject(simd_json, "legal", legal_json);
                for (int i = 0; i < data.n_loops; i++)
                {
                    cJSON *loop = cJSON_CreateNumber(data.legal[i]);
                    cJSON_AddItemToArray(legal_json, loop);
                }
                if (data.has_space_candidate == 0) {
                    loops_json = cJSON_CreateArray();
                    cJSON_AddItemToObject(simd_json, "sa_dims", loops_json);
                    for (int i = 0; i < sa->n_sa_dim; i++)
                    {
                        cJSON *loop = cJSON_CreateNumber(sa->sa_dim[i]);
                        cJSON_AddItemToArray(loops_json, loop);
                    }
                }
                p_str = isl_printer_to_str(sa->ctx);
                p_str = isl_printer_print_str(p_str, sa->options->autosa->output_dir);
                p_str = isl_printer_print_str(p_str, "/tuning.json");
                tuning_path = isl_printer_get_str(p_str);
                fp = fopen(tuning_path, "w");
                content = cJSON_Print(tuning);
                fprintf(fp, "%s", content);
                cJSON_Delete(tuning);
                free(tuning_path);
                isl_printer_free(p_str);
                exit(0);
            }
        }
        else
        {
            throw std::runtime_error("[AutoSA] Error: Auto SIMD vectorization is not supported.\n");
        }

        /* Perform the simd vectorization. */
        data.loop_cnt = 0;
        data.tile_size = tile_size;
        node = isl_schedule_node_map_descendant_bottom_up(node,
                                                          &autosa_simd_tile_loop, &data);
    }

    free(data.ubs);
    free(data.legal);
    free(tile_size);
    /* Clean up the band pe_opt properties. */
    schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);
    schedule = isl_schedule_map_schedule_node_bottom_up(
        schedule, &clear_pe_opt_prop, NULL);
    free(data.scores);
    sa->schedule = schedule;

    /* Update the tuning config, dump out the sa dimensions. */
    if (data.has_space_candidate)
    {
        cJSON *tuning, *loops_json;
        isl_printer *p_str;
        char *tuning_path;
        char *content;
        FILE *fp;

        tuning = cJSON_CreateObject();
        loops_json = cJSON_CreateArray();
        cJSON_AddItemToObject(tuning, "sa_dims", loops_json);
        for (int i = 0; i < sa->n_sa_dim; i++) {
            cJSON *loop = cJSON_CreateNumber(sa->sa_dim[i]);
            cJSON_AddItemToArray(loops_json, loop);
        }
        p_str = isl_printer_to_str(sa->ctx);
        p_str = isl_printer_print_str(p_str, sa->options->autosa->output_dir);
        p_str = isl_printer_print_str(p_str, "/tuning.json");
        tuning_path = isl_printer_get_str(p_str);
        fp = fopen(tuning_path, "w");
        content = cJSON_Print(tuning);
        fprintf(fp, "%s", content);
        cJSON_Delete(tuning);
        free(tuning_path);
        isl_printer_free(p_str);
    }

    return isl_stat_ok;
}

/* Apply PE optimization including:
 * - latency hiding
 * - SIMD vectorization
 * - array partitioning
 */
isl_stat sa_pe_optimize(struct autosa_kernel *sa, bool pass_en[], char *pass_mode[])
{
    printf("[AutoSA] Appy PE optimization.\n");
//#ifdef _DEBUG
//    DBGSCHD(stdout, sa->schedule, isl_schedule_get_ctx(sa->schedule))
//#endif

    /* Prepartion before the optimization. */
    /* Initialize the autosa_loop_types. */
    sa_loop_init(sa);
    /* Set up the space_time properties. */
    sa_space_time_loop_setup(sa);    
    /* Extract the communication pairs. */
    sa_io_update(sa);    

    /* Extract the tile sizes. */
    sa->sizes = extract_sizes_from_str(sa->ctx, sa->scop->options->autosa->sa_sizes);
    /* Set the core */
    isl_union_set *domain = isl_schedule_get_domain(sa->schedule);
    sa->core = isl_union_set_universe(domain);

//#ifdef _DEBUG
//    DBGSCHD(stdout, sa->schedule, isl_schedule_get_ctx(sa->schedule));
//    
//#endif
    /* Array partitioning. */
    sa_array_partitioning_optimize(sa, pass_en[0], pass_mode[0], pass_en[1], pass_mode[1]);    

//#ifdef _DEBUG
//    DBGSCHD(stdout, sa->schedule, isl_schedule_get_ctx(sa->schedule));    
//#endif

    /* Latency hiding. */
    sa_latency_hiding_optimize(sa, pass_en[2], pass_mode[2]);    

//#ifdef _DEBUG
//    DBGSCHD(stdout, sa->schedule, isl_schedule_get_ctx(sa->schedule));    
//#endif

    /* SIMD vectorization. */
    if (pass_en[3])
        sa_simd_vectorization_optimize(sa, pass_mode[3]);    

    return isl_stat_ok;
}

/* Extract the set of parameter values and outer schedule dimensions
 * for which any statement instance
 * in the kernel inserted at "node" needs to be executed.
 * Intersect the set of parameter values derived from the host schedule
 * relation with the context of "prog".
 */
static __isl_give isl_set *extract_context(__isl_keep isl_schedule_node *node,
                                           struct autosa_prog *prog)
{
    isl_union_map *schedule;
    isl_union_set *schedule_domain;
    isl_set *context;
    int empty;

    schedule = isl_schedule_node_get_prefix_schedule_relation(node);
    schedule_domain = isl_union_map_range(schedule);
    empty = isl_union_set_is_empty(schedule_domain);
    if (empty < 0)
    {
        isl_union_set_free(schedule_domain);
        return NULL;
    }
    if (empty)
    {
        int depth;
        isl_space *space;

        space = isl_union_set_get_space(schedule_domain);
        isl_union_set_free(schedule_domain);
        space = isl_space_set_from_params(space);
        depth = isl_schedule_node_get_schedule_depth(node);
        space = isl_space_add_dims(space, isl_dim_set, depth);
        context = isl_set_empty(space);
    }
    else
    {
        context = isl_set_from_union_set(schedule_domain);
    }
    context = isl_set_intersect_params(context,
                                       isl_set_copy(prog->context));

    return context;
}

/* Return the set of outer array elements accessed by
 * by the statement instances in "domain" in "prog".
 * The instances in "domain" are those that appear
 * in the domains of the access relations in "prog".
 */
static __isl_give isl_union_set *accessed_by_domain(
    __isl_take isl_union_set *domain, struct autosa_prog *prog)
{
    isl_union_map *access;
    isl_union_set *arrays;

    access = isl_union_map_union(isl_union_map_copy(prog->read),
                                 isl_union_map_copy(prog->may_write));
    access = isl_union_map_intersect_domain(access, domain);
    arrays = isl_union_map_range(access);
    arrays = isl_union_set_apply(arrays,
                                 isl_union_map_copy(prog->to_outer));

    return arrays;
}

/* Compute the effective grid size as a list of the sizes in each dimension.
 *
 * The grid size specified by the user or set by default
 * in read_grid_sizes() and applied by the block filter,
 * may be too large for the given code in the sense that
 * it may contain blocks that don't need to execute anything.
 * We therefore don't return this grid size, but instead the
 * smallest grid size that ensures that all blocks that actually
 * execute code are included in the grid.
 *
 * We first extract a description of the grid, i.e., the possible values
 * of the block ids, from the domain elements in "domain" and
 * kernel->block_filter.
 * The block ids are parameters in kernel->block_filter.
 * We simply need to change them into set dimensions.
 *
 * Then, for each block dimension, we compute the maximal value of the block id
 * and add one.
 */
static __isl_give isl_multi_pw_aff *extract_grid_size(
    struct autosa_kernel *kernel, __isl_take isl_union_set *domain)
{
    int i;
    isl_set *grid;
    isl_set *context;
    isl_multi_pw_aff *size;

    /* For AutoSA, we set the grid size as 1 */
    grid = isl_union_set_params(domain);
    grid = isl_set_from_params(grid);
    grid = isl_set_add_dims(grid, isl_dim_set, kernel->n_grid);
    for (i = 0; i < kernel->n_grid; ++i)
    {
        int pos;
        isl_constraint *ls;

        if (!grid)
            return NULL;

        /* Set this dimension as 1. */
        ls = isl_constraint_alloc_equality(isl_local_space_from_space(isl_set_get_space(grid)));
        ls = isl_constraint_set_constant_si(ls, 0);
        ls = isl_constraint_set_coefficient_si(ls, isl_dim_set, i, 1);
        grid = isl_set_add_constraint(grid, ls);
    }

    grid = isl_set_coalesce(grid);
    size = ppcg_size_from_extent(grid);
    context = isl_set_params(isl_set_copy(kernel->context));
    return isl_multi_pw_aff_gist(size, context);
}

/* Group the domain elements into a single space, named kernelX,
 * with X the kernel sequence number "kernel_id".
 */
static __isl_give isl_schedule_node *group_statements(
    __isl_take isl_schedule_node *node, int kernel_id)
{
    char buffer[20];
    isl_id *id;

    if (!node)
        return NULL;

    snprintf(buffer, sizeof(buffer), "kernel%d", kernel_id);
    id = isl_id_alloc(isl_schedule_node_get_ctx(node), buffer, NULL);
    return isl_schedule_node_group(node, id);
}

/* Apply communication management including:
 * - data allocation
 * - I/O construction
 * - I/O optimization 
 * First, data allocation allocates the on-chip buffers inside PEs.
 * Next, I/O construction builds the I/O system to transfer the data.
 * Lastly, I/O optimization optimizes the I/O system, performing tasks including:
 * - I/O module clustering
 * - L2 I/O buffering
 * - data packing
 */
isl_stat sa_comm_management(struct autosa_kernel *sa, struct autosa_gen *gen)
{
    printf("[AutoSA] Apply communication management.\n");

    sa_io_construct_optimize(sa, gen);

    return isl_stat_ok;
}

/* Replace "pa" by the zero function defined over the universe domain
 * in the space of "pa".
 */
static __isl_give isl_pw_aff *set_universally_zero(__isl_take isl_pw_aff *pa)
{
    isl_space *space;
    isl_aff *zero;

    space = isl_space_domain(isl_pw_aff_get_space(pa));
    isl_pw_aff_free(pa);
    zero = isl_aff_zero_on_domain(isl_local_space_from_space(space));

    return isl_pw_aff_from_aff(zero);
}

/* The sizes of the arrays on the host that have been computed by
 * extract_array_info may depend on the parameters.  Use the extra
 * constraints on the parameters that are valid at "host_domain"
 * to simplify these expressions and store the results in kernel->array.
 *
 * We only need these localized bounds for arrays that are accessed
 * by the current kernel.  If we have found at least one reference group
 * then the array is accessed by the kernel.
 *
 * The resulting sizes may be functions that are nowhere defined
 * in case the access function cannot possibly access anything inside
 * the kernel for some reason.  If so, they are replaced by the zero
 * function.  Since the access function cannot actually access anything,
 * there is no harm in printing the array sizes as zero.
 */
static void localize_bounds(struct autosa_kernel *kernel,
                            __isl_keep isl_set *host_domain)
{
    int i, j;
    isl_set *context;

    context = isl_set_copy(host_domain);
    context = isl_set_params(context);

    for (i = 0; i < kernel->n_array; ++i)
    {
        struct autosa_local_array_info *local = &kernel->array[i];
        isl_multi_pw_aff *bound;
        int n_index;

        if (local->n_pe_group == 0)
            continue;

        n_index = local->array->n_index;
        bound = isl_multi_pw_aff_copy(local->array->bound);

        for (j = 0; j < n_index; ++j)
        {
            isl_pw_aff *pwaff;
            int empty;

            pwaff = isl_multi_pw_aff_get_pw_aff(bound, j);
            pwaff = isl_pw_aff_gist(pwaff, isl_set_copy(context));
            empty = isl_pw_aff_is_empty(pwaff);
            if (empty < 0)
                pwaff = isl_pw_aff_free(pwaff);
            else if (empty)
                pwaff = set_universally_zero(pwaff);
            bound = isl_multi_pw_aff_set_pw_aff(bound, j, pwaff);
        }

        local->n_index = n_index;
        local->bound = bound;
    }
    isl_set_free(context);
}

/* Create an autosa_kernel represents the domain isntances that reach "node" and 
 * insert a mark node pointing to the autosa_kernel before "node".
 *
 * Mark all outer band nodes as atomic to ensure each kernel is only scheduled once.
 * If the domain elements that reach "node" live in more than one space,
 * then group the domain elements into a single space, named kernelX, 
 * with X the kernel sequence numbers.
 *
 * [Space-time transformation]
 * We will first perform space-time transformation to transform the design to 
 * systolic array.
 * [PE optimization]
 * PE optimization is applied next including: array parititioning, latency hiding, 
 * and SIMD vectorization.
 * For array partitioning, the mark "array" is added between the tile and point loops.
 * All the loops below the "array" mark will be mapped to FPGA device at once.
 * For latency hiding, SIMD vectorization, all the generated loops will be marked
 * "latency" and "SIMD".
 * [Communication management]
 * Then we perform comm opt. through: data allocation, I/O construction, and 
 * I/O optimization.
 * 
 * [Ignore below...]
 * The linear branch between the kernel node and "array" mark may also have a 
 * "local" mark. If present, the mapping to local memory is computed at this point. 
 * The "local" mark will be removed at the end of this function.
 *
 * Compute array reference groups for all arrays, set the local array bounds 
 * based on the set of domain instances that reach the kernel node, 
 * check the total amount of shared memory used and compute 
 * all group tilings.
 *
 * We save a copy of the schedule that may influence the mappings to shared or private
 * memory in kernel->copy_schedule.
 *
 * We add copy statements to the schedule tree and create representations for 
 * the local variables in the kernel.
 *
 * We keep a copy of the isl_id that points to the kernel to ensure 
 * that the kernel does not get destroyed if the schedule node 
 * is freed due to some error condition.
 */
static __isl_give isl_schedule_node *compute_and_comm_optimize(
    struct autosa_gen *gen, __isl_take isl_schedule_node *node)
{
    isl_size num_sa = 0;
    struct autosa_kernel **sa_candidates;
    struct autosa_kernel *sa_opt, *kernel;
    isl_schedule *schedule;
    /* Enable for array partitioning, L2 array partitioning, latency hiding, SIMD. */
    bool pe_opt_en[4];
    char *pe_opt_mode[4];
    isl_union_set *domain, *expanded;
    int single_statement;
    isl_union_map *host_schedule;
    isl_set *host_domain;
    isl_id *id;
    isl_union_pw_multi_aff *contraction;
    int n_space_dim;
    char *space_time_mode;
    cJSON *space_time_json, *space_time_mode_json, *n_sa_json, *tuning;
    cJSON *array_part_json, *array_part_en_json, *array_part_mode_json;
    cJSON *array_part_L2_json, *array_part_L2_en_json, *array_part_L2_mode_json;
    cJSON *latency_json, *latency_en_json, *latency_mode_json;
    cJSON *simd_json, *simd_en_json, *simd_mode_json;

    /* Set up the sched_pos property */
    node = sched_pos_setup(node);

//#ifdef _DEBUG
//    DBGSCHDNODE(stdout, node, isl_schedule_node_get_ctx(node))
//#endif

    /* Generate systolic arrays using space-time mapping. */
    schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);
    sa_candidates = sa_space_time_transform(schedule, gen->prog->scop, &num_sa);
    if (num_sa > 0)
        printf("[AutoSA] %d systolic arrays generated.\n", num_sa);
    space_time_json = cJSON_GetObjectItemCaseSensitive(gen->tuning_config, "space_time");
    space_time_mode_json = cJSON_GetObjectItemCaseSensitive(space_time_json, "mode");
    space_time_mode = space_time_mode_json->valuestring;    

    if (!strcmp(space_time_mode, "auto"))
    {
        /* Space-time transformation is set in AUTO mode. We will pick up
         * one systolic array to proceed based on heuristics. 
         */
        kernel = sa_candidates_smart_pick(sa_candidates, num_sa);
    }
    else
    {
        /* Space-time transformation is set in MANUAL mode. We will take the user
         * specification to select one systolic array to proceed.
         */
        isl_union_map *sizes = extract_sizes_from_str(gen->ctx,
                                                      gen->options->autosa->sa_sizes);
        int kernel_id = read_space_time_kernel_id(sizes);
        isl_union_map_free(sizes);
        if (kernel_id < 0)
        {
            /* User hasn't specified which systolic array to choose yet.
             * We will dump out the number of systolic array designs and 
             * exit the program. */
            FILE *fp;
            char *content;
            isl_printer *p_str;
            char *tuning_path;

            tuning = cJSON_CreateObject();
            space_time_json = cJSON_CreateObject();
            n_sa_json = cJSON_CreateNumber(num_sa);
            cJSON_AddItemToObject(space_time_json, "n_kernel", n_sa_json);
            cJSON_AddItemToObject(tuning, "space_time", space_time_json);
            p_str = isl_printer_to_str(gen->ctx);
            p_str = isl_printer_print_str(p_str, gen->options->autosa->output_dir);
            p_str = isl_printer_print_str(p_str, "/tuning.json");
            tuning_path = isl_printer_get_str(p_str);
            fp = fopen(tuning_path, "w");
            free(tuning_path);
            isl_printer_free(p_str);
            content = cJSON_Print(tuning);
            fprintf(fp, "%s", content);
            cJSON_Delete(tuning);
            exit(0);
        }
        else
        {
            kernel = sa_candidates_manual_pick(sa_candidates, num_sa, kernel_id);
        }
    }

//#ifdef _DEBUG
//    DBGSCHD(stdout, kernel->schedule, isl_schedule_get_ctx(kernel->schedule))    
//#endif

    kernel->prog = gen->prog;
    kernel->options = gen->options;

    /* Create local arrays. */
    kernel = autosa_kernel_create_local_arrays(kernel, gen->prog);
    assert(kernel != NULL);

    /* Apply PE optimization. */
    array_part_json = cJSON_GetObjectItemCaseSensitive(gen->tuning_config, "array_part");
    array_part_en_json = cJSON_GetObjectItemCaseSensitive(array_part_json, "enable");
    array_part_mode_json = cJSON_GetObjectItemCaseSensitive(array_part_json, "mode");

    array_part_L2_json = cJSON_GetObjectItemCaseSensitive(gen->tuning_config, "array_part_L2");
    array_part_L2_en_json = cJSON_GetObjectItemCaseSensitive(array_part_L2_json, "enable");
    array_part_L2_mode_json = cJSON_GetObjectItemCaseSensitive(array_part_L2_json, "mode");

    latency_json = cJSON_GetObjectItemCaseSensitive(gen->tuning_config, "latency");
    latency_en_json = cJSON_GetObjectItemCaseSensitive(latency_json, "enable");
    latency_mode_json = cJSON_GetObjectItemCaseSensitive(latency_json, "mode");

    simd_json = cJSON_GetObjectItemCaseSensitive(gen->tuning_config, "simd");
    simd_en_json = cJSON_GetObjectItemCaseSensitive(simd_json, "enable");
    simd_mode_json = cJSON_GetObjectItemCaseSensitive(simd_json, "mode");

    pe_opt_en[0] = array_part_en_json->valueint;
    pe_opt_en[1] = array_part_L2_en_json->valueint;
    pe_opt_en[2] = latency_en_json->valueint;
    pe_opt_en[3] = simd_en_json->valueint;

    pe_opt_mode[0] = array_part_mode_json->valuestring;
    pe_opt_mode[1] = array_part_L2_mode_json->valuestring;
    pe_opt_mode[2] = latency_mode_json->valuestring;
    pe_opt_mode[3] = simd_mode_json->valuestring;

    sa_pe_optimize(kernel, pe_opt_en, pe_opt_mode);
    /* Create the autosa_kernel object and attach to the schedule. */
    if (!kernel)
    {
        return NULL;
    }

    node = isl_schedule_get_root(kernel->schedule);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_child(node, 0);

    /* Insert "local" mark before the "array" mark. */
    node = autosa_tree_insert_local_before_array(node);
    if (!node)
        return NULL;

    domain = isl_schedule_node_get_domain(node);
    single_statement = isl_union_set_n_set(domain) == 1;

    /* Prepare some metadata. */
    kernel->single_statement = single_statement;
    kernel->context = extract_context(node, gen->prog);
    contraction = isl_schedule_node_get_subtree_contraction(node);
    kernel->contraction = isl_union_pw_multi_aff_copy(contraction);
    expanded = isl_union_set_copy(domain);
    expanded = isl_union_set_preimage_union_pw_multi_aff(expanded, contraction);
    kernel->expanded_domain = isl_union_set_copy(expanded);
    kernel->arrays = accessed_by_domain(expanded, gen->prog);
    //kernel->id = gen->kernel_id++;
    /* For FPGA, we set grid_size and block_size as 1, i.e. only one thread block 
     * and one thread inside the thread block. */
    kernel->n_grid = 1;
    kernel->block_dim[0] = 1;
    kernel->n_block = 1;
    kernel->grid_dim[0] = 1;
    kernel->grid_size = extract_grid_size(kernel, isl_union_set_copy(domain));
    host_schedule = isl_schedule_node_get_prefix_schedule_union_map(node);
    host_domain = isl_set_from_union_set(isl_union_map_range(host_schedule));
    kernel->host_domain = host_domain;
    kernel->domain = domain;

    /* Make all the host loops atomic so that kernel is only called once. */
    node = autosa_atomic_ancestors(node);

    /* Insert the "kernel" mark. */
    id = isl_id_alloc(gen->ctx, "kernel", kernel);
    node = isl_schedule_node_insert_mark(node, id);
    gen->kernel = kernel;

    if (!single_statement)
        node = group_statements(node, kernel->id);

    /* Insert the PE mark below the space band */
    node = autosa_tree_move_down_to_array(node, kernel->core);
    node = isl_schedule_node_child(node, 0);
    n_space_dim = 0;
    for (int i = 0; i < isl_schedule_node_band_n_member(node); i++)
    {
        if (isl_schedule_node_band_member_get_space_time(node, i) == autosa_loop_space)
        {
            n_space_dim++;
        }
    }
    if (isl_schedule_node_band_n_member(node) > n_space_dim)
        node = isl_schedule_node_band_split(node, n_space_dim);
    node = isl_schedule_node_child(node, 0);
    id = isl_id_alloc(gen->ctx, "pe", NULL);
    node = isl_schedule_node_insert_mark(node, id);
    node = autosa_tree_move_up_to_kernel(node);

    /* Save a copy of copy_schedule. */
    node = autosa_tree_move_down_to_pe(node, kernel->core);
    kernel->copy_schedule_dim = isl_schedule_node_get_schedule_depth(node);
    kernel->copy_schedule =
        isl_schedule_node_get_prefix_schedule_union_pw_multi_aff(node);
    contraction = isl_union_pw_multi_aff_copy(kernel->contraction);
    kernel->copy_schedule =
        isl_union_pw_multi_aff_pullback_union_pw_multi_aff(
            kernel->copy_schedule, contraction);
    node = autosa_tree_move_up_to_kernel(node);

    /* Delete the local node. */
    node = autosa_tree_move_down_to_local(node, kernel->core);
    node = isl_schedule_node_delete(node);

    node = autosa_tree_move_up_to_kernel(node);

    kernel->schedule = isl_schedule_free(kernel->schedule);
    kernel->schedule = isl_schedule_node_get_schedule(node);    

    /* Communication Management */
    sa_comm_management(kernel, gen);

    /* Localize the array bounds using parameters from the host domain. */
    localize_bounds(kernel, host_domain);

    return node;
}

/* Return a read ("read" is 1) or write access relation for "group"
 * with those accesses removed that are only needed to communicate data
 * within the subtree of the schedule rooted at "node".
 * Furthermore, include the prefix schedule at "node".
 * That is, return a relation of the form
 *
 *	S -> [D -> A]
 *
 * with D the outer schedule dimensions at "node".
 */
static __isl_give isl_union_map *anchored_non_local_accesses(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    __isl_take isl_schedule_node *node, int read)
{
    isl_union_map *access;
    isl_union_map *prefix;

    prefix = isl_schedule_node_get_prefix_schedule_relation(node);
    prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                              isl_union_pw_multi_aff_copy(kernel->contraction));
    access = autosa_array_ref_group_access_relation(group, read, !read);
    access = remove_local_accesses_group(kernel, group, access, prefix,
                                         read);
    /* Prefix: S -> D
   * Access: S -> A
   * range_product: S -> [D -> A]
   */
    access = isl_union_map_range_product(prefix, access);

    return access;
}

/* Given an array reference group "group", create a mapping
 *
 *	read[D -> A] -> [D -> A]
 *
 * if "read" is set or
 *
 *	write[D -> A] -> [D -> A]
 *
 * if "read" is not set.
 * D corresponds to the outer tile->depth dimensions of
 * the kernel schedule.
 */
static __isl_give isl_multi_aff *create_from_access(isl_ctx *ctx,
                                                    struct autosa_array_ref_group *group, int read)
{
    struct autosa_array_tile *tile;
    isl_space *space;
    isl_id *id;

    tile = autosa_array_ref_group_tile(group);
    space = isl_space_copy(group->array->space);
    space = isl_space_from_range(space);
    space = isl_space_add_dims(space, isl_dim_in, tile->depth);
    space = isl_space_wrap(space);
    space = isl_space_map_from_set(space);

    id = isl_id_alloc(ctx, read ? "read" : "write", group);
    space = isl_space_set_tuple_id(space, isl_dim_in, id);

    return isl_multi_aff_identity(space);
}

/* Add copy statements to the schedule tree of "node"
 * for reading from global memory to local memory (if "read" is set) or
 * for writing back from local memory to global memory
 * (if "read" is not set) for the array reference group "group" that
 * is mapped to local memory.
 * On input, "node" points to the kernel node, and it is moved
 * back there on output.
 *
 * The copies are performed in the order of the corresponding local
 * memory tile.
 * The copy statement instances include a reference to the outer
 * tile->depth dimensions of the kernel schedule for ease of
 * combining them with the group tiling.
 *
 * If we are performing a read from global memory to local memory and
 * if the array involved is not a scalar, then we copy
 * the entire tile to local memory.  This may result in some extra
 * elements getting copied, but it should lead to simpler code
 * (which means that fewer registers may be needed) and less divergence.
 *
 * Otherwise, we only copy the elements that will be read or have been written
 * in the kernel.
 *
 * That is, the extra schedule is of the form
 *
 *	type[D -> A] -> T
 *
 * where D corresponds to the outer tile->depth dimensions of
 * the kernel schedule, A to the global array and T is the corresponding
 * local memory tile.
 *
 * The copying is inserted in the schedule tree through an extension
 * of the form
 *
 *	D -> type[D -> A]
 *
 * where the extra domain elements type[D -> A] are those accessed
 * by the group.  In the case of read from a non-scalar, this set
 * is replaced by the entire local memory tile.
 *
 * If the "unroll_copy_local" option is set, then the AST generator
 * is instructed to unroll the copying code.
 *
 * The extension is inserted before the core computation in case of a read
 * and after the core computation in case of a write.
 */
static __isl_give isl_schedule_node *add_copies_group_local(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    __isl_take isl_schedule_node *node, int read)
{
    struct autosa_array_tile *tile;
    isl_union_map *access;
    isl_union_set *domain;
    isl_multi_aff *ma;
    isl_multi_aff *from_access;
    isl_multi_pw_aff *mpa;
    isl_multi_union_pw_aff *mupa;
    isl_schedule_node *graft;
    isl_union_set *filter;
    int skip;
    int kernel_depth;
    int empty;

    tile = autosa_array_ref_group_tile(group);
    kernel_depth = isl_schedule_node_get_schedule_depth(node);
    node = autosa_tree_move_down_to_depth(node, tile->depth, kernel->core);

    /* S -> [D -> A] 
   * S: domain elements
   * D: prefix schedule dimensions
   * A: access 
   */
    access = anchored_non_local_accesses(kernel, group, node, read);
    empty = isl_union_map_is_empty(access);
    if (empty < 0 || empty)
    {
        isl_union_map_free(access);
        if (empty < 0)
            return isl_schedule_node_free(node);
        return autosa_tree_move_up_to_kernel(node);
    }

    //group->array->global = 1;
    //group->local_array->global = 1;

    /* read[D -> A] -> [D -> A] */
    from_access = create_from_access(kernel->ctx, group, read);

    /* [D -> A] -> T */
    ma = isl_multi_aff_copy(tile->tiling);
    ma = isl_multi_aff_pullback_multi_aff(ma,
                                          isl_multi_aff_copy(from_access));
    mpa = isl_multi_pw_aff_from_multi_aff(ma);
    /* read[D -> A] -> T */
    mupa = isl_multi_union_pw_aff_from_multi_pw_aff(mpa);

    /* [D -> A] */
    domain = isl_union_map_range(access);

    if (read && !autosa_array_is_scalar(group->array))
    {
        isl_map *map;
        isl_set *set;
        set = isl_map_domain(isl_map_from_union_map(isl_union_set_unwrap(domain)));
        map = group_tile(group);
        map = isl_map_intersect_domain(map, set);
        domain = isl_union_set_from_set(isl_map_wrap(map));
    }

    /* read[D -> A] */
    domain = isl_union_set_preimage_multi_aff(domain, from_access);
    /* read[D -> A] -> D */
    access = isl_union_set_wrapped_domain_map(domain);
    /* D -> read[D -> A] */
    access = isl_union_map_reverse(access);
    access = isl_union_map_coalesce(access);
    graft = isl_schedule_node_from_extension(access);
    graft = isl_schedule_node_child(graft, 0);
    graft = isl_schedule_node_insert_partial_schedule(graft, mupa);
    if (kernel->options->unroll_copy_shared)
        graft = ppcg_set_schedule_node_type(graft, isl_ast_loop_unroll);

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

    node = autosa_tree_move_up_to_kernel(node);

    return node;
}

/* Check whether the array reference group "group" is mapped to
 * local memory and, if so, add copy statements to the schedule tree of "node"
 * for reading from global memory to local memory
 * (if "read" is set) or for writing back from local memory
 * to global memory (if "read" is not set) for this group.
 * On input, "node" points to the kernel node, and it is moved
 * back there on output.
 */
static __isl_give isl_schedule_node *add_copies_group(
    struct autosa_kernel *kernel, struct autosa_array_ref_group *group,
    __isl_take isl_schedule_node *node, int read)
{
    enum autosa_group_access_type type;

    type = autosa_cpu_array_ref_group_type(group);
    if (type == AUTOSA_ACCESS_LOCAL)
        return add_copies_group_local(kernel, group, node, read);

    return node;
}

static void create_kernel_var(isl_ctx *ctx,
                              struct autosa_array_ref_group *group,
                              struct autosa_kernel_var *var)
{
    int j;
    struct autosa_array_tile *tile;
    isl_printer *p;

    var->array = group->array;

    var->type = autosa_array_ref_group_type(group);
    tile = autosa_array_ref_group_tile(group);

    p = isl_printer_to_str(ctx);
    p = autosa_array_ref_group_print_name(group, p);
    var->name = isl_printer_get_str(p);
    isl_printer_free(p);

    var->size = isl_vec_alloc(ctx, group->array->n_index);

    for (j = 0; j < group->array->n_index; ++j)
        var->size = isl_vec_set_element_val(var->size, j,
                                            isl_val_copy(tile->bound[j].size));
}

static isl_stat create_kernel_vars(struct autosa_kernel *kernel)
{
    int i, j, n;

    n = 0;
    for (i = 0; i < kernel->n_array; ++i)
    {
        struct autosa_local_array_info *array = &kernel->array[i];

        for (j = 0; j < array->n_group; ++j)
        {
            struct autosa_array_ref_group *group = array->groups[j];
            enum autosa_group_access_type type;

            type = autosa_cpu_array_ref_group_type(group);
            if (type != AUTOSA_ACCESS_GLOBAL)
                ++n;
        }
    }

    kernel->var = isl_calloc_array(kernel->ctx, struct autosa_kernel_var, n);
    if (!kernel->var)
        return isl_stat_error;
    kernel->n_var = n;

    n = 0;
    for (i = 0; i < kernel->n_array; ++i)
    {
        struct autosa_local_array_info *array = &kernel->array[i];

        for (j = 0; j < array->n_group; ++j)
        {
            struct autosa_array_ref_group *group = array->groups[j];
            enum autosa_group_access_type type;

            type = autosa_cpu_array_ref_group_type(group);
            if (type == AUTOSA_ACCESS_GLOBAL)
                continue;
            create_kernel_var(kernel->ctx, group, &kernel->var[n]);
            ++n;
        }
    }

    return isl_stat_ok;
}

/* For each array reference group that is mapped to local memory,
 * add copy statements to the schedule tree of "node"
 * for reading from global memory to local memory
 * and for writing back.
 * On input, "node" points to the kernel node, and it is moved
 * back there on output.
 */
static __isl_give isl_schedule_node *add_copies(struct autosa_kernel *kernel,
                                                __isl_take isl_schedule_node *node)
{
    int i, j;

    for (i = 0; i < kernel->n_array; ++i)
    {
        struct autosa_local_array_info *array = &kernel->array[i];

        for (j = 0; j < array->n_group; ++j)
        {
            struct autosa_array_ref_group *group = array->groups[j];
            node = add_copies_group(kernel, group, node, 1);
            if (!node)
                return NULL;
            node = add_copies_group(kernel, group, node, 0);
            if (!node)
                return NULL;
        }
    }

    return node;
}

/* Add copy-in/out stmts for the default schedule. */
static __isl_give isl_schedule_node *sa_add_copies(
    struct autosa_gen *gen, __isl_take isl_schedule_node *node)
{
    struct autosa_kernel *kernel;
    isl_id *id;
    isl_set *host_domain;
    isl_union_pw_multi_aff *contraction;
    int single_statement;

    id = isl_schedule_node_mark_get_id(node);
    kernel = (struct autosa_kernel *)isl_id_get_user(id);
    host_domain = kernel->host_domain;
    single_statement = kernel->single_statement;

    /* Add the copy statements. */
    node = add_copies(kernel, node);

    if (create_kernel_vars(kernel) < 0)
        node = isl_schedule_node_free(node);

    if (!single_statement)
        node = isl_schedule_node_parent(node);

    isl_id_free(id);

    return node;
}

/* Perform computation and commmunication management to update the 
 * "schedule" for mapping to FPGA.
 *
 * Unlike PPCG, in AutoSA, only one SA kernel is created out of the 
 * original program, which is guaranteed by the previous step.
 * We will insert a context node, create a autosa_kernel for the schedule tree
 * beneath. Nodes for copying arrays in and out of the FPGA device and for
 * initializing and clearing the device are added. 
 *
 * The FPGA code is generated in a context where at least one statement 
 * instance is executed. The corresponding guard is inserted around 
 * the entire schedule.
 */
__isl_give isl_schedule *sa_map_to_device(struct autosa_gen *gen,
                                          __isl_take isl_schedule *schedule)
{
    isl_schedule_node *node;
    isl_set *context;
    isl_set *guard;
    isl_union_set *domain;
    isl_union_map *prefix;
    isl_union_pw_multi_aff *contraction;
    struct autosa_prog *prog;
    isl_schedule *hw_schedule;
    struct autosa_kernel *kernel;
    isl_id *id;
    cJSON *tuning_config = NULL;

    /* Load the tuning configuration file */
    tuning_config = load_tuning_config(gen->options->autosa->config);
    if (!tuning_config)
    {
        isl_schedule_free(schedule);
        printf("[AutoSA] Error: AutoSA configuration file not found: %s\n",
               gen->options->autosa->config);
        exit(1);
    }
    gen->tuning_config = tuning_config;

    context = isl_set_copy(gen->prog->context);
    context = isl_set_from_params(context);
    schedule = isl_schedule_insert_context(schedule, context);

    prog = gen->prog;
    guard = isl_union_set_params(isl_union_set_copy(prog->scop->domain));
    prog->context = isl_set_intersect(prog->context, isl_set_copy(guard));
    guard = isl_set_from_params(guard);

    node = isl_schedule_get_root(schedule);
    isl_schedule_free(schedule);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_child(node, 0);
    domain = isl_schedule_node_get_domain(node);
    contraction = isl_schedule_node_get_subtree_contraction(node);
    domain = isl_union_set_preimage_union_pw_multi_aff(domain,
                                                       isl_union_pw_multi_aff_copy(contraction));
    prefix = isl_schedule_node_get_prefix_schedule_union_map(node);
    prefix = isl_union_map_preimage_domain_union_pw_multi_aff(prefix,
                                                              contraction);

    /* Perform compute and comm optimization. */
    node = compute_and_comm_optimize(gen, node);

    id = isl_schedule_node_mark_get_id(node);
    kernel = (struct autosa_kernel *)isl_id_get_user(id);
    isl_id_free(id);
    schedule = isl_schedule_node_get_schedule(node);    
    /* Generate hw modules in the systolic array. */
    generate_hw_modules(schedule, gen, kernel);

    /* Add copy statements for the default schedule (used for correctness verification). */
    node = sa_add_copies(gen, node);

    /* Add copy-in/out statement for transferring data to/from the FPGA device. */
    node = sa_add_to_from_device(node, domain, prefix, gen->prog);
    node = isl_schedule_node_root(node);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_child(node, 0);
    node = isl_schedule_node_insert_guard(node, guard);
    node = isl_schedule_node_child(node, 0);    

    /* Add init/clear device statements. */
    node = sa_add_init_clear_device(node, kernel);

    /* Add drain merge nodes. */
    node = sa_add_drain_merge(node, gen);    

    isl_schedule_free(gen->schedule);
    gen->schedule = isl_schedule_node_get_schedule(node);
    isl_schedule_node_free(node);
    cJSON_Delete(gen->tuning_config);

    return gen->schedule;
}

/* Generate HLS code for "scop" and print it to "p".
 * After generating an AST for the transformed scop as explained below,
 * we call "gen->print" to print the AST in the desired output format 
 * to "p".
 * 
 * If it turns out that it does not make sense to generate SA code, 
 * then we generate CPU code instead.
 * 
 * The declarations of the arrays that are visible outside of the scop
 * are printed outside of the code generated from the schedule,
 * because the generated code may involve a guard around the entire code.
 * 
 * We first compute a schedule that respects the dependences 
 * of the original program and test if the current program can be mapped to sa.
 * If not, we will generate CPU code instead.
 * If the --load-schedule is specified, then the loaded schedule 
 * is used instead of a computed schedule.
 * 
 * For the candidate program, a sequence of optimizations are performed, 
 * including: 
 * - Space-time Transformation
 * - PE Optimization
 *   - Array Partitioning
 *   - Latency Hiding
 *   - SIMD Vectorization
 * - Data Transfer Optimization
 *   - Data Allocation
 *   - I/O Construction
 *   - I/O Optimization
 * 
 * After the array partitioning, we have a program with
 * K
 * |
 * T
 * |
 * P
 * 
 * We add the kernel marker on top.
 * For each iteration of the T band and for each array, we compute
 * the array elements accessed by that iteration, construct a rectangular
 * box around it and shift it to the origin. The result is used
 * as the on-chip memory for the array.
 * 
 * Copying statements are added to this schedule tree.
 * In practice, these are added in front of the P band, but some of them 
 * may get hoisted up to higher levels.
 * 
 * The entire AST is then generated from the single resulting schedule tree.
 * During the generation the subtrees at kernel nodes (K) are saved aside and
 * replaced by kernel calls. The result is printed as host code while the saved
 * subtrees are printed as device code.
 */
static __isl_give isl_printer *generate(__isl_take isl_printer *p,
                                        struct autosa_gen *gen, struct ppcg_scop *scop,
                                        struct ppcg_options *options)
{
    struct autosa_prog *prog;
    isl_ctx *ctx;
    isl_schedule *schedule;
    isl_bool any_sa;

    if (!scop)
        return isl_printer_free(p);

    ctx = isl_printer_get_ctx(p);
    prog = autosa_prog_alloc(ctx, scop);
    if (!prog)
        return isl_printer_free(p);

    gen->prog = prog;
    /* Scheduling */
    schedule = get_schedule(gen);

//#ifdef _DEBUG
//    DBGSCHD(stdout, schedule, gen->ctx);
//#endif

    /* Hack: If we disable reschedule, we will try another time
     * here to merge some of the schedule bands. 
     */
    if (!gen->options->reschedule) {
        schedule = merge_outer_bands(schedule, gen);
    }

//#ifdef _DEBUG
//    DBGSCHD(stdout, schedule, gen->ctx);
//#endif

    /* Legality check */
    isl_bool is_legal = sa_legality_check(schedule, scop);
    if (is_legal < 0 || !is_legal)
    {
        if (is_legal < 0)
            p = isl_printer_free(p);
        else
            p = print_cpu(p, scop, options);
        isl_schedule_free(schedule);
    }
    else
    {
        /* Perform opt. stages:
         * Computation Management -> Communication Management     
         */        
        gen->schedule = sa_map_to_device(gen, schedule);        

        /* Generate the AST tree. */
        gen->tree = sa_generate_code(gen, gen->schedule);
        for (int i = 0; i < gen->n_hw_modules; i++)
        {
            if (gen->hw_modules[i]->is_filter == 1 &&
                gen->hw_modules[i]->is_buffer == 1)
            {
                sa_filter_buffer_io_module_generate_code(gen, gen->hw_modules[i]);
            }
            else
            {
                sa_module_generate_code(gen, gen->hw_modules[i]);
            }
        }
        sa_top_module_generate_code(gen);
        for (int i = 0; i < gen->n_drain_merge_funcs; i++)
        {
            sa_drain_merge_generate_code(gen, gen->drain_merge_funcs[i]);
        }
        if (gen->options->autosa->host_serialize)
        {
            for (int i = 0; i < gen->n_hw_modules; i++)
            {
                if (gen->hw_modules[i]->to_mem)
                {
                    sa_host_serialize_generate_code(gen, gen->hw_modules[i]);
                }
            }
        }

        /* Extract loop structure for latency estimation */
        for (int i = 0; i < gen->n_hw_modules; i++)
        {
            sa_extract_loop_info(gen, gen->hw_modules[i]);
        }
        /* Dump out the array information */
        sa_extract_array_info(gen->kernel);
        /* Extract design information for resource estimation */
        sa_extract_design_info(gen);

        /* Code generation */
        p = ppcg_set_macro_names(p);
        p = ppcg_print_exposed_declarations(p, prog->scop);
        p = gen->print(p, gen->prog, gen->tree, gen->hw_modules, gen->n_hw_modules,
                       gen->hw_top_module, gen->drain_merge_funcs, gen->n_drain_merge_funcs,
                       &gen->types, gen->print_user);

        /* Clean up */
        isl_ast_node_free(gen->tree);
        autosa_kernel_free(gen->kernel);
        for (int i = 0; i < gen->n_hw_modules; i++)
        {
            autosa_hw_module_free(gen->hw_modules[i]);
        }
        free(gen->hw_modules);
        autosa_hw_top_module_free(gen->hw_top_module);
        for (int i = 0; i < gen->n_drain_merge_funcs; i++)
        {
            autosa_drain_merge_func_free(gen->drain_merge_funcs[i]);
        }
        free(gen->drain_merge_funcs);
    }

    autosa_prog_free(prog);

    return p;
}

/* Wrapper around generate for use as a ppcg_transform callback. 
 */
static __isl_give isl_printer *generate_wrap(__isl_take isl_printer *p,
                                             struct ppcg_scop *scop, void *user)
{
    struct autosa_gen *gen = (struct autosa_gen *)user;

    return generate(p, gen, scop, gen->options);
}

/* Transform the code in the file called "input" by replacing 
 * all scops by corresponding HLS code and write the results to "out".
 */
int generate_sa(isl_ctx *ctx, const char *input, FILE *out,
                struct ppcg_options *options,
                __isl_give isl_printer *(*print)(__isl_take isl_printer *p,
                                                 struct autosa_prog *prog, __isl_keep isl_ast_node *trees,
                                                 struct autosa_hw_module **modules, int n_module,
                                                 struct autosa_hw_top_module *module,
                                                 struct autosa_drain_merge_func **drain_merge_funcs, int n_drain_merge_funcs,
                                                 struct autosa_types *types, void *user),
                void *user)
{
    struct autosa_gen gen;
    int r;
    int i;

    gen.ctx = ctx;
    gen.sizes = extract_sizes_from_str(ctx, options->sizes);
    gen.options = options;
    gen.kernel_id = 0;
    gen.print = print;
    gen.print_user = user;
    gen.types.n = 0;
    gen.types.name = NULL;
    gen.hw_modules = NULL;
    gen.n_hw_modules = 0;
    gen.hw_top_module = NULL;
    gen.drain_merge_funcs = NULL;
    gen.n_drain_merge_funcs = 0;
    gen.schedule = NULL;
    gen.kernel = NULL;
    gen.tuning_config = NULL;

    if (options->debug->dump_sizes)
    {
        isl_space *space = isl_space_params_alloc(ctx, 0);
        gen.used_sizes = isl_union_map_empty(space);
    }

    r = ppcg_transform(ctx, input, out, options, &generate_wrap, &gen);

    if (options->debug->dump_sizes)
    {
        isl_union_map_dump(gen.used_sizes);
        isl_union_map_free(gen.used_sizes);
    }

    isl_union_map_free(gen.sizes);
    for (i = 0; i < gen.types.n; ++i)
        free(gen.types.name[i]);
    free(gen.types.name);

    return r;
}
