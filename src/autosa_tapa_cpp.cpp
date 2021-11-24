#include <isl/ctx.h>

#include "autosa_tapa_cpp.h"
#include "autosa_common.h"
#include "autosa_comm.h"
#include "autosa_print.h"
#include "autosa_trans.h"
#include "autosa_codegen.h"
#include "autosa_utils.h"

#include <set>

struct print_host_user_data
{
  struct hls_info *hls;
  struct autosa_prog *prog;
  struct autosa_hw_top_module *top;
};

struct print_hw_module_data
{
  struct hls_info *hls;
  struct autosa_prog *prog;
  struct autosa_hw_module *module;
  /* Used for double buffer codegen. Modify the printed iterator prefix. */
  const char *iterator_prefix;
};

/* Print the includes for TAPA host.
 */
static void print_tapa_host_header(FILE *fp)
{
  fprintf(fp, "#include <tapa.h>\n");
  fprintf(fp, "using tapa::aligned_allocator;");
}

/* Open the host .cpp file and the kernel .h and .cpp files for writing.
 * Add the necessary includes.
 */
static void hls_open_files(struct hls_info *info, const char *input)
{
  char name[PATH_MAX];
  char dir[PATH_MAX];
  int len, len_dir;
  isl_printer *p_str;
  char *file_path;

  p_str = isl_printer_to_str(info->ctx);
  p_str = isl_printer_print_str(p_str, info->output_dir);
  p_str = isl_printer_print_str(p_str, "/src/");
  file_path = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  len = ppcg_extract_base_name(name, input);
  /* Add the prefix */
  sprintf(dir, "%s", file_path);
  len_dir = strlen(file_path);

  strcpy(name + len, "_host.cpp");
  strcpy(dir + len_dir, name);
  info->host_c = fopen(dir, "w");
  if (!info->host_c)
  {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

  strcpy(name + len, "_host.h");
  strcpy(dir + len_dir, name);
  info->host_h = fopen(dir, "w");

  fprintf(info->host_h, "template <typename T1, typename T2> "                                                                                                                                                                              
          "inline T1 min(T1 x, T2 y) { return (x < T1(y)) ? x : T1(y); }\n");
  fprintf(info->host_h, "template <typename T1, typename T2> "
          "inline T1 max(T1 x, T2 y) { return (x > T1(y)) ? x : T1(y); }\n");
  fprintf(info->host_h, "\n");
  print_tapa_host_header(info->host_h);
  fprintf(info->host_c, "#include \"%s\"\n", name);

  strcpy(name + len, "_kernel_modules.cpp");
  strcpy(dir + len_dir, name);
  info->kernel_c = fopen(dir, "w");
  if (!info->kernel_c)
  {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

  strcpy(name + len, "_kernel.h");
  strcpy(dir + len_dir, name);
  info->kernel_h = fopen(dir, "w");
  if (!info->kernel_h)
  {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

  fprintf(info->host_c, "#include <assert.h>\n");
  fprintf(info->host_c, "#include <stdio.h>\n");
  fprintf(info->host_c, "#include \"%s\"\n\n", name);
  fprintf(info->kernel_c, "#include \"%s\"\n", name);

  strcpy(name + len, "_top_gen.cpp");
  strcpy(dir + len_dir, name);
  info->top_gen_c = fopen(dir, "w");

  strcpy(name + len, "_top_gen.h");
  strcpy(dir + len_dir, name);
  info->top_gen_h = fopen(dir, "w");

  fprintf(info->top_gen_c, "#include <isl/printer.h>\n");
  fprintf(info->top_gen_c, "#include \"%s\"\n", name);

  fprintf(info->kernel_h, "#include <tapa.h>\n");
  fprintf(info->kernel_h, "#include <ap_int.h>\n");
  fprintf(info->kernel_h, "\n");

  fprintf(info->kernel_c, "template <typename T1, typename T2> "
          "inline T1 min(T1 x, T2 y) { return (x < T1(y)) ? x : T1(y); }\n");
  fprintf(info->kernel_c, "template <typename T1, typename T2> "
          "inline T1 max(T1 x, T2 y) { return (x > T1(y)) ? x : T1(y); }\n");
  fprintf(info->kernel_c, "\n");

  free(file_path);
}

/* Close all output files.
 */
static void hls_close_files(struct hls_info *info)
{
  isl_printer *p_str;
  char *complete;
  FILE *f;

  fclose(info->kernel_c);
  fclose(info->kernel_h);
  fclose(info->host_c);
  fclose(info->host_h);
  fclose(info->top_gen_c);
  fclose(info->top_gen_h);

  p_str = isl_printer_to_str(info->ctx);
  p_str = isl_printer_print_str(p_str, info->output_dir);
  p_str = isl_printer_print_str(p_str, "/src/completed");
  complete = isl_printer_get_str(p_str);
  isl_printer_free(p_str);
  f = fopen(complete, "w");
  fclose(f);
  free(complete);
}

/* Extract the data pack factors for each I/O buffer allocated for the current
 * I/O group.
 * Only insert the data pack factor that is not found in the current list
 * "data_pack_factors".
 * The list is in ascending order.
 */
static int *extract_data_pack_factors(int *data_pack_factors,
                                      int *n_factor, struct autosa_array_ref_group *group)
{
  /* Test if the group default packing factor needs to be inserted */
  if (group->n_lane > 1)
  {
    int n_lane = group->n_lane;
    bool insert = true;
    int pos = 0;
    for (pos = 0; pos < *n_factor; pos++)
    {
      if (n_lane > data_pack_factors[pos])
      {
        if (pos < *n_factor - 1)
        {
          if (n_lane < data_pack_factors[pos + 1])
          {
            // insert @pos+1
            pos++;
            break;
          }
        }
      }
      else if (n_lane == data_pack_factors[pos])
      {
        insert = false;
        break;
      }
    }

    if (insert) {
      *n_factor = *n_factor + 1;
      data_pack_factors = (int *)realloc(data_pack_factors,
                                         sizeof(int) * (*n_factor));
      for (int j = *n_factor - 1; j > pos; j--)
      {
        data_pack_factors[j] = data_pack_factors[j - 1];
      }
      data_pack_factors[pos] = n_lane;
    }
  }

  for (int i = 0; i < group->n_io_buffer; i++)
  {
    struct autosa_io_buffer *buf = group->io_buffers[i];
    bool insert = true;
    int pos = 0;
    for (pos = 0; pos < *n_factor; pos++)
    {
      if (buf->n_lane > data_pack_factors[pos])
      {
        if (pos < *n_factor - 1)
        {
          if (buf->n_lane < data_pack_factors[pos + 1])
          {
            // insert @pos+1
            pos++;
            break;
          }
        }
      }
      else if (buf->n_lane == data_pack_factors[pos])
      {
        insert = false;
        break;
      }
    }

    if (!insert)
      continue;

    *n_factor = *n_factor + 1;
    data_pack_factors = (int *)realloc(data_pack_factors,
                                       sizeof(int) * (*n_factor));
    for (int j = *n_factor - 1; j > pos; j--)
    {
      data_pack_factors[j] = data_pack_factors[j - 1];
    }
    data_pack_factors[pos] = buf->n_lane;
  }

  return data_pack_factors;
}

/* Examine the local buffers of each array group.
 * Extract the data pack factors and build the data types
 * required by the program.
 */
static isl_stat print_data_types_tapa(
    struct autosa_hw_top_module *top, struct hls_info *hls)
{
  isl_printer *p;
  struct autosa_kernel *kernel;

  kernel = top->kernel;
  p = isl_printer_to_file(kernel->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_str_new_line(p, "/* Data Type */");

  /* Print the primitive data type. */
  for (int i = 0; i < kernel->n_array; i++) {
    struct autosa_local_array_info *local = &kernel->array[i];
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "typedef ");
    p = isl_printer_print_str(p, local->array->type);
    p = isl_printer_print_str(p, " ");
    p = isl_printer_print_str(p, local->array->name);
    p = isl_printer_print_str(p, "_t1;");
    p = isl_printer_end_line(p);
  }

  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local = &kernel->array[i];
    int *data_pack_factors = (int *)malloc(sizeof(int));
    int n_factor = 1;
    /* First insert the default data pack factor for the array. */
    data_pack_factors[0] = local->n_lane;

    /* IO group */
    for (int n = 0; n < local->n_io_group; n++)
    {
      struct autosa_array_ref_group *group = local->io_groups[n];
      data_pack_factors = extract_data_pack_factors(data_pack_factors, &n_factor, group);
    }
    /* Drain group */
    if (local->drain_group)
      data_pack_factors = extract_data_pack_factors(data_pack_factors, &n_factor, local->drain_group);

    if (local->is_sparse) {
      std::set<int> tmp_lanes;
      for (int n = 0; n < n_factor; n++) {
        tmp_lanes.insert(data_pack_factors[n] * kernel->n_nzero);
        tmp_lanes.insert(data_pack_factors[n]);
      }
      for (auto it = tmp_lanes.begin(); it != tmp_lanes.end(); ++it) {
        int f = *it;
        if (local->array->size * 8 * f > 1024) {
          printf("[AutoSA] Warning: The data width %d is greater than 1024-bit. The type definition is not generated.\n", local->array->size * 8 * f);
          continue;
        }
        if (f > 1) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "typedef vec_t<");
          p = isl_printer_print_str(p, local->array->type);
          p = isl_printer_print_str(p, ", ");
          p = isl_printer_print_int(p, f);
          p = isl_printer_print_str(p, "> ");
          p = isl_printer_print_str(p, local->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, f);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }
      }

      for (int n = 0; n < n_factor; n++) {
        if (data_pack_factors[n] * kernel->n_nzero * local->array->size * 8 > 1024)
          continue;
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "typedef struct ");
        p = isl_printer_print_str(p, local->array->name);
        p = isl_printer_print_str(p, "_s_t");
        p = isl_printer_print_int(p, data_pack_factors[n]);
        p = isl_printer_print_str(p, " {");
        p = isl_printer_end_line(p);

        p = isl_printer_indent(p, 2);

        p = isl_printer_start_line(p);
        if (data_pack_factors[n] == 1 && kernel->n_nzero == 1) {
          p = isl_printer_print_str(p, local->array->type);
        } else {
          p = isl_printer_print_str(p, local->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, data_pack_factors[n] * kernel->n_nzero);
        }
        p = isl_printer_print_str(p, " d;");
        p = isl_printer_end_line(p);

        p = isl_printer_start_line(p);
        if (data_pack_factors[n] == 1 && kernel->n_nzero == 1) {
          p = isl_printer_print_str(p, "unsigned char");
        } else {
          p = isl_printer_print_str(p, "tapa::vec_t<");
          p = isl_printer_print_int(p, 8 * data_pack_factors[n]);
          p = isl_printer_print_str(p, ">");
        }
        p = isl_printer_print_str(p, " i;");
        p = isl_printer_end_line(p);

        p = isl_printer_indent(p, -2);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "} ");
        p = isl_printer_print_str(p, local->array->name);
        p = isl_printer_print_str(p, "_s_t");
        p = isl_printer_print_int(p, data_pack_factors[n]);
        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);
      }
    } else {
      for (int n = 0; n < n_factor; n++)
      {
        if (data_pack_factors[n] != 1)
        {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "typedef tapa::vec_t<");
          p = isl_printer_print_str(p, local->array->type);
          p = isl_printer_print_str(p, ", ");
          p = isl_printer_print_int(p, data_pack_factors[n]);
          p = isl_printer_print_str(p, "> ");
          p = isl_printer_print_str(p, local->array->name);
          p = isl_printer_print_str(p, "_t");
          p = isl_printer_print_int(p, data_pack_factors[n]);
          p = isl_printer_print_str(p, ";");
          p = isl_printer_end_line(p);
        }
      }
    }
    free(data_pack_factors);
  }
  p = print_str_new_line(p, "/* Data Type */");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  return isl_stat_ok;
}

