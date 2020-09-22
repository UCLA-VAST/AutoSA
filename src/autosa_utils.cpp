#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdexcept>
#include <limits>

#include <isl/space.h>
#include <barvinok/isl.h>

#include "autosa_utils.h"

__isl_give isl_union_map *extract_sizes_from_str(isl_ctx *ctx, const char *str)
{
  if (!str)
    return NULL;
  return isl_union_map_read_from_str(ctx, str);
}

/* Concat the basic maps in the map "el" with the basic map list "user". 
 */
static isl_stat concat_basic_map(__isl_take isl_map *el, void *user)
{
  isl_basic_map_list **bmap_list = (isl_basic_map_list **)(user);
  isl_basic_map_list *bmap_list_sub = isl_map_get_basic_map_list(el);
  if (!(*bmap_list))
  {
    *bmap_list = bmap_list_sub;
  }
  else
  {
    *bmap_list = isl_basic_map_list_concat(*bmap_list, bmap_list_sub);
  }

  isl_map_free(el);
  return isl_stat_ok;
}

/* Extract the basic map list from the union map "umap".
 */
__isl_give isl_basic_map_list *isl_union_map_get_basic_map_list(
    __isl_keep isl_union_map *umap)
{
  isl_map_list *map_list = isl_union_map_get_map_list(umap);
  isl_basic_map_list *bmap_list = NULL;
  isl_map_list_foreach(map_list, &concat_basic_map, &bmap_list);

  isl_map_list_free(map_list);
  return bmap_list;
}

static isl_stat acc_n_basic_map(__isl_take isl_map *el, void *user)
{
  isl_size *n = (isl_size *)(user);
  isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(el);
  *n = *n + isl_basic_map_list_n_basic_map(bmap_list);
  isl_map_free(el);
  isl_basic_map_list_free(bmap_list);
  return isl_stat_ok;
}

/* Return the number of basic maps in the union map "umap".
 */
isl_size isl_union_map_n_basic_map(__isl_keep isl_union_map *umap)
{
  isl_size n = 0;
  isl_map_list *map_list = isl_union_map_get_map_list(umap);
  isl_map_list_foreach(map_list, &acc_n_basic_map, &n);

  isl_map_list_free(map_list);

  return n;
}

__isl_give isl_basic_map *isl_basic_map_from_map(__isl_take isl_map *map)
{
  if (!map)
    return NULL;

  assert(isl_map_n_basic_map(map) == 1);
  isl_basic_map_list *bmap_list = isl_map_get_basic_map_list(map);
  isl_map_free(map);

  isl_basic_map *bmap = isl_basic_map_list_get_basic_map(bmap_list, 0);
  isl_basic_map_list_free(bmap_list);

  return bmap;
}

/* Return a union set containing those elements in the domains
 * of the elements of "mupa" where they are all nonnegative.
 *
 * If there are no elements, then simply return the entire domain.
 */
__isl_give isl_union_set *isl_multi_union_pw_aff_nonneg_union_set(
    __isl_take isl_multi_union_pw_aff *mupa)
{
  int i;
  isl_size n;
  isl_union_pw_aff *upa;
  isl_union_set *nonneg;

  n = isl_multi_union_pw_aff_dim(mupa, isl_dim_set);
  if (n < 0)
    mupa = isl_multi_union_pw_aff_free(mupa);
  if (!mupa)
    return NULL;

  if (n == 0)
    return isl_multi_union_pw_aff_domain(mupa);

  upa = isl_multi_union_pw_aff_get_union_pw_aff(mupa, 0);
  nonneg = isl_union_pw_aff_nonneg_union_set(upa);

  for (i = 1; i < n; ++i)
  {
    isl_union_set *nonneg_i;

    upa = isl_multi_union_pw_aff_get_union_pw_aff(mupa, i);
    nonneg_i = isl_union_pw_aff_nonneg_union_set(upa);

    nonneg = isl_union_set_intersect(nonneg, nonneg_i);
  }

  isl_multi_union_pw_aff_free(mupa);
  return nonneg;
}

/* Compute the set of elements in the domain of "pa" where it is nonnegative 
 * and add this set to "uset".
 */
static isl_stat nonneg_union_set(__isl_take isl_pw_aff *pa, void *user)
{
  isl_union_set **uset = (isl_union_set **)user;

  *uset = isl_union_set_add_set(*uset, isl_pw_aff_nonneg_set(pa));

  return *uset ? isl_stat_ok : isl_stat_error;
}

/* Return a union set containing those elements in the domains
 * of "upa" where it is nonnegative.
 */