static __isl_give isl_printer *declare_and_allocate_arrays(
    __isl_take isl_printer *p, struct autosa_prog *prog,
    struct autosa_kernel *kernel, struct autosa_hw_top_module *top)
{
  p = print_str_new_line(p, "// Allocate memory in host memory");
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    if (local_array->n_mem_ports > 1)
    {
      /* Create multiple host buffers. */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::vector<std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", tapa::aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">>> ");
      p = isl_printer_print_str(p, "dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, local_array->n_mem_ports);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", tapa::aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">> ");
      p = isl_printer_print_str(p, "dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      p = isl_printer_print_str(p, "_tmp");
      p = isl_printer_print_str(p, "(");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, ");");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, ".push_back(dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      p = isl_printer_print_str(p, "_tmp);");
      p = isl_printer_end_line(p);

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");

      if (local_array->host_serialize) {
        /* Allocate additional serialize buffer. */
        /* Create multiple host buffers. */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "std::vector<std::vector<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, ", tapa::aligned_allocator<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, ">>> ");
        p = isl_printer_print_str(p, "dev_");
        p = isl_printer_print_str(p, local_array->array->name);

        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int i = 0; i < ");
        p = isl_printer_print_int(p, local_array->n_mem_ports);
        p = isl_printer_print_str(p, "; i++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "std::vector<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, ", tapa::aligned_allocator<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, ">> ");
        p = isl_printer_print_str(p, "dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "_tmp");
        p = isl_printer_print_str(p, "(");
        p = isl_printer_print_pw_qpolynomial(p, local_array->serialize_bound);
        if (local_array->is_sparse) {
          p = isl_printer_print_str(p, " / ");
          p = isl_printer_print_double(p, (double)local_array->eff_compress_ratio);
        }
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, ".push_back(dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "_tmp);");
        p = isl_printer_end_line(p);

        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
      }
    }
    else
    {
      /* Create a single host buffer. */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ", tapa::aligned_allocator<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, ">> ");
      p = isl_printer_print_str(p, "dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, "(");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, ");");
      p = isl_printer_end_line(p);

      if (local_array->host_serialize) {
        /* Create a single host buffer. */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "std::vector<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, ", tapa::aligned_allocator<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, ">> ");
        p = isl_printer_print_str(p, "dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "(");
        p = isl_printer_print_pw_qpolynomial(p, local_array->serialize_bound);
        if (local_array->is_sparse) {
          p = isl_printer_print_str(p, " / ");
          p = isl_printer_print_double(p, (double)local_array->eff_compress_ratio);
        }
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
  }
  p = isl_printer_end_line(p);

  /* Initialize buffer. */
  p = print_str_new_line(p, "// Initialize host buffers");

  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    if (local_array->n_mem_ports > 1 && local_array->array->copy_in)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, local_array->n_mem_ports);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::copy(reinterpret_cast<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *>(");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, "), reinterpret_cast<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *>(");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, ") + ");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, ", dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, "[i]");
      p = isl_printer_print_str(p, ".begin());");
      p = isl_printer_end_line(p);

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");
    }
    else if (local_array->array->copy_in)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::copy(reinterpret_cast<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *>(");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, "), reinterpret_cast<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *>(");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, ") + ");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, ", dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, ".begin());");
      p = isl_printer_end_line(p);
    }
  }

  /* Perform data serialization if needed. */
  for (int i = 0; i < top->n_hw_modules; i++) {
    struct autosa_hw_module *module = top->hw_modules[i];
    if (module->serialize_tree && module->in) {
      struct autosa_array_ref_group *group = module->io_groups[0];
      struct autosa_local_array_info *local_array = group->local_array;
      if (local_array->n_mem_ports > 1 && local_array->array->copy_in)
      {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "for (int i = 0; i < ");
        p = isl_printer_print_int(p, local_array->n_mem_ports);
        p = isl_printer_print_str(p, "; i++) {");
        p = isl_printer_end_line(p);
        p = isl_printer_indent(p, 2);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, module->in? "host_serialize_" : "host_deserialize_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "(");
        p = print_host_serialize_arguments(p, kernel, group, module, 0, 0);  // TODO: add hbm support later.
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);

        p = isl_printer_indent(p, -2);
        p = print_str_new_line(p, "}");
      } else
      {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, module->in? "host_serialize_" : "host_deserialize_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "(");
        p = print_host_serialize_arguments(p, kernel, group, module, 0, 0);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
  }
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "// Explicitly create TAPA mmap objects");
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "std::vector<");
    if (local_array->array->copy_in) {
      if (local_array->array->copy_out)
        p = isl_printer_print_str(p, "tapa::read_write_mmap<");
      else
        p = isl_printer_print_str(p, "tapa::read_only_mmap<");
    } else if (local_array->array->copy_out)
      p = isl_printer_print_str(p, "tapa::write_only_mmap<");
    else
      p = isl_printer_print_str(p, "tapa::placeholder_mmap<");
    p = isl_printer_print_str(p, local_array->array->type);
    p = isl_printer_print_str(p, ">> buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  }

  p = print_str_new_line(p, "// Set the direction of the TAPA mmap objects");
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    //for (int j = 0; j < local_array->n_mem_ports; j++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int i = 0; i < ");
    p = isl_printer_print_int(p, local_array->n_mem_ports);
    p = isl_printer_print_str(p, "; i++) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);

    p = isl_printer_start_line(p);
    if (local_array->array->copy_in && local_array->array->copy_out) {
      p = isl_printer_print_str(p, "tapa::read_write_mmap<");
    } else {
      if (local_array->array->copy_in)
        p = isl_printer_print_str(p, "tapa::read_only_mmap<");
      else if (local_array->array->copy_out)
        p = isl_printer_print_str(p, "tapa::write_only_mmap<");
    }
    p = isl_printer_print_str(p, local_array->array->type);
    p = isl_printer_print_str(p, "> buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_tmp(");
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, local_array->array->name);
    if (local_array->n_mem_ports > 1) {
      p = isl_printer_print_str(p, "[i]");
    }
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, ".push_back(std::move(buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_tmp));");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }
  p = isl_printer_end_line(p);

  return p;
}


/* Print code for initializing the device for execution of the transformed
 * code. This includes declaring locally defined variables as well as
 * declaring and allocating the required copies of arrays on the device.
 */
static __isl_give isl_printer *init_device_tapa(__isl_take isl_printer *p,
                                                struct autosa_prog *prog,
                                                struct autosa_kernel *kernel,
                                                int hls,
                                                struct autosa_hw_top_module *top)
{
  p = autosa_print_local_declarations(p, prog);
  p = declare_and_allocate_arrays(p, prog, kernel, top);

  return p;
}

/* Print code for clearing the device after execution of the transformed code.
 * In particular, free the memory that was allocated on the device.
 */
static __isl_give isl_printer *clear_device_tapa(__isl_take isl_printer *p,
                                                   struct autosa_prog *prog,
                                                   struct autosa_kernel *kernel,
                                                   int hls,
                                                   struct autosa_hw_top_module *top)
{
  /* Deserialize the buffer data if necessary. */
  for (int i = 0; i < top->n_hw_modules; i++) {
    struct autosa_hw_module *module = top->hw_modules[i];
    if (module->serialize_tree && !module->in) {
      struct autosa_array_ref_group *group = module->io_groups[0];
      struct autosa_local_array_info *local_array = group->local_array;
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "host_deserialize_");
      p = isl_printer_print_str(p, local_array->array->name);
      p = isl_printer_print_str(p, "(");
      p = print_host_serialize_arguments(p, top->kernel, group, module, 0, 0);  // TODO: add hbm support later.
      p = isl_printer_print_str(p, ");");
      p = isl_printer_end_line(p);
    }
  }

  /* Restore buffer */
  p = print_str_new_line(p, "// Restore data from host buffers");
  for (int i = 0; i < prog->n_array; i++)
  {
    struct autosa_array_info *array = &prog->array[i];
    if (!autosa_array_requires_device_allocation(array))
      continue;

    if (array->copy_out)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::copy(dev_");
      p = isl_printer_print_str(p, array->name);
      if (array->local_array->host_serialize) {
        p = isl_printer_print_str(p, "_unserialized");
      }
      if (array->local_array->n_mem_ports > 1)
      {
        p = isl_printer_print_str(p, "[0]");
      }
      p = isl_printer_print_str(p, ".begin(), dev_");
      p = isl_printer_print_str(p, array->name);
      if (array->local_array->host_serialize) {
        p = isl_printer_print_str(p, "_unserialized");
      }
      if (array->local_array->n_mem_ports > 1)
      {
        p = isl_printer_print_str(p, "[0]");
      }
      p = isl_printer_print_str(p, ".end(), reinterpret_cast<");
      p = isl_printer_print_str(p, array->type);
      p = isl_printer_print_str(p, " *>(");
      p = isl_printer_print_str(p, array->name);
      p = isl_printer_print_str(p, "));");
      p = isl_printer_end_line(p);
    }
  }

  return p;
}

static __isl_give isl_printer *drain_merge_tapa(
    __isl_take isl_printer *p, struct autosa_prog *prog,
    struct autosa_drain_merge_func *func,
    int hls)
{
  struct autosa_array_ref_group *group = func->group;
  p = print_str_new_line(p, "// Merge results");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "for (int idx = ");
  p = isl_printer_print_int(p, group->mem_port_id);
  p = isl_printer_print_str(p, "; idx < ");
  p = isl_printer_print_int(p, group->mem_port_id + group->n_mem_ports);
  p = isl_printer_print_str(p, "; idx++) {");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, 2);
  p = isl_printer_start_line(p);
  p = autosa_array_ref_group_print_prefix(group, p);
  p = isl_printer_print_str(p, "_drain_merge(");
  p = print_drain_merge_arguments(p, func->kernel, group, func, 0, hls);
  p = isl_printer_print_str(p, ");");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");
  p = isl_printer_end_line(p);
  return p;
}

/* Print a statement for copying an array to or from the device,
 * or for initializing or clearing the device.
 * The statement identifier of a copying node is called
 * "to_device_<array name>" or "from_device_<array name>" and
 * its user pointer points to the autosa_array_info of the array
 * that needs to be copied.
 * The node for initializing the device is called "init_device".
 * The node for clearing the device is called "clear_device".
 *
 * Extract the array (if any) from the identifier and call
 * init_device, clear_device, copy_array_to_device or copy_array_from_device.
 */
static __isl_give isl_printer *print_device_node_tapa(__isl_take isl_printer *p,
                                                        __isl_keep isl_ast_node *node,
                                                        struct autosa_prog *prog,
                                                        int hls,
                                                        struct autosa_hw_top_module *top)
{
  isl_ast_expr *expr, *arg;
  isl_id *id;
  const char *name;
  struct autosa_array_info *array;
  struct autosa_kernel *kernel;
  struct autosa_drain_merge_func *func;

  expr = isl_ast_node_user_get_expr(node);
  arg = isl_ast_expr_get_op_arg(expr, 0);
  id = isl_ast_expr_get_id(arg);
  name = isl_id_get_name(id);
  if (!strcmp(name, "init_device") || !strcmp(name, "clear_device"))
    kernel = (struct autosa_kernel *)isl_id_get_user(id);
  else if (!strcmp(name, "drain_merge"))
    func = (struct autosa_drain_merge_func *)isl_id_get_user(id);
  else
    array = (struct autosa_array_info *)isl_id_get_user(id);
  isl_id_free(id);
  isl_ast_expr_free(arg);
  isl_ast_expr_free(expr);

  if (!name)
    return isl_printer_free(p);
  if (!strcmp(name, "init_device"))
    return init_device_tapa(p, prog, kernel, hls, top);
  if (!strcmp(name, "clear_device"))
    return clear_device_tapa(p, prog, kernel, hls, top);
  if (!strcmp(name, "drain_merge"))
    return drain_merge_tapa(p, prog, func, hls);
  if (!array)
    return isl_printer_free(p);

  return p;
}

/* Print the header of the given kernel to both gen->hls.kernel_h
 * and gen->hls.kernel_c.
 */
static void print_kernel_headers_tapa(struct autosa_prog *prog,
                                        struct autosa_kernel *kernel, struct hls_info *hls)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_kernel_header(p, prog, kernel, hls, 1);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);

  isl_printer_free(p);
}

/* Print the user statement of the host code to "p".
 *
 * The host code may contain original user statements, kernel launches,
 * statements that copy data to/from the device and statements
 * the initialize or clear the device.
 * The original user statements and the kernel launches have
 * an associated annotation, while the other statements do not.
 * The latter are handled by print_device_node.
 * The annotation on the user statements is called "user".
 *
 * In case of a kernel launch, print a block of statements that
 * defines the grid and the block and then launches the kernel.
 */
static __isl_give isl_printer *print_host_user_tapa(__isl_take isl_printer *p,
                                                      __isl_take isl_ast_print_options *print_options,
                                                      __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  int is_user;
  struct autosa_kernel *kernel;
  struct autosa_kernel_stmt *stmt;
  struct print_host_user_data *data;
  struct hls_info *hls;
  struct autosa_hw_top_module *top;

  isl_ast_print_options_free(print_options);

  data = (struct print_host_user_data *)user;
  hls = data->hls;
  top = data->top;

  id = isl_ast_node_get_annotation(node);
  if (!id)
  {
    return print_device_node_tapa(p, node, data->prog, hls->hls, top);
  }

  is_user = !strcmp(isl_id_get_name(id), "user");
  kernel = is_user ? NULL : (struct autosa_kernel *)isl_id_get_user(id);
  stmt = is_user ? (struct autosa_kernel_stmt *)isl_id_get_user(id) : NULL;
  isl_id_free(id);

  if (is_user)
    return autosa_kernel_print_domain(p, stmt);

  p = print_str_new_line(p, "// Launch the kernel");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "tapa::task().invoke(kernel0, ");
  p = print_kernel_arguments(p, data->prog, kernel, 0, hls);
  p = isl_printer_print_str(p, ");");
  p = isl_printer_end_line(p);

  /* Print the top kernel header. */
  print_kernel_headers_tapa(data->prog, kernel, data->hls);

  return p;
}

/* Print the header of the given module.
 */
static __isl_give isl_printer *print_module_header_tapa(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, struct autosa_hw_module *module,
    int inter, int boundary)
{
  int n = isl_id_list_n_id(module->inst_ids);;
  int first = 1;

  if (n > 0 && prog->scop->options->autosa->use_cplusplus_template) {
    /* Print the index template */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "template<");
    for (int i = 0; i < n; i++) {
      if (!first)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "int p");
      p = isl_printer_print_int(p, i);
      first = 0;
    }
    p = isl_printer_print_str(p, ">");
    p = isl_printer_end_line(p);
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "void ");
  p = isl_printer_print_str(p, module->name);
  if (inter == 0)
    p = isl_printer_print_str(p, "_intra_trans");
  else if (inter == 1)
    p = isl_printer_print_str(p, "_inter_trans");
  if (boundary)
    p = isl_printer_print_str(p, "_boundary");
  p = isl_printer_print_str(p, "(");
  p = print_module_arguments(p, prog, module->kernel, module, 1, TAPA_HW, inter, -1, boundary, 0);
  p = isl_printer_print_str(p, ")");

  return p;
}

/* Print the header of the given module to both gen->hls.kernel_h
 * and gen->hls.kernel_c
 * If "inter" is -1, this is a normal module call.
 * If "inter" is 0, this is a intra_trans module call.
 * If "inter" is 1, this is a inter_trans module call.
 */
static isl_stat print_module_headers_tapa(
    struct autosa_prog *prog, struct autosa_hw_module *module,
    struct hls_info *hls, int inter, int boundary)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_header_tapa(p, prog, module, inter, boundary);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->kernel_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_header_tapa(p, prog, module, inter, boundary);
  isl_printer_free(p);

  return isl_stat_ok;
}

/* Print out variable declarations
 * The local variable can be mapped to different memory resources:
 * FF, LUTRAM, BRAM, URAM.
 */
static __isl_give isl_printer *print_module_var_tapa(
    __isl_take isl_printer *p,
    struct autosa_kernel_var *var, int double_buffer,
    struct autosa_hw_module *module)
{
  int j;
  int use_memory = 0; // 0: FF 1: LUTRAM 2: BRAM 3: URAM
  use_memory = extract_memory_type(module, var, module->options->autosa->uram);

  p = isl_printer_start_line(p);
  if (var->array->local_array->is_sparse && module->type != PE_MODULE) {
    p = isl_printer_print_str(p, var->array->name);
    p = isl_printer_print_str(p, "_s_t");
    p = isl_printer_print_int(p, var->n_lane);
  } else {
    p = isl_printer_print_str(p, var->array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, var->n_lane);
  }
  p = isl_printer_print_str(p, " ");
  p = isl_printer_print_str(p, var->name);
  if (double_buffer)
    p = isl_printer_print_str(p, "_ping");
  for (j = 0; j < isl_vec_size(var->size); ++j)
  {
    isl_val *v;

    p = isl_printer_print_str(p, "[");
    v = isl_vec_get_element_val(var->size, j);
    p = isl_printer_print_val(p, v);
    isl_val_free(v);
    p = isl_printer_print_str(p, "]");
  }
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  if (use_memory && var->n_part != 1)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=");
    p = isl_printer_print_str(p, var->name);
    if (double_buffer)
      p = isl_printer_print_str(p, "_ping");
    p = isl_printer_print_str(p, " dim=");
    p = isl_printer_print_int(p, isl_vec_size(var->size));
    p = isl_printer_print_str(p, " factor=");
    p = isl_printer_print_int(p, var->n_part);
    p = isl_printer_print_str(p, " cyclic");
    p = isl_printer_end_line(p);
  } else if (use_memory == 0) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=");
    p = isl_printer_print_str(p, var->name);
    if (double_buffer)
      p = isl_printer_print_str(p, "_ping");
    p = isl_printer_print_str(p, " dim=0 complete");
    p = isl_printer_end_line(p);
  }

  if (use_memory)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma HLS RESOURCE variable=");
    p = isl_printer_print_str(p, var->name);
    if (double_buffer)
      p = isl_printer_print_str(p, "_ping");
    if (module->type == IO_MODULE && module->data_pack_inter == module->data_pack_intra)
      p = isl_printer_print_str(p, use_memory == 1 ? " core=RAM_1P_LUTRAM" : (use_memory == 2 ? " core=RAM_1P_BRAM" : " core=RAM_1P_URAM"));
    else
      p = isl_printer_print_str(p, use_memory == 1 ? " core=RAM_2P_LUTRAM" : (use_memory == 2 ? " core=RAM_2P_BRAM" : " core=RAM_2P_URAM"));
    p = isl_printer_end_line(p);

    if (var->array->local_array->is_sparse) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "#pragma HLS DATA_PACK variable=");
      p = isl_printer_print_str(p, var->name);
      if (double_buffer)
        p = isl_printer_print_str(p, "_ping");
      p = isl_printer_end_line(p);
    }
  }

  /* Print pong buffer */
  if (double_buffer)
  {
    p = isl_printer_start_line(p);
    if (var->array->local_array->is_sparse) {
      p = isl_printer_print_str(p, var->array->name);
      p = isl_printer_print_str(p, "_s_t");
      p = isl_printer_print_int(p, var->n_lane);
    } else {
      if (var->n_lane == 1)
        p = isl_printer_print_str(p, var->array->type);
      else {
        p = isl_printer_print_str(p, var->array->name);
        p = isl_printer_print_str(p, "_t");
        p = isl_printer_print_int(p, var->n_lane);
      }
    }
    p = isl_printer_print_str(p, " ");
    p = isl_printer_print_str(p, var->name);
    if (double_buffer)
      p = isl_printer_print_str(p, "_pong");
    for (j = 0; j < isl_vec_size(var->size); ++j)
    {
      isl_val *v;

      p = isl_printer_print_str(p, "[");
      v = isl_vec_get_element_val(var->size, j);
      p = isl_printer_print_val(p, v);
      isl_val_free(v);
      p = isl_printer_print_str(p, "]");
    }
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
    if (use_memory && var->n_part != 1)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=");
      p = isl_printer_print_str(p, var->name);
      if (double_buffer)
        p = isl_printer_print_str(p, "_pong");
      p = isl_printer_print_str(p, " dim=");
      p = isl_printer_print_int(p, isl_vec_size(var->size));
      p = isl_printer_print_str(p, " factor=");
      p = isl_printer_print_int(p, var->n_part);
      p = isl_printer_print_str(p, " cyclic");
      p = isl_printer_end_line(p);
    } else if (use_memory == 0) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "#pragma HLS ARRAY_PARTITION variable=");
      p = isl_printer_print_str(p, var->name);
      if (double_buffer)
        p = isl_printer_print_str(p, "_pong");
      p = isl_printer_print_str(p, " dim=0 complete");
      p = isl_printer_end_line(p);
    }

    if (use_memory)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "#pragma HLS RESOURCE variable=");
      p = isl_printer_print_str(p, var->name);
      p = isl_printer_print_str(p, "_pong");
      if (module->type == IO_MODULE && module->data_pack_inter == module->data_pack_intra)
        p = isl_printer_print_str(p, use_memory == 1 ? " core=RAM_1P_LUTRAM" : (use_memory == 2 ? " core=RAM_1P_BRAM" : " core=RAM_1P_URAM"));
      else
        p = isl_printer_print_str(p, use_memory == 1 ? " core=RAM_2P_LUTRAM" : (use_memory == 2 ? " core=RAM_2P_BRAM" : " core=RAM_2P_URAM"));
      p = isl_printer_end_line(p);

      if (var->array->local_array->is_sparse) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "#pragma HLS DATA_PACK variable=");
        p = isl_printer_print_str(p, var->name);
        p = isl_printer_print_str(p, "_pong");
        p = isl_printer_end_line(p);
      }
    }
  }

  return p;
}