__isl_give isl_union_set *isl_union_pw_aff_nonneg_union_set(
    __isl_take isl_union_pw_aff *upa)
{
  isl_union_set *nonneg;

  nonneg = isl_union_set_empty(isl_union_pw_aff_get_space(upa));
  if (isl_union_pw_aff_foreach_pw_aff(upa, &nonneg_union_set, &nonneg) < 0)
    nonneg = isl_union_set_free(nonneg);

  isl_union_pw_aff_free(upa);
  return nonneg;
}

/* Return a union set containing those elements in the domains
 * of the elements of "mupa" where they are all non zero.
 *
 * If there are no elements, then simply return the entire domain.
 */
__isl_give isl_union_set *isl_multi_union_pw_aff_non_zero_union_set(
    __isl_take isl_multi_union_pw_aff *mupa)
{
  int i;
  isl_size n;
  isl_union_pw_aff *upa;
  isl_union_set *non_zero;

  n = isl_multi_union_pw_aff_dim(mupa, isl_dim_set);
  if (n < 0)
    mupa = isl_multi_union_pw_aff_free(mupa);
  if (!mupa)
    return NULL;

  if (n == 0)
    return isl_multi_union_pw_aff_domain(mupa);

  upa = isl_multi_union_pw_aff_get_union_pw_aff(mupa, 0);
  non_zero = isl_union_pw_aff_non_zero_union_set(upa);

  for (i = 1; i < n; ++i)
  {
    isl_union_set *non_zero_i;

    upa = isl_multi_union_pw_aff_get_union_pw_aff(mupa, i);
    non_zero_i = isl_union_pw_aff_nonneg_union_set(upa);

    non_zero = isl_union_set_intersect(non_zero, non_zero_i);
  }

  isl_multi_union_pw_aff_free(mupa);
  return non_zero;
}

/* Compute the set of elements in the domain of "pa" where it is non zero
 * and add this set to "uset".
 */
static isl_stat non_zero_union_set(__isl_take isl_pw_aff *pa, void *user)
{
  isl_union_set **uset = (isl_union_set **)user;
  *uset = isl_union_set_add_set(*uset, isl_pw_aff_non_zero_set(pa));

  return *uset ? isl_stat_ok : isl_stat_error;
}

/* Return a union_set containing those elements in the domains
 * of "upa" where it is non zero.
 */
__isl_give isl_union_set *isl_union_pw_aff_non_zero_union_set(
    __isl_take isl_union_pw_aff *upa)
{
  isl_union_set *non_zero;

  non_zero = isl_union_set_empty(isl_union_pw_aff_get_space(upa));
  if (isl_union_pw_aff_foreach_pw_aff(upa, &non_zero_union_set, &non_zero) < 0)
    non_zero = isl_union_set_free(non_zero);

  isl_union_pw_aff_free(upa);
  return non_zero;
}

/* Print the isl_mat "mat" to "fp".
 */
void print_mat(FILE *fp, __isl_keep isl_mat *mat)
{
  isl_printer *printer = isl_printer_to_file(isl_mat_get_ctx(mat), fp);
  for (int i = 0; i < isl_mat_rows(mat); i++)
  {
    for (int j = 0; j < isl_mat_cols(mat); j++)
    {
      isl_printer_print_val(printer, isl_mat_get_element_val(mat, i, j));
      fprintf(fp, " ");
    }
    fprintf(fp, "\n");
  }
  isl_printer_free(printer);
}

/* Compare the two vectors, return 0 if equal.
 */
int isl_vec_cmp(__isl_keep isl_vec *vec1, __isl_keep isl_vec *vec2)
{
  if (isl_vec_size(vec1) != isl_vec_size(vec2))
    return 1;

  for (int i = 0; i < isl_vec_size(vec1); i++)
  {
    if (isl_vec_cmp_element(vec1, vec2, i))
      return 1;
  }

  return 0;
}

/* Construct the string "<a>_<b>".
 */
char *concat(isl_ctx *ctx, const char *a, const char *b)
{
  isl_printer *p;
  char *s;

  p = isl_printer_to_str(ctx);
  p = isl_printer_print_str(p, a);
  p = isl_printer_print_str(p, "_");
  p = isl_printer_print_str(p, b);
  s = isl_printer_get_str(p);
  isl_printer_free(p);

  return s;
}

bool isl_vec_is_zero(__isl_keep isl_vec *vec)
{
  int n = isl_vec_size(vec);
  for (int i = 0; i < n; i++)
  {
    isl_val *val = isl_vec_get_element_val(vec, i);
    if (!isl_val_is_zero(val))
    {
      isl_val_free(val);
      return false;
    }
    isl_val_free(val);
  }
  return true;
}

int suffixcmp(const char *s, const char *suffix)
{
  int start = strlen(s) - strlen(suffix);
  if (start < 0)
    return 1;
  else
    return strncmp(s + start, suffix, strlen(suffix));
}