static __isl_give isl_printer *print_module_vars_tapa(__isl_take isl_printer *p,
                                                        struct autosa_hw_module *module, int inter)
{
  int i, n;
  isl_space *space;
  const char *type;

  if (inter == -1)
  {
    for (i = 0; i < module->n_var; ++i)
      p = print_module_var_tapa(p, &module->var[i], module->double_buffer, module);
  }

  if (module->double_buffer && inter == -1)
  {
    type = isl_options_get_ast_iterator_type(module->kernel->ctx);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "bool arb = 0;");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->in ? "bool inter_trans_en = 1;" : "bool inter_trans_en = 0;");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->in ? "bool intra_trans_en = 0;" : "bool intra_trans_en = 1;");
    p = isl_printer_end_line(p);
    /* iterators */
    space = (module->in) ? module->intra_space : module->inter_space;
    n = isl_space_dim(space, isl_dim_set);
    for (int i = 0; i < n; i++)
    {
      const char *name;
      name = isl_space_get_dim_name(space, isl_dim_set, i);
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, type);
      p = isl_printer_print_str(p, " ");
      p = isl_printer_print_str(p, name);
      p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, name);
      p = isl_printer_print_str(p, "_prev");
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);
    }
  }

  return p;
}

static __isl_give isl_printer *print_for_with_pipeline(
    __isl_keep isl_ast_node *node, __isl_take isl_printer *p,
    __isl_take isl_ast_print_options *print_options)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#pragma HLS PIPELINE II=1");
  p = isl_printer_end_line(p);

  p = isl_ast_node_for_print(node, p, print_options);

  return p;
}

static __isl_give isl_printer *print_for_with_unroll(
    __isl_keep isl_ast_node *node, __isl_take isl_printer *p,
    __isl_take isl_ast_print_options *print_options)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#pragma HLS UNROLL");
  p = isl_printer_end_line(p);

  p = isl_ast_node_for_print(node, p, print_options);

  return p;
}

static __isl_give isl_printer *print_for_tapa(__isl_take isl_printer *p,
                                                __isl_take isl_ast_print_options *print_options,
                                                __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  int pipeline;
  int unroll;

  pipeline = 0;
  unroll = 0;
  id = isl_ast_node_get_annotation(node);

  if (id)
  {
    struct autosa_ast_node_userinfo *info;

    info = (struct autosa_ast_node_userinfo *)isl_id_get_user(id);
    if (info && info->is_pipeline)
      pipeline = 1;
    if (info && info->is_unroll)
      unroll = 1;
  }

  if (pipeline)
    p = print_for_with_pipeline(node, p, print_options);
  else if (unroll)
    p = print_for_with_unroll(node, p, print_options);
  else
    p = isl_ast_node_for_print(node, p, print_options);

  isl_id_free(id);

  return p;
}

/* Print the intra_trans module.
 */
static __isl_give isl_printer *autosa_print_intra_trans_module(
    __isl_take isl_printer *p,
    struct autosa_hw_module *module, struct autosa_prog *prog,
    struct hls_info *hls, int boundary)
{
  struct print_hw_module_data hw_data = {hls, prog, module, NULL};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  if (!module->intra_tree)
    return p;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  print_module_headers_tapa(prog, module, hls, 0, boundary);
  fprintf(hls->kernel_c, " {\n");
  /* If double buffer is disabled, the module is then inlined to reduce the
   * overheads.
   * Double buffer module can't inlined, this might cause deadlocks.
   */
  if (module->double_buffer)
    fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  else
    fprintf(hls->kernel_c, "#pragma HLS INLINE\n");
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  p = print_module_vars_tapa(p, module, 0);
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  if (module->double_buffer)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (!intra_trans_en) return;");
    p = isl_printer_end_line(p);
    p = isl_printer_end_line(p);
  }
  /* For local reduce, print the buffer initialization. */
  for (int i = 0; i < module->n_var; i++) {
    if (module->var[i].init_required) {
      p = autosa_print_var_initialization(p, &module->var[i], hls->target);
    }
  }
  p = isl_printer_end_line(p);

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_for_tapa, &hw_data);

  p = isl_ast_node_print(module->intra_tree, p, print_options);
  p = isl_printer_indent(p, -2);

  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

  return p;
}

/* Print the inter_trans module.
 */
static __isl_give isl_printer *autosa_print_inter_trans_module(
    __isl_take isl_printer *p,
    struct autosa_hw_module *module, struct autosa_prog *prog,
    struct hls_info *hls, int boundary)
{
  struct print_hw_module_data hw_data = {hls, prog, module, NULL};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  if (boundary) {
    if (!module->boundary_inter_tree)
      return p;
  } else {
    if (!module->inter_tree)
      return p;
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  print_module_headers_tapa(prog, module, hls, 1, boundary);
  fprintf(hls->kernel_c, " {\n");
  if (module->double_buffer)
    fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  else
    fprintf(hls->kernel_c, "#pragma HLS INLINE\n");
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  p = print_module_vars_tapa(p, module, 1);
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  if (module->double_buffer)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (!inter_trans_en) return;");
    p = isl_printer_end_line(p);
    p = isl_printer_end_line(p);
  }

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_for_tapa, &hw_data);

  p = isl_ast_node_print((boundary == 0) ? module->inter_tree : module->boundary_inter_tree, p, print_options);
  p = isl_printer_indent(p, -2);

  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

  return p;
}

static __isl_give isl_printer *print_module_core_header_tapa(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, struct autosa_hw_module *module,
    int inter, int boundary, int serialize, int types)
{
  int n = isl_id_list_n_id(module->inst_ids);
  if (types && n > 0 && prog->scop->options->autosa->use_cplusplus_template) {
    /* Print the template */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "template<");
    for (int i = 0; i < n; i++) {
      if (i > 0)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "int p");
      p = isl_printer_print_int(p, i);
    }
    p = isl_printer_print_str(p, ">");
    p = isl_printer_end_line(p);
  }

  p = isl_printer_start_line(p);
  if (types)
    p = isl_printer_print_str(p, "void ");
  p = isl_printer_print_str(p, module->name);
  if (inter == 0)
    p = isl_printer_print_str(p, "_intra_trans");
  else if (inter == 1)
    p = isl_printer_print_str(p, "_inter_trans");
  if (boundary)
    p = isl_printer_print_str(p, "_boundary");
  if (serialize)
    p = isl_printer_print_str(p, "_serialize");
  if (!types && n > 0 && prog->scop->options->autosa->use_cplusplus_template) {
    p = isl_printer_print_str(p, "<");
    for (int i = 0; i < n; i++) {
      if (i > 0)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "p");
      p = isl_printer_print_int(p, i);
    }
    p = isl_printer_print_str(p, ">");
  }
  p = isl_printer_print_str(p, "(");
  if (!types) {
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);
    p = isl_printer_start_line(p);
  }
  p = print_module_arguments(p, prog, module->kernel, module, types,
                             TAPA_HW, inter, -1, boundary, serialize);
  p = isl_printer_print_str(p, ")");
  if (!types) {
    p = isl_printer_indent(p, -2);
  }

  return p;
}

static __isl_give isl_printer *print_module_core_headers_tapa(
    __isl_take isl_printer *p, struct autosa_prog *prog,
    struct autosa_hw_module *module, struct hls_info *hls,
    int inter, int boundary, int serialize, int types)
{
  p = print_module_core_header_tapa(p, prog, module, inter, boundary, serialize, types);

  return p;
}

static __isl_give isl_printer *print_module_wrapper_header_tapa(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, struct autosa_hw_module *module,
    int inter, int boundary)
{
  int n = isl_id_list_n_id(module->inst_ids);
  if (n > 0 && prog->scop->options->autosa->use_cplusplus_template) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "template<");
    for (int i = 0; i < n; i++) {
      if (i > 0)
        p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, "int p");
      p = isl_printer_print_int(p, i);
    }
    p = isl_printer_print_str(p, ">");
    p = isl_printer_end_line(p);
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "void ");
  p = isl_printer_print_str(p, module->name);
  if (inter == 0)
    p = isl_printer_print_str(p, "_intra_trans");
  else if (inter == 1)
    p = isl_printer_print_str(p, "_inter_trans");
  if (boundary)
    p = isl_printer_print_str(p, "_boundary");
  p = isl_printer_print_str(p, "_wrapper");
  p = isl_printer_print_str(p, "(");
  p = print_module_arguments(p, prog, module->kernel, module, 1,
                             TAPA_HW, inter, -1, boundary, 0);
  p = isl_printer_print_str(p, ")");

  return p;
}

static isl_stat print_module_wrapper_headers_tapa(
    struct autosa_prog *prog, struct autosa_hw_module *module,
    struct hls_info *hls, int inter, int boundary)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_wrapper_header_tapa(p, prog, module, inter, boundary);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->kernel_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_wrapper_header_tapa(p, prog, module, inter, boundary);
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  return isl_stat_ok;
}

/* Print the serializaztion module that connects the external memory to the
 * top-level I/O module.
 */
static __isl_give isl_printer *autosa_print_serialize_module(
  __isl_take isl_printer *p,
  struct autosa_hw_module *module, struct autosa_prog *prog,
  struct hls_info *hls, int boundary)
{
  struct print_hw_module_data hw_data = {hls, prog, module, NULL};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  /* Print core. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = print_module_core_headers_tapa(p, prog, module, hls, -1, boundary, 1, 1); // TODO
  fprintf(hls->kernel_c, " {\n");
  fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  p = print_module_serialize_body(p, module, hls);
  p = isl_printer_indent(p, -2);
  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);
  return p;
}

/* Print the default module.
 * For PE modules, we will print a wrapper function to speedup the HLS
 * synthesis.
 * For the rest of the modules, wrapper is disabled.
 */
static __isl_give isl_printer *autosa_print_default_module(
  __isl_take isl_printer *p,
  struct autosa_hw_module *module, struct autosa_prog *prog,
  struct hls_info *hls, int boundary)
{
  if (!boundary) {
    if (!module->device_tree)
      return p;
  } else {
    if (!module->boundary_tree)
      return p;
  }

  bool wrapper = 0;
  struct print_hw_module_data hw_data = {hls, prog, module, NULL};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  /* Print wrapper for PE and L1 IO module */
  if (module->type == PE_MODULE || (module->type != PE_MODULE && module->level == 1))
    wrapper = 1;

  /* Print core. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = print_module_core_headers_tapa(p, prog, module, hls, -1, boundary, 0, 1);
  fprintf(hls->kernel_c, " {\n");
  if (!boundary || !wrapper)
    fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  else
    fprintf(hls->kernel_c, "#pragma HLS INLINE\n");
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  if (prog->scop->options->autosa->block_sparse) {
    for (int i = 0; i < module->n_io_group; i++) {
      struct autosa_array_ref_group *group = module->io_groups[i];
      if (group->local_array->array_type == AUTOSA_EXT_ARRAY) {
        int n_lane = get_io_group_n_lane(module, NULL, group);
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, group->array->name);
        if (group->local_array->is_sparse)
          p = isl_printer_print_str(p, "_s_t");
        else
          p = isl_printer_print_str(p, "_t");
        p = isl_printer_print_int(p, n_lane);
        p = isl_printer_print_str(p, " fifo_data_");
        p = isl_printer_print_str(p, group->array->name);
        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);
      }
    }
  }
  p = print_module_vars_tapa(p, module, -1);
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  if (module->credit && !module->in)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "credit.write(1);");
    p = isl_printer_end_line(p);
  }

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_for_tapa, &hw_data);

  if (!boundary)
    p = isl_ast_node_print(module->device_tree, p, print_options);
  else
    p = isl_ast_node_print(module->boundary_tree, p, print_options);

  if (module->credit && module->in)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int token = credit.read();");
    p = isl_printer_end_line(p);
  }

  p = isl_printer_indent(p, -2);

  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

  if (wrapper) {
    /* Print wrapper. */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);

    print_module_wrapper_headers_tapa(prog, module, hls, -1, boundary);

    fprintf(hls->kernel_c, " {\n");
    p = isl_printer_indent(p, 2);

    p = print_module_core_headers_tapa(p, prog, module, hls, -1, boundary, 0, 0);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, -2);
    fprintf(hls->kernel_c, "}\n");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);

    p = isl_printer_end_line(p);
  }

  /* If the module serialization is enabled, we will print out an extra module
   * for serializing the data. */
  if (module->to_mem && module->options->autosa->host_serialize) {
    p = autosa_print_serialize_module(p, module, prog, hls, boundary);
  }

  return p;
}

static __isl_give isl_printer *print_pe_dummy_module_core_header_tapa(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, struct autosa_pe_dummy_module *module, int types)
{
  struct autosa_array_ref_group *group = module->io_group;

  p = isl_printer_start_line(p);
  if (types)
    p = isl_printer_print_str(p, "void ");
  // group_name
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
  p = isl_printer_print_str(p, "_PE_dummy");
  p = isl_printer_print_str(p, module->in? "_in" : "_out");
  p = isl_printer_print_str(p, "(");
  p = print_pe_dummy_module_arguments(p, prog, module->module->kernel,
                                      module, types, TAPA_HW);
  p = isl_printer_print_str(p, ")");

  return p;
}

static __isl_give isl_printer *print_pe_dummy_module_core_headers_tapa(
    __isl_take isl_printer *p, struct autosa_prog *prog,
    struct autosa_pe_dummy_module *module, struct hls_info *hls, int types)
{
  p = print_pe_dummy_module_core_header_tapa(p, prog, module, types);

  return p;
}

static __isl_give isl_printer *print_pe_dummy_module_wrapper_header_tapa(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, struct autosa_pe_dummy_module *module)
{
  struct autosa_array_ref_group *group = module->io_group;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "void ");
  // group_name
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
  p = isl_printer_print_str(p, "_PE_dummy");
  p = isl_printer_print_str(p, module->in? "_in": "_out");
  p = isl_printer_print_str(p, "_wrapper");
  p = isl_printer_print_str(p, "(");
  p = print_pe_dummy_module_arguments(p, prog, module->module->kernel,
                                      module, 1, TAPA_HW);
  p = isl_printer_print_str(p, ")");

  return p;
}

static isl_stat print_pe_dummy_module_wrapper_headers_tapa(
    struct autosa_prog *prog, struct autosa_pe_dummy_module *module,
    struct hls_info *hls)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_pe_dummy_module_wrapper_header_tapa(p, prog, module);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->kernel_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_pe_dummy_module_wrapper_header_tapa(p, prog, module);
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  return isl_stat_ok;
}

static __isl_give isl_printer *autosa_print_default_pe_dummy_module(
    __isl_take isl_printer *p,
    struct autosa_pe_dummy_module *pe_dummy_module,
    struct autosa_prog *prog, struct hls_info *hls, int boundary)
{
  /* For dummy module, we disable wrapper by default due to the relatively
   * high overheads.
   */
  bool wrapper = 0;
  struct autosa_hw_module *module = pe_dummy_module->module;
  struct print_hw_module_data hw_data = {hls, prog, module};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  /* Print core. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = print_pe_dummy_module_core_headers_tapa(p, prog,
                                              pe_dummy_module, hls, 1);

  fprintf(hls->kernel_c, " {\n");
  if (wrapper)
    fprintf(hls->kernel_c, "#pragma HLS INLINE\n");

  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  p = print_str_new_line(p, "/* Variable Declaration */");

  p = isl_printer_end_line(p);

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_for_tapa, &hw_data);

  p = isl_ast_node_print(pe_dummy_module->device_tree, p, print_options);

  p = isl_printer_indent(p, -2);

  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

  /* Print wrapper. */
  if (wrapper) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);

    print_pe_dummy_module_wrapper_headers_tapa(prog, pe_dummy_module, hls);

    fprintf(hls->kernel_c, " {\n");
    p = isl_printer_indent(p, 2);
    p = print_pe_dummy_module_core_headers_tapa(p, prog, pe_dummy_module, hls, 0);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, -2);
    fprintf(hls->kernel_c, "}\n");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);

    p = isl_printer_end_line(p);
  }

  return p;
}

struct print_db_module_while_data {
  int inter; // -1: outer 0: intra 1: inter
  int under_if;
  int reach_user;

  isl_printer *p_for;
  isl_printer *p_user;
  /* Outer */
  std::vector<char *> outer_for_logic;
  std::vector<char *> outer_iterator_name;
  std::vector<char *> outer_iterator_lb;
  std::vector<char *> outer_iterator_ub;
  int outer_for_level;
  /* Inter */
  std::vector<char *> inter_for_logic;
  std::vector<char *> inter_iterator_name;
  std::vector<char *> inter_iterator_lb;
  std::vector<char *> inter_iterator_ub;
  int inter_for_level;
  /* Intra */
  std::vector<char *> intra_for_logic;
  std::vector<char *> intra_iterator_name;
  std::vector<char *> intra_iterator_lb;
  std::vector<char *> intra_iterator_ub;
  int intra_for_level;
};

static __isl_give isl_printer *print_double_buffer_module_vars_while(
  __isl_take isl_printer *p, struct autosa_hw_module *module,
  struct hls_info *hls,
  struct print_db_module_while_data *data)
{
  /* Inst ids */
  if (!module->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  /* Local buffer */
  for (int i = 0; i < module->n_var; i++) {
    struct autosa_kernel_var *var = &module->var[i];
    p = isl_printer_start_line(p);
    if (var->n_lane == 1)
      p = isl_printer_print_str(p, var->array->type);
    else
    {
      p = isl_printer_print_str(p, var->array->name);
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, var->n_lane);
    }
    p = isl_printer_print_str(p, " ");
    p = isl_printer_print_str(p, var->name);
    p = isl_printer_print_str(p, "[2]");
    for (int j = 0; j < isl_vec_size(var->size); j++) {
      isl_val *v;

      p = isl_printer_print_str(p, "[");
      v = isl_vec_get_element_val(var->size, j);
      p = isl_printer_print_val(p, v);
      isl_val_free(v);
      p = isl_printer_print_str(p, "]");
    }
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  }

  /* State handle variables */
  p = print_str_new_line(p, "bool arb = 0;");
  p = print_str_new_line(p, module->in? "bool inter_trans_en = 1;" : "bool inter_trans_en = 0;");
  p = print_str_new_line(p, module->in? "bool intra_trans_en = 0;" : "bool intra_trans_en = 1;");
  p = print_str_new_line(p, module->in? "bool inter_done = 0;" : "bool inter_done = 1;");
  p = print_str_new_line(p, module->in? "bool intra_done = 1;" : "bool intra_done = 0;");
  /* Iterators */
  for (int i = 0; i < data->outer_iterator_name.size(); i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int ");
    p = isl_printer_print_str(p, data->outer_iterator_name[i]);
    free(data->outer_iterator_name[i]);
    p = isl_printer_print_str(p, " = ");
    p = isl_printer_print_str(p, data->outer_iterator_lb[i]);
    free(data->outer_iterator_lb[i]);
    p = isl_printer_print_str(p, "; ");
    p = isl_printer_print_str(p, "/* UB: ");
    p = isl_printer_print_str(p, data->outer_iterator_ub[i]);
    free(data->outer_iterator_ub[i]);
    p = isl_printer_print_str(p, " */");
    p = isl_printer_end_line(p);
  }
  for (int i = 0; i < data->inter_iterator_name.size(); i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int ");
    p = isl_printer_print_str(p, data->inter_iterator_name[i]);
    free(data->inter_iterator_name[i]);
    p = isl_printer_print_str(p, " = ");
    p = isl_printer_print_str(p, data->inter_iterator_lb[i]);
    free(data->inter_iterator_lb[i]);
    p = isl_printer_print_str(p, "; ");
    p = isl_printer_print_str(p, "/* UB: ");
    p = isl_printer_print_str(p, data->inter_iterator_ub[i]);
    free(data->inter_iterator_ub[i]);
    p = isl_printer_print_str(p, " */");
    p = isl_printer_end_line(p);
  }
  for (int i = 0; i < data->intra_iterator_name.size(); i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int ");
    p = isl_printer_print_str(p, data->intra_iterator_name[i]);
    free(data->intra_iterator_name[i]);
    p = isl_printer_print_str(p, " = ");
    p = isl_printer_print_str(p, data->intra_iterator_lb[i]);
    free(data->intra_iterator_lb[i]);
    p = isl_printer_print_str(p, "; ");
    p = isl_printer_print_str(p, "/* UB: ");
    p = isl_printer_print_str(p, data->intra_iterator_ub[i]);
    free(data->intra_iterator_ub[i]);
    p = isl_printer_print_str(p, " */");
    p = isl_printer_end_line(p);
  }

  p = print_str_new_line(p, "bool last_run = false;");

  return p;
}

/* Count the for level.
 */
static __isl_give isl_printer *count_module_for(__isl_take isl_printer *p,
                                                __isl_take isl_ast_print_options *print_options,
                                                __isl_keep isl_ast_node *node, void *user)
{
  struct print_db_module_while_data *data = (struct print_db_module_while_data *)user;
  isl_ast_node *body;

  if (data->inter == -1)
    data->outer_for_level++;
  else if (data->inter == 0)
    data->intra_for_level++;
  else if (data->inter == 1)
    data->inter_for_level++;

  body = isl_ast_node_for_get_body(node);
  p = isl_ast_node_print(body, p, print_options);
  isl_ast_node_free(body);

  return p;
}