/* Add "len" parameters p[i] with identifiers "ids" and intersect "set"
 * with
 *
 *	{ : 0 <= p[i] < size[i] }
 *
 * or an overapproximation.
 */
__isl_give isl_set *add_bounded_parameters_dynamic(
    __isl_take isl_set *set, __isl_keep isl_multi_pw_aff *size,
    __isl_keep isl_id_list *ids)
{
  int i, len;
  unsigned nparam;
  isl_space *space;
  isl_local_space *ls;

  len = isl_multi_pw_aff_dim(size, isl_dim_out);
  nparam = isl_set_dim(set, isl_dim_param);
  set = isl_set_add_dims(set, isl_dim_param, len);

  for (i = 0; i < len; ++i)
  {
    isl_id *id;

    id = isl_id_list_get_id(ids, i);
    set = isl_set_set_dim_id(set, isl_dim_param, nparam + i, id);
  }

  space = isl_space_params(isl_set_get_space(set));
  ls = isl_local_space_from_space(space);
  for (i = 0; i < len; ++i)
  {
    isl_pw_aff *param, *size_i, *zero;
    isl_set *bound;

    param = isl_pw_aff_var_on_domain(isl_local_space_copy(ls),
                                     isl_dim_param, nparam + i);

    size_i = isl_multi_pw_aff_get_pw_aff(size, i);
    bound = isl_pw_aff_lt_set(isl_pw_aff_copy(param), size_i);
    bound = isl_set_from_basic_set(isl_set_simple_hull(bound));
    set = isl_set_intersect_params(set, bound);

    zero = isl_pw_aff_zero_on_domain(isl_local_space_copy(ls));
    bound = isl_pw_aff_ge_set(param, zero);
    set = isl_set_intersect_params(set, bound);
  }
  isl_local_space_free(ls);

  return set;
}

long int convert_pwqpoly_to_int(__isl_keep isl_pw_qpolynomial *to_convert)
{
  isl_ctx *ctx = isl_pw_qpolynomial_get_ctx(to_convert);
  long int ret = -1;
  isl_printer *p;
  char *str;

  p = isl_printer_to_str(ctx);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = isl_printer_print_pw_qpolynomial(p, to_convert);
  str = isl_printer_get_str(p);
  isl_printer_free(p);

  /* Check if the string only contains the digits */
  for (int i = 0; i < strlen(str); i++) 
  {
    if (!isdigit(str[i])) {
      throw std::runtime_error("[AutoSA] Error: The pw_qpolynomial contains non-digits.\n");
    }
  }

  ret = atol(str);
  free(str);

  return ret;
}

char *isl_vec_to_str(__isl_keep isl_vec *vec)
{
  isl_printer *p_str;
  p_str = isl_printer_to_str(isl_vec_get_ctx(vec));
  p_str = isl_printer_print_vec(p_str, vec);
  char *ret = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  return ret;
}

/* Safe conversion to integer value. */
long isl_val_get_num(__isl_take isl_val *val)
{
  long ret;
  isl_val *denominator = isl_val_get_den_val(val)  ;
  assert(isl_val_is_one(denominator));
  isl_val_free(denominator);
  ret = isl_val_get_num_si(val);
  isl_val_free(val);

  return ret;
}

static isl_stat find_pa_min(__isl_take isl_set *set, __isl_take isl_aff *aff, void *user)
{
  long *min = (long *)user;
  if (isl_aff_is_cst(aff)) {
    *min = std::min(*min, isl_val_get_num(isl_aff_get_constant_val(aff)));
  } else {
    *min = std::numeric_limits<long>::min();
  }
  isl_set_free(set);
  isl_aff_free(aff);
  return isl_stat_ok;
}

long compute_set_min(__isl_keep isl_set *set, int dim)
{
  long min = std::numeric_limits<long>::max();
  isl_pw_aff *pa = isl_set_dim_min(isl_set_copy(set), dim);
  isl_pw_aff_foreach_piece(pa, &find_pa_min, &min);
  isl_pw_aff_free(pa);

  return min;  
}

static isl_stat find_pa_max(__isl_take isl_set *set, __isl_take isl_aff *aff, void *user)
{
  long *max = (long *)user;
  if (isl_aff_is_cst(aff)) {
    *max = std::max(*max, isl_val_get_num(isl_aff_get_constant_val(aff)));
  } else {
    *max = std::numeric_limits<long>::max();
  }
  isl_set_free(set);
  isl_aff_free(aff);
  return isl_stat_ok;
}

long compute_set_max(__isl_keep isl_set *set, int dim)
{
  long max = std::numeric_limits<long>::min();
  isl_pw_aff *pa = isl_set_dim_max(isl_set_copy(set), dim);
  isl_pw_aff_foreach_piece(pa, &find_pa_max, &max);
  isl_pw_aff_free(pa);

  return max;  
}