/* Count the for level. A different implementation.
 * Currently only used for inter_trans module.
 * Since there might be if branches existing, only count one branch.
 * We assume the two branches are with the equal depth.
 */
static isl_bool count_module_for_alt(__isl_keep isl_ast_node *node, void *user) {
  struct print_db_module_while_data *data = (struct print_db_module_while_data *)user;
  if (isl_ast_node_get_type(node) == isl_ast_node_if) {
    data->under_if = 1;
  }

  if (isl_ast_node_get_type(node) == isl_ast_node_for) {
    if (data->under_if == 0 || (data->under_if == 1 && data->reach_user == 0)) {
      data->inter_for_level++;
    }
  }
  if (isl_ast_node_get_type(node) == isl_ast_node_user) {
    data->reach_user = 1;
  }

  return isl_bool_true;
}

/* Extract the loop information.
 */
static __isl_give isl_printer *extract_module_for(__isl_take isl_printer *p,
                                                  __isl_take isl_ast_print_options *print_options,
                                                  __isl_keep isl_ast_node *node, void *user)
{
  struct print_db_module_while_data *data = (struct print_db_module_while_data *)user;
  isl_ast_expr *iterator, *init, *cond, *ub;
  const char *iterator_suffix;
  isl_printer *p_local, *p_str;
  char *text;
  std::vector<char *> text_lines;
  isl_ast_node *body;

  p_local = data->p_for;

  /* Extract the lower bound and upper bound. */
  iterator = isl_ast_node_for_get_iterator(node);
  init = isl_ast_node_for_get_init(node);
  cond = isl_ast_node_for_get_cond(node);
  ub = isl_ast_expr_op_get_arg(cond, 1);

  p_str = isl_printer_to_str(isl_ast_node_get_ctx(node));
  p_str = isl_printer_set_output_format(p_str, ISL_FORMAT_C);
  //p_str = isl_printer_print_str(p_str, iterator_suffix);
  p_str = isl_printer_print_ast_expr(p_str, iterator);
  if (data->inter == -1)
    data->outer_iterator_name.push_back(isl_printer_get_str(p_str));
  else if (data->inter == 0)
    data->intra_iterator_name.push_back(isl_printer_get_str(p_str));
  else if (data->inter == 1)
    data->inter_iterator_name.push_back(isl_printer_get_str(p_str));
  isl_printer_flush(p_str);

  p_str = isl_printer_print_ast_expr(p_str, ub);
  if (data->inter == -1)
    data->outer_iterator_ub.push_back(isl_printer_get_str(p_str));
  else if (data->inter == 0)
    data->intra_iterator_ub.push_back(isl_printer_get_str(p_str));
  else if (data->inter == 1)
    data->inter_iterator_ub.push_back(isl_printer_get_str(p_str));
  isl_printer_flush(p_str);

  p_str = isl_printer_print_ast_expr(p_str, init);
  if (data->inter == -1)
    data->outer_iterator_lb.push_back(isl_printer_get_str(p_str));
  else if (data->inter == 0)
    data->intra_iterator_lb.push_back(isl_printer_get_str(p_str));
  else if (data->inter == 1)
    data->inter_iterator_lb.push_back(isl_printer_get_str(p_str));
  isl_printer_free(p_str);

  p_local = isl_printer_indent(p_local, -4);

  p_local = isl_printer_start_line(p_local);
  p_local = isl_printer_print_ast_expr(p_local, iterator);
  p_local = isl_printer_print_str(p_local, "++;");
  p_local = isl_printer_end_line(p_local);
  text = isl_printer_get_str(p_local);
  text_lines.push_back(text);
  p_local = isl_printer_flush(p_local);

  p_local = isl_printer_start_line(p_local);
  p_local = isl_printer_print_str(p_local, "if (");
  p_local = isl_printer_print_ast_expr(p_local, iterator);
  p_local = isl_printer_print_str(p_local, " == ");
  p_local = isl_printer_print_ast_expr(p_local, ub);
  p_local = isl_printer_print_str(p_local, " + 1) {");
  p_local = isl_printer_end_line(p_local);
  text = isl_printer_get_str(p_local);
  text_lines.push_back(text);
  p_local = isl_printer_flush(p_local);

  p_local = isl_printer_indent(p_local, 4);
  p_local = isl_printer_start_line(p_local);
  p_local = isl_printer_print_ast_expr(p_local, iterator);
  p_local = isl_printer_print_str(p_local, " = ");
  p_local = isl_printer_print_ast_expr(p_local, init);
  p_local = isl_printer_print_str(p_local, ";");
  p_local = isl_printer_end_line(p_local);
  text = isl_printer_get_str(p_local);
  text_lines.push_back(text);
  p_local = isl_printer_flush(p_local);

  if (data->inter == -1)
    data->outer_for_logic.insert(data->outer_for_logic.begin(), text_lines.begin(), text_lines.end());
  else if (data->inter == 0)
    data->intra_for_logic.insert(data->intra_for_logic.begin(), text_lines.begin(), text_lines.end());
  else if (data->inter == 1)
    data->inter_for_logic.insert(data->inter_for_logic.begin(), text_lines.begin(), text_lines.end());

  isl_ast_expr_free(iterator);
  isl_ast_expr_free(init);
  isl_ast_expr_free(cond);
  isl_ast_expr_free(ub);

  p_local = isl_printer_indent(p_local, -4);

  body = isl_ast_node_for_get_body(node);
  p = isl_ast_node_print(body, p, print_options);
  isl_ast_node_free(body);

  return p;
}

static void extract_double_buffer_module_while_data(
  struct autosa_hw_module *module, int boundary,
  struct print_db_module_while_data *data)
{
  isl_ast_print_options *print_options;
  isl_ctx *ctx = module->kernel->ctx;
  isl_printer *p_for, *p_user, *p;
  const char *for_logic, *user_logic;

  /* Outer module */
  data->inter = -1;
  p = isl_printer_to_str(ctx);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p_for = isl_printer_to_str(ctx);
  p_for = isl_printer_set_output_format(p_for, ISL_FORMAT_C);
  p_user = isl_printer_to_str(ctx);
  p_user = isl_printer_set_output_format(p_user, ISL_FORMAT_C);
  data->p_for = p_for;
  data->p_user = p_user;
  data->outer_for_level = 0;

  /* Count the for level first. */
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &count_module_for, data);
  if (!boundary)
    p = isl_ast_node_print(module->device_tree, p, print_options);
  else
    p = isl_ast_node_print(module->boundary_tree, p, print_options);

  /* Extract the for and user logic. */
  data->p_for = isl_printer_indent(data->p_for, 4 * data->outer_for_level);
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &extract_module_for, data);
  if (!boundary)
    p = isl_ast_node_print(module->device_tree, p, print_options);
  else
    p = isl_ast_node_print(module->boundary_tree, p, print_options);
  isl_printer_free(p);
  isl_printer_free(data->p_for);
  isl_printer_free(data->p_user);

  /* Intra module */
  data->inter = 0;
  p = isl_printer_to_str(ctx);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p_for = isl_printer_to_str(ctx);
  p_for = isl_printer_set_output_format(p_for, ISL_FORMAT_C);
  p_user = isl_printer_to_str(ctx);
  p_user = isl_printer_set_output_format(p_user, ISL_FORMAT_C);
  data->p_for = p_for;
  data->p_user = p_user;
  data->intra_for_level = 0;

  /* Count the for level first. */
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &count_module_for, data);
  p = isl_ast_node_print(module->intra_tree, p, print_options);

  /* Extract the for logic. */
  data->p_for = isl_printer_indent(data->p_for, 4 * data->intra_for_level);
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &extract_module_for, data);
  p = isl_ast_node_print(module->intra_tree, p, print_options);
  isl_printer_free(p);
  isl_printer_free(data->p_for);
  isl_printer_free(data->p_user);

  /* Inter module */
  data->inter = 1;
  data->under_if = 0;
  data->reach_user = 0;
  p = isl_printer_to_str(ctx);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p_for = isl_printer_to_str(ctx);
  p_for = isl_printer_set_output_format(p_for, ISL_FORMAT_C);
  p_user = isl_printer_to_str(ctx);
  p_user = isl_printer_set_output_format(p_user, ISL_FORMAT_C);
  data->p_for = p_for;
  data->p_user = p_user;
  data->inter_for_level = 0;

  /* Count the for level first. */
  if (!boundary) {
    isl_ast_node_foreach_descendant_top_down(module->inter_tree, &count_module_for_alt, data);
  } else {
    isl_ast_node_foreach_descendant_top_down(module->boundary_inter_tree, &count_module_for_alt, data);
  }

  /* Extract the for logic. */
  data->p_for = isl_printer_indent(data->p_for, 4 * data->inter_for_level);
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &extract_module_for, data);
  if (!boundary)
    p = isl_ast_node_print(module->inter_tree, p, print_options);
  else
    p = isl_ast_node_print(module->boundary_inter_tree, p, print_options);
  isl_printer_free(p);
  isl_printer_free(data->p_for);
  isl_printer_free(data->p_user);
}

static __isl_give isl_printer *print_null_for(__isl_take isl_printer *p,
                                              __isl_take isl_ast_print_options *print_options,
                                              __isl_keep isl_ast_node *node, void *user)
{
  isl_ast_node *body;

  body = isl_ast_node_for_get_body(node);
  p = isl_ast_node_print(body, p, print_options);
  isl_ast_node_free(body);

  return p;
}

/* Print the inter_trans module in double buffer mode.
 */
static __isl_give isl_printer *autosa_print_inter_trans_module_double_buffer(
  __isl_take isl_printer *p,
  struct autosa_hw_module *module, struct autosa_prog *prog,
  struct hls_info *hls, int boundary)
{
  struct print_hw_module_data hw_data = {hls, prog, module, "inter_c"};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_null_for, &hw_data);

  p = isl_ast_node_print((boundary == 0) ? module->inter_tree : module->boundary_inter_tree, p, print_options);
  p = isl_printer_end_line(p);

  return p;
}

/* Print the intra_trans module in double buffer mode.
 */
static __isl_give isl_printer *autosa_print_intra_trans_module_double_buffer(
  __isl_take isl_printer *p,
  struct autosa_hw_module *module, struct autosa_prog *prog,
  struct hls_info *hls, int boundary)
{
  struct print_hw_module_data hw_data = {hls, prog, module, "intra_c"};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_null_for, &hw_data);

  p = isl_ast_node_print(module->intra_tree, p, print_options);
  p = isl_printer_end_line(p);

  return p;
}

/* Print the double buffer module using while loops instead of for loops.
 * First, we will change the buffer to
 * local_buffer[2][...][...].
 *
 * Specifically, when handling a code structure:
 * [outer for loops]
 * for ...
 *   for ...
 * [outer for loops]
 * {
 *   if (arb) {
 *     ld(local_buffer_ping, ld_en);
 *     st(local_buffer_pong, st_en);
 *   else {
 *     ld(local_buffer_pong, ld_en);
 *     st(local_buffer_ping, st_en);
 *   }
 *   [state handle logic]
 *   arb = !arb;
 *   [state handle logic]
 * }
 * [last batch]
 * if (arb) {
 *   st(local_buffer_pong, st_en);
 * } else {
 *   st(local_buffer_ping, st_en);
 * }
 * [last batch]
 * We will convert it to a new code structure:
 * while (1) {
 *   if (ld_en) {
 *     [inlined logic]
 *     ld(local_buffer[arb][...]);
 *     [inlined logic]
 *   }
 *   if (st_en) {
 *     [inlined logic]
 *     st(local_buffer[!arb][...]);
 *     [inlined logic]
 *   }
 *   [state handle logic]
 *   arb = !arb;
 *   ld_en = 1;
 *   st_en = 1;
 *   [state handle logic]
 *   [outer for loops]
 *   outer_iter0++;
 *   if (outer_iter0 == ...) {
 *     outer_iter0 = 0;
 *     [last batch]
 *     ld_en = 0;
 *     [last batch]
 *   }
 *   [outer for loops]
 * }
 *
 * Note that this only works if each for loop structure is a perfectly
 * nested loop so that we could convert to a while loop.
 */
static __isl_give isl_printer *print_double_buffer_module_while(
  __isl_take isl_printer *p, struct autosa_hw_module *module,
  struct autosa_prog *prog, struct hls_info *hls, int boundary)
{
  if (!boundary) {
    if (!module->device_tree)
      return p;
  } else {
    if (!module->boundary_tree)
      return p;
  }

  struct print_db_module_while_data print_data;

  /* Extract the code snippets. */
  extract_double_buffer_module_while_data(module, boundary, &print_data);

  /* Print header */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  print_module_headers_tapa(prog, module, hls, -1, boundary);
  p = print_str_new_line(p, "{");
  p = isl_printer_indent(p, 2);

  /* Print variables */
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = print_double_buffer_module_vars_while(p, module, hls, &print_data);
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  /* Print content */
  p = print_str_new_line(p, "while (1) {");
  p = print_str_new_line(p, "#pragma HLS PIPELINE II=1");
  p = isl_printer_indent(p, 2);

  /* Print inter_trans */
  p = print_str_new_line(p, "if (inter_trans_en) {");
  p = isl_printer_indent(p, 2);
  /* Print the module logic */
  p = autosa_print_inter_trans_module_double_buffer(p, module, prog, hls, boundary);
  /* Print the loop counter */
  for (int i = 0; i < print_data.inter_for_logic.size(); i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, print_data.inter_for_logic[i]);
    free(print_data.inter_for_logic[i]);
  }
  p = isl_printer_indent(p, 4 * print_data.inter_for_level);
  p = print_str_new_line(p, "inter_done = 1;");
  p = print_str_new_line(p, "inter_trans_en = 0;");
  for (int i = 0; i < print_data.inter_for_level; i++) {
    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");

  /* Print intra_trans */
  p = print_str_new_line(p, "if (intra_trans_en) {");
  p = isl_printer_indent(p, 2);
  /* Print the module logic */
  p = autosa_print_intra_trans_module_double_buffer(p, module, prog, hls, boundary);
  /* Print the loop counter */
  for (int i = 0; i < print_data.intra_for_logic.size(); i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, print_data.intra_for_logic[i]);
    free(print_data.intra_for_logic[i]);
  }
  p = isl_printer_indent(p, 4 * print_data.intra_for_level);
  p = print_str_new_line(p, "intra_done = 1;");
  p = print_str_new_line(p, "intra_trans_en = 0;");
  for (int i = 0; i < print_data.intra_for_level; i++) {
    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");

  /* Print state_handle */
  p = print_str_new_line(p, "if (inter_done && intra_done) {");
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "if (last_run) break;");
  p = print_str_new_line(p, "intra_trans_en = 1;");
  p = print_str_new_line(p, "inter_trans_en = 1;");
  p = print_str_new_line(p, "intra_done = 0;");
  p = print_str_new_line(p, "inter_done = 0;");
  p = print_str_new_line(p, "arb = !arb;");
  /* Print the loop counter */
  for (int i = 0; i < print_data.outer_for_logic.size(); i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, print_data.outer_for_logic[i]);
    free(print_data.outer_for_logic[i]);
  }
  p = isl_printer_indent(p, 4 * print_data.outer_for_level);
  p = print_str_new_line(p, module->in? "inter_trans_en = 0;" : "intra_trans_en = 0;");
  p = print_str_new_line(p, module->in? "inter_done = 1;" : "intra_done = 1;");
  p = print_str_new_line(p, "last_run = true;");
  for (int i = 0; i < print_data.outer_for_level; i++) {
    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  /* If the module serialization is enabled, we will print out an extra module
   * for serializing the data. */
  if (module->to_mem && module->options->autosa->host_serialize) {
    p = autosa_print_serialize_module(p, module, prog, hls, boundary);
  }

  return p;
}

static __isl_give isl_printer *autosa_print_host_code(__isl_take isl_printer *p,
                                                      struct autosa_prog *prog, __isl_keep isl_ast_node *tree,
                                                      struct autosa_hw_module **modules, int n_modules,
                                                      struct autosa_hw_top_module *top,
                                                      struct autosa_drain_merge_func **drain_merge_funcs, int n_drain_merge_funcs,
                                                      struct hls_info *hls)
{
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_ast_node_get_ctx(tree);
  struct print_host_user_data data = {hls, prog, top};
  struct print_hw_module_data hw_data = {hls, prog, NULL};
  isl_printer *p_module;

  /* Print the data pack types in the program. */
  print_data_types_tapa(top, hls);

  /* Print the macros for sparse data structure */
  if (prog->scop->options->autosa->block_sparse) {
    print_sparse_macros(top->kernel, hls);
  }

  /* Print the helper functions in the program. */
  print_drain_merge_funcs(top->kernel, drain_merge_funcs, n_drain_merge_funcs, hls);

  /* Print the host data serialization function. */
  print_host_serialize_funcs(top->kernel, modules, n_modules, hls); // TODO

  /* Print the default AST. */
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_host_user_tapa, &data);

  /* Print the macros definitions in the program. */
  p = autosa_print_macros(p, tree);
  p = isl_ast_node_print(tree, p, print_options);

  /* Print the hw module ASTs. */
  p_module = isl_printer_to_file(ctx, hls->kernel_c);
  p_module = isl_printer_set_output_format(p_module, ISL_FORMAT_C);

  for (int i = 0; i < n_modules; i++)
  {
    if (modules[i]->double_buffer && modules[i]->options->autosa->double_buffer_style == 0)
    {
      p_module = print_double_buffer_module_while(p_module, modules[i], prog, hls, 0);
      if (modules[i]->boundary) {
        p_module = print_double_buffer_module_while(p_module, modules[i], prog, hls, 1);
      }
    } else {
      if (modules[i]->is_filter && modules[i]->is_buffer)
      {
        /* Print out the definitions for inter_trans and intra_trans function calls. */
        /* Intra transfer function */
        p_module = autosa_print_intra_trans_module(p_module, modules[i], prog, hls, 0);

        /* Inter transfer function */
        p_module = autosa_print_inter_trans_module(p_module, modules[i], prog, hls, 0);
        if (modules[i]->boundary)
          p_module = autosa_print_inter_trans_module(p_module, modules[i], prog, hls, 1);
      }

      p_module = autosa_print_default_module(p_module, modules[i], prog, hls, 0);

      if (modules[i]->boundary)
      {
        /* Print out the definitions for boundary trans function calls. */
        p_module = autosa_print_default_module(p_module, modules[i], prog, hls, 1);
      }

      if (modules[i]->n_pe_dummy_modules > 0)
      {
        /* Print out the definitions for pe dummy function calls. */
        for (int j = 0; j < modules[i]->n_pe_dummy_modules; j++)
        {
          p_module = autosa_print_default_pe_dummy_module(
              p_module, modules[i]->pe_dummy_modules[j], prog, hls, 0);
        }
      }
    }
  }
  isl_printer_free(p_module);

  return p;
}

static __isl_give isl_printer *print_top_module_headers_tapa(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, struct autosa_hw_top_module *top, struct hls_info *hls)
{
  struct autosa_kernel *kernel = top->kernel;

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"void kernel");
  p = isl_printer_print_int(p, 0);
  p = isl_printer_print_str(p, "(");
  p = print_kernel_arguments(p, prog, top->kernel, 1, hls);
  p = isl_printer_print_str(p, ")\");");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"{\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  return p;
}

static char *extract_fifo_name_from_fifo_decl_name(isl_ctx *ctx, char *fifo_decl_name)
{
  int loc = 0;
  char ch;
  isl_printer *p_str = isl_printer_to_str(ctx);
  char *name = NULL;

  while ((ch = fifo_decl_name[loc]) != '\0')
  {
    if (ch == '.')
      break;
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    p_str = isl_printer_print_str(p_str, buf);
    loc++;
  }

  name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  return name;
}

static char *extract_fifo_width_from_fifo_decl_name(isl_ctx *ctx, char *fifo_decl_name)
{
  int loc = 0;
  char ch;
  isl_printer *p_str = isl_printer_to_str(ctx);
  char *name = NULL;

  while ((ch = fifo_decl_name[loc]) != '\0')
  {
    if (ch == '.')
      break;
    loc++;
  }

  loc++;

  while ((ch = fifo_decl_name[loc]) != '\0')
  {
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    p_str = isl_printer_print_str(p_str, buf);
    loc++;
  }

  name = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

  return name;
}

static __isl_give isl_printer *print_top_module_fifo_stmt(__isl_take isl_printer *p,
                                                          __isl_take isl_ast_print_options *print_options,
                                                          __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  struct autosa_kernel_stmt *stmt;
  struct print_hw_module_data *data = (struct print_hw_module_data *)(user);

  id = isl_ast_node_get_annotation(node);
  stmt = (struct autosa_kernel_stmt *)isl_id_get_user(id);
  isl_id_free(id);

  isl_ast_print_options_free(print_options);

  switch (stmt->type)
  {
  case AUTOSA_KERNEL_STMT_FIFO_DECL:
    return autosa_kernel_print_fifo_decl(p, stmt, data->prog, data->hls);
  }

  return p;
}

static __isl_give isl_printer *print_top_module_call_stmt(
  __isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options,
  __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  struct autosa_kernel_stmt *stmt;
  struct print_hw_module_data *data = (struct print_hw_module_data *)(user);

  id = isl_ast_node_get_annotation(node);
  stmt = (struct autosa_kernel_stmt *)isl_id_get_user(id);
  isl_id_free(id);

  isl_ast_print_options_free(print_options);

  switch (stmt->type)
  {
  case AUTOSA_KERNEL_STMT_MODULE_CALL:
    return autosa_kernel_print_module_call(p, stmt, data->prog, data->hls->target);
  }

  return p;
}

/* This function prints the code that prints out the top function that
 * calls the hardware modules and declares the fifos.
 */
static void print_top_gen_host_code(
    struct autosa_prog *prog, __isl_keep isl_ast_node *node,
    struct autosa_hw_top_module *top, struct hls_info *hls)
{
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_ast_node_get_ctx(node);
  isl_printer *p;
  int fifo_depth = prog->scop->options->autosa->fifo_depth;
  struct print_hw_module_data hw_data = {hls, prog, NULL};

  /* Print the top module ASTs. */
  p = isl_printer_to_file(ctx, hls->top_gen_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);

  print_top_gen_headers(prog, top, hls);
  fprintf(hls->top_gen_c, " {\n");
  p = isl_printer_indent(p, 2);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "FILE *fd = fopen(\"");
  p = isl_printer_print_str(p, hls->output_dir);
  p = isl_printer_print_str(p, "/resource_est/design_info.dat\", \"w\");");
  p = isl_printer_end_line(p);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "int fifo_cnt;");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "isl_ctx *ctx = isl_ctx_alloc();");
  p = isl_printer_end_line(p);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "isl_printer *p = isl_printer_to_file(ctx, f);");
  p = isl_printer_end_line(p);
  p = isl_printer_end_line(p);

  p = print_top_module_headers_tapa(p, prog, top, hls);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, 2);");
  p = isl_printer_end_line(p);

  /* Print FIFO declarations */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* FIFO Declaration */\");");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
  p = isl_printer_end_line(p);
  p = isl_printer_end_line(p);

  /* Print the serialize fifos if existing. */
  for (int i = 0; i < top->n_hw_modules; i++) {
    struct autosa_hw_module *module = top->hw_modules[i];
    struct autosa_array_ref_group *group = module->io_groups[0];
    if (module->is_serialized) {
      /* Generate fifo decl counter. */
      char *fifo_name;
      int fifo_w;  // bytes
      fifo_w = module->data_pack_inter * group->array->size;
      isl_printer *p_str;
      p_str = isl_printer_to_str(ctx);
      p_str = autosa_array_ref_group_print_fifo_name(group, p_str);
      p_str = isl_printer_print_str(p_str, "_");
      p_str = isl_printer_print_str(p_str, module->name);
      p_str = isl_printer_print_str(p_str, "_serialize");
      fifo_name = isl_printer_get_str(p_str);
      isl_printer_free(p_str);

      p = print_str_new_line(p, "fifo_cnt = 1;");
      p = print_str_new_line(p, "p = isl_printer_start_line(p);");
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* ");
      p = isl_printer_print_str(p, module->name);
      p = isl_printer_print_str(p, "_serialize fifo */ ");
      p = print_fifo_type_tapa(p, group, module->data_pack_inter, fifo_depth, NULL);
      p = isl_printer_print_str(p, " ");
      p = isl_printer_print_str(p, fifo_name);
      p = isl_printer_print_str(p, ";\");");
      p = isl_printer_end_line(p);
      p = print_str_new_line(p, "p = isl_printer_end_line(p);");

      if (group->local_array->is_sparse) {
        p = print_str_new_line(p, "p = isl_printer_start_line(p);");
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS DATA_PACK variable=");
        p = isl_printer_print_str(p, fifo_name);
        p = isl_printer_print_str(p, "\");");
        p = isl_printer_end_line(p);
        p = print_str_new_line(p, "p = isl_printer_end_line(p);");
      }

      /* fifo:fifo_name:fifo_cnt:fifo_width */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "fprintf(fd, \"fifo:");
      p = isl_printer_print_str(p, fifo_name);
      p = isl_printer_print_str(p, ":\%d:");
      p = isl_printer_print_int(p, fifo_w);
      p = isl_printer_print_str(p, "\\n\", fifo_cnt);");
      p = isl_printer_end_line(p);

      p = isl_printer_end_line(p);
      free(fifo_name);
    }
  }

  for (int i = 0; i < top->n_fifo_decls; i++) {
    /* Generate fifo decl counter. */
    char *fifo_decl_name = top->fifo_decl_names[i];
    char *fifo_name = extract_fifo_name_from_fifo_decl_name(ctx, fifo_decl_name);
    char *fifo_w = extract_fifo_width_from_fifo_decl_name(ctx, fifo_decl_name);
    p = print_str_new_line(p, "fifo_cnt = 0;");

    /* Print AST */
    print_options = isl_ast_print_options_alloc(ctx);
    print_options = isl_ast_print_options_set_print_user(print_options,
                                                         &print_top_module_fifo_stmt, &hw_data);

    p = isl_ast_node_print(top->fifo_decl_wrapped_trees[i],
                           p, print_options);

    /* fifo:fifo_name:fifo_cnt:fifo_width */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "fprintf(fd, \"fifo:");
    p = isl_printer_print_str(p, fifo_name);
    p = isl_printer_print_str(p, ":\%d:");
    p = isl_printer_print_str(p, fifo_w);
    p = isl_printer_print_str(p, "\\n\", fifo_cnt);");
    p = isl_printer_end_line(p);

    p = isl_printer_end_line(p);

    free(fifo_name);
    free(fifo_w);
  }

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_start_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"/* FIFO Declaration */\");");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_end_line(p);");
  p = isl_printer_end_line(p);

  int n_module_names = 0;
  char **module_names = NULL;
  for (int i = 0; i < top->n_hw_modules; i++)
  {
    /* Generate module call counter. */
    struct autosa_hw_module *module = top->hw_modules[i];
    char *module_name;

    if (module->is_filter && module->is_buffer)
    {
      module_name = concat(ctx, module->name, "intra_trans");

      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;

      module_name = concat(ctx, module->name, "inter_trans");

      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;

      if (module->boundary)
      {
        module_name = concat(ctx, module->name, "inter_trans_boundary");

        n_module_names++;
        module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
        module_names[n_module_names - 1] = module_name;
      }
    }

    module_name = strdup(module->name);

    n_module_names++;
    module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
    module_names[n_module_names - 1] = module_name;

    if (module->boundary)
    {
      module_name = concat(ctx, module->name, "boundary");

      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;
    }

    if (module->n_pe_dummy_modules > 0)
    {
      for (int j = 0; j < module->n_pe_dummy_modules; j++)
      {
        struct autosa_pe_dummy_module *dummy_module = module->pe_dummy_modules[j];
        struct autosa_array_ref_group *group = dummy_module->io_group;
        isl_printer *p_str = isl_printer_to_str(ctx);
        p_str = autosa_array_ref_group_print_prefix(group, p_str);
        p_str = isl_printer_print_str(p_str, "_PE_dummy");
        p_str = isl_printer_print_str(p_str, dummy_module->in? "_in" : "_out");
        module_name = isl_printer_get_str(p_str);
        isl_printer_free(p_str);

        n_module_names++;
        module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
        module_names[n_module_names - 1] = module_name;
      }
    }

    if (module->is_serialized) {
      if (module->boundary)
        module_name = concat(ctx, module->name, "boundary_serialize");
      else
        module_name = concat(ctx, module->name, "serialize");

      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;
    }
  }
  for (int i = 0; i < n_module_names; i++)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int ");
    p = isl_printer_print_str(p, module_names[i]);
    p = isl_printer_print_str(p, "_cnt = 0;");
    p = isl_printer_end_line(p);
  }

  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"  tapa::task()\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  /* Print module calls. */
  for (int i = 0; i < top->n_module_calls; i++)
  {
    /* Print AST */
    print_options = isl_ast_print_options_alloc(ctx);
    print_options = isl_ast_print_options_set_print_user(print_options,
                                                         &print_top_module_call_stmt, &hw_data);

    p = isl_ast_node_print(top->module_call_wrapped_trees[i],
                           p, print_options);
  }

  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"  ;\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  /* module:module_name:module_cnt. */
  for (int i = 0; i < n_module_names; i++)
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "fprintf(fd, \"module:");
    p = isl_printer_print_str(p, module_names[i]);
    p = isl_printer_print_str(p, ":\%d\\n\", ");
    p = isl_printer_print_str(p, module_names[i]);
    p = isl_printer_print_str(p, "_cnt);");
    p = isl_printer_end_line(p);
  }
  p = isl_printer_end_line(p);

  for (int i = 0; i < n_module_names; i++)
  {
    free(module_names[i]);
  }
  free(module_names);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, -2);");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"}\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = isl_printer_end_line(p);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "fclose(fd);");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "isl_printer_free(p);");
  p = isl_printer_end_line(p);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "isl_ctx_free(ctx);");
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, -2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "}");
  p = isl_printer_end_line(p);
  p = isl_printer_end_line(p);

  /* For internal testing only. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "int main()");
  p = isl_printer_end_line(p);

  p = ppcg_start_block(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "FILE *f = fopen(\"");
  p = isl_printer_print_str(p, hls->output_dir);
  p = isl_printer_print_str(p, "/src/top.cpp\", \"w\");");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "top_generate(f);");
  p = isl_printer_end_line(p);

  p = ppcg_end_block(p);
  p = isl_printer_free(p);

  return;
}

/* Given a autosa_prog "prog" and the corresponding tranformed AST
 * "tree", print the entire OpenCL/HLS code to "p".
 * "types" collects the types for which a definition has already been
 * printed.
 */
static __isl_give isl_printer *print_hw(
    __isl_take isl_printer *p,
    struct autosa_prog *prog, __isl_keep isl_ast_node *tree,
    struct autosa_hw_module **modules, int n_modules,
    struct autosa_hw_top_module *top_module,
    struct autosa_drain_merge_func **drain_merge_funcs, int n_drain_merge_funcs,
    struct autosa_types *types, void *user)
{
  struct hls_info *hls = (struct hls_info *)user;
  isl_printer *p_tmp;

  p_tmp = isl_printer_to_file(isl_printer_get_ctx(p), hls->kernel_c);
  p_tmp = isl_printer_set_output_format(p_tmp, ISL_FORMAT_C);
  p_tmp = autosa_print_types(p_tmp, types, prog);
  p_tmp = isl_printer_free(p_tmp);

  /* Print OpenCL host and kernel function. */
  p = autosa_print_host_code(p, prog, tree, modules, n_modules, top_module,
                             drain_merge_funcs, n_drain_merge_funcs, hls);
  /* Print seperate top module code generation function. */
  print_top_gen_host_code(prog, tree, top_module, hls);

  return p;
}

/* Generate systolic arrays for TAPA C++
 */
int generate_autosa_tapa_cpp(isl_ctx *ctx, struct ppcg_options *options,
                                 const char *input)
{
  struct hls_info hls;
  int r;

  hls.target = TAPA_HW;
  hls.hls = false;
  hls.hcl = false;
  hls.ctx = ctx;
  hls.output_dir = options->autosa->output_dir;
  hls_open_files(&hls, input);

  r = generate_sa(ctx, input, hls.host_c, options, &print_hw, &hls);

  hls_close_files(&hls);

  return r;
}
