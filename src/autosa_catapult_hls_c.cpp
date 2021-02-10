#include <isl/ctx.h>

#include "autosa_catapult_hls_c.h"
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

  /* Store the prefix */
  strncpy(dir, name, len);
  dir[len] = '\0';
  p_str = isl_printer_to_str(info->ctx);
  p_str = isl_printer_print_str(p_str, dir);
  info->kernel_prefix = isl_printer_get_str(p_str);
  isl_printer_free(p_str);

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

  //if (!info->hls)
  //{
  //  /* OpenCL host */
  //  strcpy(name + len, "_host.hpp");
  //  strcpy(dir + len_dir, name);
  //  info->host_h = fopen(dir, "w");
  //  print_xilinx_host_header(info->host_h);
  //  fprintf(info->host_c, "#include \"%s\"\n", name);
  //}

  strcpy(name + len, "_directives.tcl");
  strcpy(dir + len_dir, name);
  info->tcl = fopen(dir, "w");
  if (!info->tcl) 
  {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

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

  //fprintf(info->host_c, "#include <assert.h>\n");
  //fprintf(info->host_c, "#include <stdio.h>\n");
  fprintf(info->host_c, "#include <vector>\n");
  fprintf(info->host_c, "#include <cstdlib>\n");
  if (info->hls)
    fprintf(info->host_c, "#include \"%s\"\n", name);

  if (info->hls) {
    fprintf(info->kernel_c, "#include \"%s\"\n", name);
    //fprintf(info->kernel_c, "#include <mc_scverify.h>\n");
  }

  if (info->hls) {
    strcpy(name + len, "_kernel_hw.h");
    fprintf(info->host_c, "#include \"%s\"\n", name);
    fprintf(info->host_c, "#include <mc_scverify.h>\n\n");
  }    

  strcpy(name + len, "_top_gen.cpp");
  strcpy(dir + len_dir, name);
  info->top_gen_c = fopen(dir, "w");

  strcpy(name + len, "_top_gen.h");
  strcpy(dir + len_dir, name);
  info->top_gen_h = fopen(dir, "w");

  fprintf(info->top_gen_c, "#include <isl/printer.h>\n");
  fprintf(info->top_gen_c, "#include \"%s\"\n", name);
  
  fprintf(info->kernel_h, "#ifndef _KERNEL_H_\n");
  fprintf(info->kernel_h, "#define _KERNEL_H_\n");
  fprintf(info->kernel_h, "#include <ac_int.h>\n");
  fprintf(info->kernel_h, "#include <ac_channel.h>\n");
  fprintf(info->kernel_h, "#include <ac_float.h>\n");
  fprintf(info->kernel_h, "#include <ac_std_float.h>\n");
  fprintf(info->kernel_h, "#include <ac_math.h>\n");
  fprintf(info->kernel_h, "\n");

  fprintf(info->kernel_h, "#define min(x,y) ((x < y) ? x : y)\n");
  fprintf(info->kernel_h, "#define max(x,y) ((x > y) ? x : y)\n");
  fprintf(info->kernel_h, "\n");

  free(file_path);
}

/* Close all output files.
 */
static void hls_close_files(struct hls_info *info)
{
  isl_printer *p_str;
  char *complete;
  FILE *f;

  fprintf(info->kernel_h, "#endif\n\n");

  fclose(info->kernel_c);
  fclose(info->kernel_h);
  fclose(info->host_c);
  if (!info->hls)
  {
    fclose(info->host_h);
  }
  fclose(info->top_gen_c);
  fclose(info->top_gen_h);
  fclose(info->tcl);
  free(info->kernel_prefix);

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
static isl_stat print_data_types_catapult(
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
    if (!strcmp(local->array->type, "float")) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "typedef ac_ieee_float<binary32> ");
      p = isl_printer_print_str(p, local->array->name);
      p = isl_printer_print_str(p, "_t1;");
      p = isl_printer_end_line(p);
    } else if (!strcmp(local->array->type, "unsigned short")) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "typedef ac_int<");
      p = isl_printer_print_int(p, local->array->size * 8);
      p = isl_printer_print_str(p, ",false> ");
      p = isl_printer_print_str(p, local->array->name);
      p = isl_printer_print_str(p, "_t1;");
      p = isl_printer_end_line(p);      
    } else if (!strcmp(local->array->type, "unsigned int")) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "typedef ac_int<");
      p = isl_printer_print_int(p, local->array->size * 8);
      p = isl_printer_print_str(p, ",false> ");
      p = isl_printer_print_str(p, local->array->name);
      p = isl_printer_print_str(p, "_t1;");
      p = isl_printer_end_line(p);      
    } else {
      printf("[AutoSA] Warning: The primitive data type is not converted to Catapult data type.\n");
      continue;
    }
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
          //p = isl_printer_print_str(p, "typedef ap_uint<");
          p = isl_printer_print_str(p, "typedef ac_int<");
          p = isl_printer_print_int(p, local->array->size * 8 * f);
          p = isl_printer_print_str(p, ",false");
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
          //p = isl_printer_print_str(p, "ap_uint<");
          p = isl_printer_print_str(p, "ac_int<");
          p = isl_printer_print_int(p, 8 * data_pack_factors[n]);
          p = isl_printer_print_str(p, ",false");
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
          int width;
          width = local->array->size * 8 * data_pack_factors[n];
          p = isl_printer_start_line(p);
          //p = isl_printer_print_str(p, "typedef ap_uint<");
          p = isl_printer_print_str(p, "typedef ac_int<");
          p = isl_printer_print_int(p, width);
          p = isl_printer_print_str(p, ",false");
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

static __isl_give isl_printer *declare_and_allocate_cpu_arrays_catapult(
  __isl_take isl_printer *p, struct autosa_prog *prog, 
  struct autosa_kernel *kernel, struct autosa_hw_top_module *top)
{
  p = print_str_new_line(p, "// Allocate memory in host memory");
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
    {
      /* Create multiple host buffers. */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::vector<");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *> ");
      p = isl_printer_print_str(p, "dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize) {
        p = isl_printer_print_str(p, "_unserialized");
      }
      p = isl_printer_print_str(p, ";");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, local_array->n_mem_ports);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      p = isl_printer_print_str(p, "_tmp");
      p = isl_printer_print_str(p, " = (");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *)malloc(");
      p = autosa_array_info_print_data_size(p, local_array->array);      
      p = isl_printer_print_str(p, " * sizeof(");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, "));");
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
        p = isl_printer_print_str(p, "std::vector<");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, " *> ");
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
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, " *dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "_tmp");
        p = isl_printer_print_str(p, " = (");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, " *)malloc(");
        //p = autosa_array_info_print_data_size(p, local_array->array);
        p = isl_printer_print_pw_qpolynomial(p, local_array->serialize_bound);
        if (local_array->is_sparse) {
          p = isl_printer_print_str(p, " / ");
          p = isl_printer_print_double(p, (double)local_array->eff_compress_ratio);
        }
        p = isl_printer_print_str(p, " * sizeof(");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, "));");
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
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, " = (");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, " *)malloc(");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, " * sizeof(");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, "));");
      p = isl_printer_end_line(p);

      if (local_array->host_serialize) {
        /* Create a single host buffer. */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, " *dev_");
        p = isl_printer_print_str(p, local_array->array->name);       
        p = isl_printer_print_str(p, " = (");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, " *)malloc(");        
        p = isl_printer_print_pw_qpolynomial(p, local_array->serialize_bound);
        if (local_array->is_sparse) {
          p = isl_printer_print_str(p, " / ");
          p = isl_printer_print_double(p, (double)local_array->eff_compress_ratio);
        }
        p = isl_printer_print_str(p, " * sizeof(");
        p = isl_printer_print_str(p, local_array->array->type);
        p = isl_printer_print_str(p, "));");
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

    if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, local_array->n_mem_ports);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "memcpy(dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, "[i]");      
      p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, ", ");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, " * sizeof(");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, "));");
      p = isl_printer_end_line(p);

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");
    }
    else
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "memcpy(dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->host_serialize)
        p = isl_printer_print_str(p, "_unserialized");
      p = isl_printer_print_str(p, ", ");
      p = isl_printer_print_str(p, local_array->array->name);
      if (local_array->is_sparse)
        p = isl_printer_print_str(p, "_s");
      p = isl_printer_print_str(p, ", ");
      p = autosa_array_info_print_data_size(p, local_array->array);
      p = isl_printer_print_str(p, " * sizeof(");
      p = isl_printer_print_str(p, local_array->array->type);
      p = isl_printer_print_str(p, "));");
      p = isl_printer_end_line(p);
    }
  }
  
  /* Perform data serialization if needed. */
  for (int i = 0; i < top->n_hw_modules; i++) {
    struct autosa_hw_module *module = top->hw_modules[i];
    if (module->serialize_tree && module->in) {
      struct autosa_array_ref_group *group = module->io_groups[0];
      struct autosa_local_array_info *local_array = group->local_array;
      if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
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

  p = print_str_new_line(p, "// Allocate buffers in device memory");
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "std::vector<");
    p = autosa_print_array_type(p, local_array->array);
    p = isl_printer_print_str(p, " *> buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
  }

  for (int i = 0; i < kernel->n_array; i++)
  {
    int indent1, indent2;
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int i = 0; i < ");
    p = isl_printer_print_int(p, local_array->n_mem_ports);
    p = isl_printer_print_str(p, "; i++) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);

    p = isl_printer_start_line(p);
    p = autosa_print_array_type(p, local_array->array);
    p = isl_printer_print_str(p, " *buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_tmp = (");
    p = autosa_print_array_type(p, local_array->array);
    p = isl_printer_print_str(p, " *)malloc(");
    if (local_array->host_serialize) {
      p = autosa_array_info_print_serialize_data_size(p, local_array->array);
    } else {
      p = autosa_array_info_print_data_size(p, local_array->array);
    }
    p = isl_printer_print_str(p, " / ");
    p = isl_printer_print_int(p, local_array->array->n_lane);
    p = isl_printer_print_str(p, " * sizeof(");
    p = autosa_print_array_type(p, local_array->array);
    p = isl_printer_print_str(p, "));");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, ".push_back(buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "_tmp);");
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
static __isl_give isl_printer *init_device_catapult(__isl_take isl_printer *p,
                                                    struct autosa_prog *prog, 
                                                    struct autosa_kernel *kernel, 
                                                    int hls,
                                                    struct autosa_hw_top_module *top)
{
  p = autosa_print_local_declarations(p, prog);
  //if (!hls)
  //{
  //  p = find_device_catapult(p);
  //  p = declare_and_allocate_device_arrays_catapult(p, prog, kernel, top);
  //}
  //else
  //{
  p = declare_and_allocate_cpu_arrays_catapult(p, prog, kernel, top);
  //}

  return p;
}

static __isl_give isl_printer *autosa_free_cpu_arrays_catapult(
  __isl_take isl_printer *p, struct autosa_prog *prog, struct autosa_kernel *kernel)
{
  p = print_str_new_line(p, "// Clean up resources");
  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int i = 0; i < ");
    p = isl_printer_print_int(p, local_array->n_mem_ports);
    p = isl_printer_print_str(p, "; i++) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "free(buffer_");
    p = isl_printer_print_str(p, local_array->array->name);
    p = isl_printer_print_str(p, "[i]);");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
  }

  for (int i = 0; i < kernel->n_array; i++)
  {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (!autosa_array_requires_device_allocation(local_array->array))
      continue;

    if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "for (int i = 0; i < ");
      p = isl_printer_print_int(p, local_array->n_mem_ports);
      p = isl_printer_print_str(p, "; i++) {");
      p = isl_printer_end_line(p);
      p = isl_printer_indent(p, 2);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "free(dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      p = isl_printer_print_str(p, "[i]);");
      p = isl_printer_end_line(p);

      if (local_array->host_serialize) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "free(dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "_unserialized");
        p = isl_printer_print_str(p, "[i]);");
        p = isl_printer_end_line(p);
      }

      p = isl_printer_indent(p, -2);
      p = print_str_new_line(p, "}");
    }
    else
    {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "free(dev_");
      p = isl_printer_print_str(p, local_array->array->name);
      p = isl_printer_print_str(p, ");");
      p = isl_printer_end_line(p);

      if (local_array->host_serialize) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "free(dev_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "_unserialized");
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
  }

  return p;
}

/* Print code for clearing the device after execution of the transformed code.
 * In particular, free the memory that was allocated on the device.
 */
static __isl_give isl_printer *clear_device_catapult(__isl_take isl_printer *p,
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

  if (hls)
  {
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
        p = isl_printer_print_str(p, "memcpy(");
        p = isl_printer_print_str(p, array->name);
        p = isl_printer_print_str(p, ", dev_");
        p = isl_printer_print_str(p, array->name);
        if (array->local_array->host_serialize) {
          p = isl_printer_print_str(p, "_unserialized");
        }
        if (array->local_array->n_mem_ports > 1)
        {
          p = isl_printer_print_str(p, "[0]");
        }
        p = isl_printer_print_str(p, ", ");
        p = autosa_array_info_print_size(p, array);
        p = isl_printer_print_str(p, ");");
        p = isl_printer_end_line(p);
      }
    }
    p = isl_printer_end_line(p);
    p = autosa_free_cpu_arrays_catapult(p, prog, kernel);
  }
  //else
  //{
  //  /* Restore buffer */
  //  p = print_str_new_line(p, "// Restore data from host buffers");
  //  for (int i = 0; i < prog->n_array; i++)
  //  {
  //    struct autosa_array_info *array = &prog->array[i];
  //    if (!autosa_array_requires_device_allocation(array))
  //      continue;
//
  //    if (array->copy_out)
  //    {
  //      p = isl_printer_start_line(p);
  //      p = isl_printer_print_str(p, "std::copy(dev_");
  //      p = isl_printer_print_str(p, array->name);
  //      if (array->local_array->host_serialize) {
  //        p = isl_printer_print_str(p, "_unserialized");
  //      }
  //      if (array->local_array->n_mem_ports > 1)
  //      {
  //        p = isl_printer_print_str(p, "[0]");
  //      }
  //      p = isl_printer_print_str(p, ".begin(), dev_");
  //      p = isl_printer_print_str(p, array->name);
  //      if (array->local_array->host_serialize) {
  //        p = isl_printer_print_str(p, "_unserialized");
  //      }
  //      if (array->local_array->n_mem_ports > 1)
  //      {
  //        p = isl_printer_print_str(p, "[0]");
  //      }
  //      p = isl_printer_print_str(p, ".end(), reinterpret_cast<");
  //      p = isl_printer_print_str(p, array->type);
  //      p = isl_printer_print_str(p, " *>(");
  //      p = isl_printer_print_str(p, array->name);
  //      p = isl_printer_print_str(p, "));");
  //      p = isl_printer_end_line(p);
  //    }
  //  }
  //}

  return p;
}

static __isl_give isl_printer *drain_merge_catapult(
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

/* Print code to "p" for copying "array" from the host to the device
 * in its entirety.  The bounds on the extent of "array" have
 * been precomputed in extract_array_info and are used in
 * gpu_array_info_print_size.
 */
static __isl_give isl_printer *copy_array_to_device_catapult(
  __isl_take isl_printer *p,
  struct autosa_array_info *array, int hls)
{
  int indent;

  struct autosa_local_array_info *local_array = array->local_array;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "for (int i = 0; i < ");
  p = isl_printer_print_int(p, local_array->n_mem_ports);
  p = isl_printer_print_str(p, "; i++) {");
  p = isl_printer_end_line(p);
  p = isl_printer_indent(p, 2);

  //p = isl_printer_start_line(p);
  //p = isl_printer_print_str(p, "memcpy(buffer_");
  //p = isl_printer_print_str(p, array->name);
  //p = isl_printer_print_str(p, "[i], dev_");
  //p = isl_printer_print_str(p, array->name);
  //if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
  //{
  //  p = isl_printer_print_str(p, "[i]");
  //}
  //p = isl_printer_print_str(p, ", ");
  //if (local_array->host_serialize) {
  //  p = autosa_array_info_print_serialize_size(p, array);
  //} else {
  //  p = autosa_array_info_print_size(p, array);
  //}
  //p = isl_printer_print_str(p, ");");
  //p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "for (int c0 = 0; c0 < ");
  if (local_array->host_serialize) {
    p = autosa_array_info_print_serialize_data_size(p, array);
  } else {
    p = autosa_array_info_print_data_size(p, array);
  }
  p = isl_printer_print_str(p, " / ");
  p = isl_printer_print_int(p, array->n_lane);
  p = isl_printer_print_str(p, "; c0++) {");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, 2);
  p = isl_printer_start_line(p);
  p = autosa_print_array_type(p, array);
  p = isl_printer_print_str(p, " tmp;");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "for (int c1 = 0; c1 < ");
  p = isl_printer_print_int(p, array->n_lane);
  p = isl_printer_print_str(p, "; c1++) {");
  p = isl_printer_end_line(p);
  
  p = isl_printer_indent(p, 2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "tmp.set_slc(c1 * ");
  p = isl_printer_print_int(p, array->size * 8);
  p = isl_printer_print_str(p, ", (");
  p = isl_printer_print_str(p, array->name);
  p = isl_printer_print_str(p, "_t1)dev_");
  p = isl_printer_print_str(p, array->name);
  if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
  {
    p = isl_printer_print_str(p, "[i]");
  }
  p = isl_printer_print_str(p, "[c0 * ");
  p = isl_printer_print_int(p, array->n_lane);
  p = isl_printer_print_str(p, " + c1]);");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "buffer_");
  p = isl_printer_print_str(p, array->name);
  p = isl_printer_print_str(p, "[i][c0] = tmp;");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");

  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "}");
  p = isl_printer_end_line(p);  

  return p;
}

/* Print code to "p" for copying "array" back from the device to the host
 * in its entirety.  The bounds on the extent of "array" have
 * been precomputed in extract_array_info and are used in
 * polysa_array_info_print_size.
 */
static __isl_give isl_printer *copy_array_from_device_catapult(
  __isl_take isl_printer *p, struct autosa_array_info *array, int hls)
{
  struct autosa_local_array_info *local_array;
  int indent;

  local_array = array->local_array;
  //if (!hls)
  //{
  //  p = isl_printer_start_line(p);
  //  p = isl_printer_print_str(p, "for (int i = 0; i < ");
  //  p = isl_printer_print_int(p, local_array->n_io_group_refs);
  //  p = isl_printer_print_str(p, "; i++) {");
  //  p = isl_printer_end_line(p);
  //  p = isl_printer_indent(p, 2);
//
  //  p = print_str_new_line(p, "OCL_CHECK(err,");
  //  indent = strlen("OCL_CHECK(");
  //  p = isl_printer_indent(p, indent);
  //  p = isl_printer_start_line(p);
  //  p = isl_printer_print_str(p, "err = q.enqueueMigrateMemObjects({buffer_");
  //  p = isl_printer_print_str(p, array->name);
  //  p = isl_printer_print_str(p, "[i]");
  //  p = isl_printer_print_str(p, "}, CL_MIGRATE_MEM_OBJECT_HOST));");
  //  p = isl_printer_end_line(p);
  //  p = isl_printer_indent(p, -indent);
//
  //  p = isl_printer_indent(p, -2);
  //  p = print_str_new_line(p, "}");
  //}
  //else
  {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int i = 0; i < ");
    p = isl_printer_print_int(p, local_array->n_mem_ports);
    p = isl_printer_print_str(p, "; i++) {");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);

    //p = isl_printer_start_line(p);
    //p = isl_printer_print_str(p, "memcpy(dev_");
    //p = isl_printer_print_str(p, array->name);
    //if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
    //{
    //  p = isl_printer_print_str(p, "[i]");
    //}
    //p = isl_printer_print_str(p, ", buffer_");
    //p = isl_printer_print_str(p, array->name);
    //p = isl_printer_print_str(p, "[i], ");
    //if (local_array->host_serialize) {
    //  p = autosa_array_info_print_serialize_size(p, array);
    //} else {
    //  p = autosa_array_info_print_size(p, array);
    //}
    //p = isl_printer_print_str(p, ");");
    //p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int c0 = 0; c0 < ");
    if (local_array->host_serialize) {
      p = autosa_array_info_print_serialize_data_size(p, array);
    } else {
      p = autosa_array_info_print_data_size(p, array);
    }
    p = isl_printer_print_str(p, " / ");
    p = isl_printer_print_int(p, array->n_lane);
    p = isl_printer_print_str(p, "; c0++) {");
    p = isl_printer_end_line(p);   

    p = isl_printer_indent(p, 2);
    p = isl_printer_start_line(p);
    p = autosa_print_array_type(p, array);
    p = isl_printer_print_str(p, " tmp = buffer_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "[i][c0];");
    p = isl_printer_end_line(p);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (int c1 = 0; c1 < ");
    p = isl_printer_print_int(p, array->n_lane);
    p = isl_printer_print_str(p, "; c1++) {");
    p = isl_printer_end_line(p); 

    p = isl_printer_indent(p, 2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, array->name);
    if (local_array->n_mem_ports > 1 && local_array->array->copy_out)
    {
      p = isl_printer_print_str(p, "[i]");
    }
    p = isl_printer_print_str(p, "[c0 * ");
    p = isl_printer_print_int(p, array->n_lane);
    p = isl_printer_print_str(p, " + c1] = (");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, ")tmp.slc<");
    p = isl_printer_print_int(p, array->size * 8);
    p = isl_printer_print_str(p, ">(");
    p = isl_printer_print_int(p, array->size * 8);
    p = isl_printer_print_str(p, " * c1);");
    p = isl_printer_end_line(p); 

    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");    

    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");    

    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "}");
    p = isl_printer_end_line(p);    
  }

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
static __isl_give isl_printer *print_device_node_catapult(__isl_take isl_printer *p,
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
    return init_device_catapult(p, prog, kernel, hls, top);
  if (!strcmp(name, "clear_device"))
    return clear_device_catapult(p, prog, kernel, hls, top);
  if (!strcmp(name, "drain_merge"))
    return drain_merge_catapult(p, prog, func, hls);
  if (!array)
    return isl_printer_free(p);

  if (!prefixcmp(name, "to_device"))
    return copy_array_to_device_catapult(p, array, hls);
  else
    return copy_array_from_device_catapult(p, array, hls);

  return p;
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
static __isl_give isl_printer *print_host_user_catapult(__isl_take isl_printer *p,
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
    return print_device_node_catapult(p, node, data->prog, hls->hls, top);
  }

  is_user = !strcmp(isl_id_get_name(id), "user");
  kernel = is_user ? NULL : (struct autosa_kernel *)isl_id_get_user(id);
  stmt = is_user ? (struct autosa_kernel_stmt *)isl_id_get_user(id) : NULL;
  isl_id_free(id);

  if (is_user)
    return autosa_kernel_print_domain(p, stmt);

  //if (!hls->hls)
  //{
  //  /* Print OpenCL host. */
  //  p = ppcg_start_block(p);
//
  //  p = print_set_kernel_arguments_xilinx(p, data->prog, kernel);
  //  p = print_str_new_line(p, "q.finish();");
  //  p = print_str_new_line(p, "fpga_begin = std::chrono::high_resolution_clock::now();");
  //  p = isl_printer_end_line(p);
  //  p = print_str_new_line(p, "// Launch the kernel");
  //  p = print_str_new_line(p, "OCL_CHECK(err, err = q.enqueueTask(krnl));");
  //  p = isl_printer_end_line(p);
  //  p = print_str_new_line(p, "q.finish();");
  //  p = print_str_new_line(p, "fpga_end = std::chrono::high_resolution_clock::now();");
//
  //  p = ppcg_end_block(p);
  //  p = isl_printer_end_line(p);
  //}
  //else
  //{
    /* Print HLS host. */
    p = ppcg_start_block(p);

    p = print_str_new_line(p, "// Launch the kernel");
    p = print_str_new_line(p, "kernel0 kernel0_inst;");

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "kernel");    
    p = isl_printer_print_int(p, 0);
    p = isl_printer_print_str(p, "_inst.run(");
    p = print_kernel_arguments(p, data->prog, kernel, 0, hls);
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);

    p = ppcg_end_block(p);
  //}
  /* Print the top kernel header. */
  //print_kernel_headers_catapult(data->prog, kernel, data->hls);

  return p;
}

static __isl_give isl_printer *print_module_core_header_catapult(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_hw_module *module,
  int inter, int boundary, int serialize, int types)
{
  int n = isl_id_list_n_id(module->inst_ids);

  p = isl_printer_start_line(p);  
  if (types)
    p = isl_printer_print_str(p, "void ");
  p = isl_printer_print_str(p, "CCS_BLOCK(run)");
  p = isl_printer_print_str(p, "(");
  if (!types) {
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, 2);
    p = isl_printer_start_line(p);  
  }
  p = print_module_arguments(p, prog, module->kernel, module, types,
                             CATAPULT_HW, inter, -1, boundary, serialize);
  p = isl_printer_print_str(p, ")");
  if (!types) {
    p = isl_printer_indent(p, -2);
  }

  return p;
}

/* Print out variable declarations on Xilinx platforms.
 * The local variable can be mapped to different memory resources:
 * FF, LUTRAM, BRAM, URAM.
 */
static __isl_give isl_printer *print_module_var_catapult(
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
    //if (var->n_lane == 1)
    //  p = isl_printer_print_str(p, var->array->type);
    //else {
      p = isl_printer_print_str(p, var->array->name);    
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, var->n_lane);
    //}
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
  }

  return p;
}

static __isl_give isl_printer *print_module_vars_catapult(
  __isl_take isl_printer *p, struct autosa_hw_module *module, int inter)
{
  int i, n;
  isl_space *space;
  const char *type;

  if (inter == -1)
  {
    for (i = 0; i < module->n_var; ++i)
      p = print_module_var_catapult(p, &module->var[i], module->double_buffer, module);
  }  

  return p;
}

static __isl_give isl_printer *print_for_with_pipeline(
  __isl_keep isl_ast_node *node, __isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#pragma hls_pipeline_init_interval 1");
  p = isl_printer_end_line(p);

  p = isl_ast_node_for_print(node, p, print_options);

  return p;
}

static __isl_give isl_printer *print_for_with_unroll(
  __isl_keep isl_ast_node *node, __isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "#pragma unroll yes");
  p = isl_printer_end_line(p);

  p = isl_ast_node_for_print(node, p, print_options);

  return p;
}

static __isl_give isl_printer *print_for_with_guard(
  __isl_take isl_ast_node *node, __isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options,
  int pipeline, int unroll,
  int guard_start, int guard_end,
  char **fifo_names, isl_pw_qpolynomial **bounds, int n_fifo,
  int double_buffer, int inter, int read,
  char *module_name, char *buf_name
  )
{  
  if (guard_start) {
    p = isl_printer_print_str(p, "#ifndef __SYNTHESIS__");
    p = isl_printer_end_line(p);    

    p = print_str_new_line(p, "// while () // Please add the fifo check for C sim.");
    //if (n_fifo > 0) {
    //  p = isl_printer_start_line(p);
    //  p = isl_printer_print_str(p, "while (");
    //  //for (int i = 0; i < n_fifo; i++) {
    //  //  if (i > 0)
    //  //    p = isl_printer_print_str(p, " && ");
    //  //  p = isl_printer_print_str(p, fifo_names[i]);
    //  //  p = isl_printer_print_str(p, ".available(");
    //  //  p = isl_printer_print_pw_qpolynomial(p, bounds[i]);
    //  //  p = isl_printer_print_str(p, ")");
    //  //}
    //  p = isl_printer_print_str(p, ")");
    //  p = isl_printer_end_line(p);
    //}
  }

  //p = isl_printer_indent(p, 2);
  if (pipeline) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma hls_pipeline_init_interval 1");
    p = isl_printer_end_line(p);
  }
  if (unroll) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma unroll yes");
    p = isl_printer_end_line(p);
  }

  if (!guard_end) {
    p = isl_ast_node_for_print(node, p, print_options);   
    //p = isl_printer_indent(p, -2); 
  } else {
    isl_ast_expr *iterator, *init, *cond, *inc;
    isl_ast_node *body;
    const char *iter_type;
    iterator = isl_ast_node_for_get_iterator(node);
    init = isl_ast_node_for_get_init(node);
    cond = isl_ast_node_for_get_cond(node);
    inc = isl_ast_node_for_get_inc(node);
    body = isl_ast_node_for_get_body(node);
    iter_type = isl_options_get_ast_iterator_type(isl_ast_node_get_ctx(node));
    
    //p = isl_printer_indent(p, -2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "for (");
    p = isl_printer_print_str(p, iter_type);
    p = isl_printer_print_str(p, " ");
    p = isl_printer_print_ast_expr(p, iterator);
    p = isl_printer_print_str(p, " = ");
    p = isl_printer_print_ast_expr(p, init);
    p = isl_printer_print_str(p, "; ");
    p = isl_printer_print_ast_expr(p, cond);
    p = isl_printer_print_str(p, "; ");
    p = isl_printer_print_ast_expr(p, iterator);
    p = isl_printer_print_str(p, " += ");
    p = isl_printer_print_ast_expr(p, inc);
    p = isl_printer_print_str(p, ")");
    p = isl_printer_end_line(p);

    p = isl_printer_print_str(p, "#endif");
    p = isl_printer_end_line(p);

    p = ppcg_start_block(p);

    /* Add the double buffer logic if needed. */    
    if (inter == 0 || inter == 1) {      
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, module_name);
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, buf_name);
      p = isl_printer_print_str(p, " ");
      p = isl_printer_print_str(p, buf_name);
      p = isl_printer_print_str(p, "_tmp;");
      p = isl_printer_end_line(p);

      if (read) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, buf_name);
        p = isl_printer_print_str(p, "_tmp = ");
        p = isl_printer_print_str(p, buf_name);
        p = isl_printer_print_str(p, ".read();");
        p = isl_printer_end_line(p);      
      }
    }    

    //p = isl_printer_indent(p, 2);  
    p = isl_ast_node_print(body, p, print_options);    
    //p = isl_printer_indent(p, -2);  
        
    if (inter == 0 || inter == 1) {      
      if (!read) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, buf_name);
        p = isl_printer_print_str(p, ".write(");
        p = isl_printer_print_str(p, buf_name);
        p = isl_printer_print_str(p, "_tmp);");
        p = isl_printer_end_line(p);      
      }
    }

    p = ppcg_end_block(p);

    isl_ast_expr_free(iterator);
    isl_ast_expr_free(init);
    isl_ast_expr_free(cond);
    isl_ast_expr_free(inc);
    isl_ast_node_free(body);
  }

  return p;
}

static __isl_give isl_printer *print_for_catapult(__isl_take isl_printer *p,
                                                  __isl_take isl_ast_print_options *print_options,
                                                  __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  int pipeline;
  int unroll;
  int guard_start;
  int guard_end;
  /* for catapult fifos */
  int n_fifo;
  char **fifo_names;
  isl_pw_qpolynomial **bounds;
  int double_buffer, inter, read;
  char *module_name, *buf_name;

  pipeline = 0;
  unroll = 0;
  guard_start = 0;
  guard_end = 0;
  id = isl_ast_node_get_annotation(node);
  n_fifo = 0;
  fifo_names = NULL;
  bounds = NULL;
  double_buffer = 0;
  inter = -1;
  read = -1;
  module_name = NULL;
  buf_name = NULL;

  if (id)
  {
    struct autosa_ast_node_userinfo *info;

    info = (struct autosa_ast_node_userinfo *)isl_id_get_user(id);
    if (info && info->is_pipeline)
      pipeline = 1;
    if (info && info->is_unroll)
      unroll = 1;
    if (info && info->is_guard_start)
      guard_start = 1;
    if (info && info->is_guard_end) {
      guard_end = 1;
      if (info->inter >= 0) {
        double_buffer = info->double_buffer;
        inter = info->inter;
        read = info->read;
        module_name = info->module_name;
        buf_name = info->buf_name;
      }
    }
  }

  if (guard_start || guard_end)
    p = print_for_with_guard(
            node, p, print_options, pipeline, unroll, 
            guard_start, guard_end,
            fifo_names, bounds, n_fifo,
            double_buffer, inter, read, module_name, buf_name);
  else if (pipeline)
    p = print_for_with_pipeline(node, p, print_options);
  else if (unroll)
    p = print_for_with_unroll(node, p, print_options);
  else
    p = isl_ast_node_for_print(node, p, print_options);

  isl_id_free(id);

  return p;
}

/* Prints out the rest of the fields in the class for Catapult HLS. 
 * If the function holds the inter and intra trans modules, prints out 
 * a private filed containing the function decls.
 * 
 */
static __isl_give isl_printer *print_module_fields_catapult(
  __isl_take isl_printer *p, struct autosa_prog *prog,
  struct autosa_hw_module *module, struct hls_info *hls,
  int inter, int boundary, int serialize, int types) 
{
  p = print_str_new_line(p, "}");

  // TODO: More to be printed out for other functions
  if (inter == -1 && module->is_filter && module->is_buffer) {
    /* Print the inter/intra trans modules and the buffer. */
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, -2);
    p = print_str_new_line(p, "private:");
    p = isl_printer_indent(p, 2);
    /* inter trans module */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->name);
    p = isl_printer_print_str(p, "_inter_trans");    
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");    
    p = isl_printer_print_str(p, " ");
    p = isl_printer_print_str(p, module->name);
    p = isl_printer_print_str(p, "_inter_trans");
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");    
    p = isl_printer_print_str(p, "_inst;");
    p = isl_printer_end_line(p);

    /* intra trans module */
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->name);
    p = isl_printer_print_str(p, "_intra_trans ");
    p = isl_printer_print_str(p, module->name);
    p = isl_printer_print_str(p, "_intra_trans_inst;");
    p = isl_printer_end_line(p);    

    /* buffer */
    for (int i = 0; i < module->n_var; i++) {
      struct autosa_kernel_var *var;
      var = (struct autosa_kernel_var *)&module->var[i];
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "ac_channel<");
      p = isl_printer_print_str(p, module->name);      
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, var->name);
      p = isl_printer_print_str(p, "> ");
      p = isl_printer_print_str(p, module->name);      
      //if (boundary)
      //  p = isl_printer_print_str(p, "_boundary");    
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, var->name);
      p = isl_printer_print_str(p, "_inst;");
      p = isl_printer_end_line(p);
    }    
  } 

  p = isl_printer_indent(p, -2);
  p = isl_printer_indent(p, -2);
  p = print_str_new_line(p, "};");  

  return p;
}

static __isl_give isl_printer *print_module_core_headers_catapult(
  __isl_take isl_printer *p, struct autosa_prog *prog, 
  struct autosa_hw_module *module, struct hls_info *hls,
  int inter, int boundary, int serialize, int types)
{
  int n = isl_id_list_n_id(module->inst_ids);  

  if (types) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "class ");
    p = isl_printer_print_str(p, module->name);
    if (inter == 0)
      p = isl_printer_print_str(p, "_intra_trans");
    if (inter == 1)
      p = isl_printer_print_str(p, "_inter_trans");
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");
    if (serialize)
      p = isl_printer_print_str(p, "_serialize");
    p = isl_printer_print_str(p, " {");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, 2);
    p = print_str_new_line(p, "public:");

    p = isl_printer_indent(p, 2);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->name);
    if (inter == 0)
      p = isl_printer_print_str(p, "_intra_trans");
    if (inter == 1)
      p = isl_printer_print_str(p, "_inter_trans");
    if (boundary)
      p = isl_printer_print_str(p, "_boundary");
    if (serialize)
      p = isl_printer_print_str(p, "_serialize");
    p = isl_printer_print_str(p, "() {}");
    p = isl_printer_end_line(p);

    p = print_str_new_line(p, "#pragma hls_design interface");
    if ((inter == -1 && module->pipeline_at_default_func && !serialize && !module->is_filter) ||
        (inter == -1 && module->pipeline_at_filter_func[0] && module->is_filter) ||
        (inter == 0 && module->pipeline_at_filter_func[1]) ||
        (inter == 1 && module->pipeline_at_filter_func[2]))
      p = print_str_new_line(p, "#pragma hls_pipeline_init_interval 1");
    p = print_module_core_header_catapult(p, prog, module, inter, boundary, serialize, 1);
    p = isl_printer_print_str(p, " {");
    p = isl_printer_end_line(p);
  } else {
    // TODO
  }

  return p;
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

  if (hls->target == CATAPULT_HW)
    p = print_module_core_headers_catapult(p, prog, module, hls, -1, boundary, 1, 1); // TODO  
  
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);    
  }
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  p = isl_printer_print_str(p, "#ifndef __SYNTHESIS__");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "// while () // Please add the fifo check for C sim.");
  p = isl_printer_print_str(p, "#endif");
  p = isl_printer_end_line(p);
  
  p = print_module_serialize_body(p, module, hls);
  p = isl_printer_indent(p, -2);  
  if (hls->target == CATAPULT_HW)
    p = print_module_fields_catapult(p, prog, module, hls, -1, boundary, 1, 1);

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

  //bool wrapper = 0;
  struct print_hw_module_data hw_data = {hls, prog, module, NULL};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);
  
  ///* Print wrapper for PE and L1 IO module */
  //if (module->type == PE_MODULE || (module->type != PE_MODULE && module->level == 1)) 
  //  wrapper = 1;  

  /* Print core. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  if (hls->target == CATAPULT_HW)
    p = print_module_core_headers_catapult(p, prog, module, hls, -1, boundary, 0, 1);  
  
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  //if (!prog->scop->options->autosa->use_cplusplus_template) {
  p = print_module_iterators(p, hls->kernel_c, module);  
  //}  
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
  if (module->type == PE_MODULE)
    p = print_module_vars_catapult(p, module, -1);
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);  

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);  
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_for_catapult, &hw_data);

  if (!boundary)
    p = isl_ast_node_print(module->device_tree, p, print_options);
  else
    p = isl_ast_node_print(module->boundary_tree, p, print_options);
  p = isl_printer_indent(p, -2);
  
  if (hls->target == CATAPULT_HW)
    p = print_module_fields_catapult(p, prog, module, hls, -1, boundary, 0, 1);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

  /* If the module serialization is enabled, we will print out an extra module
   * for serializing the data. */
  if (module->to_mem && module->options->autosa->host_serialize) {
    p = autosa_print_serialize_module(p, module, prog, hls, boundary);
  }

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
  
  p = print_module_core_headers_catapult(p, prog, module, hls, 1, boundary, 0, 1);
    
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }  
  p = print_module_vars_catapult(p, module, 1); 
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_module_stmt, &hw_data);  
  print_options = isl_ast_print_options_set_print_for(print_options,
                                                      &print_for_catapult, &hw_data);  
  
  p = isl_ast_node_print((boundary == 0) ? module->inter_tree : module->boundary_inter_tree, p, print_options);
  p = isl_printer_indent(p, -2);
  
  p = print_module_fields_catapult(p, prog, module, hls, 1, boundary, 0, 1);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

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

  p = print_module_core_headers_catapult(p, prog, module, hls, 0, boundary, 0, 1);
  
  p = isl_printer_indent(p, 2);
  p = print_str_new_line(p, "/* Variable Declaration */");
  if (!prog->scop->options->autosa->use_cplusplus_template) {
    p = print_module_iterators(p, hls->kernel_c, module);
  }
  p = print_module_vars_catapult(p, module, 1);
  p = print_str_new_line(p, "/* Variable Declaration */");
  p = isl_printer_end_line(p);

  //if (module->double_buffer)
  //{
  //  p = isl_printer_start_line(p);
  //  p = isl_printer_print_str(p, "if (!intra_trans_en) return;");
  //  p = isl_printer_end_line(p);
  //  p = isl_printer_end_line(p);
  //}
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
                                                        &print_for_catapult, &hw_data);  
    
  p = isl_ast_node_print(module->intra_tree, p, print_options);
  p = isl_printer_indent(p, -2);

  p = print_module_fields_catapult(p, prog, module, hls, 0, boundary, 0, 1);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  p = isl_printer_end_line(p);

  return p;  
}

static __isl_give isl_printer *print_local_array_struct(
  __isl_take isl_printer *p,
  struct autosa_hw_module *module,
  struct autosa_kernel_var *var)
{
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "struct ");
  p = isl_printer_print_str(p, module->name);
  p = isl_printer_print_str(p, "_");
  p = isl_printer_print_str(p, var->name);
  p = isl_printer_print_str(p, " {");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, 2);
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
  p = isl_printer_print_str(p, " data");
  for (int i = 0; i < isl_vec_size(var->size); i++) {
    isl_val *v;

    p = isl_printer_print_str(p, "[");
    v = isl_vec_get_element_val(var->size, i);
    p = isl_printer_print_val(p, v);
    isl_val_free(v);
    p = isl_printer_print_str(p, "]");    
  }
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);

  p = isl_printer_indent(p, -2);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "};");
  p = isl_printer_end_line(p);

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
  print_data_types_catapult(top, hls);

  /* Print the macros for sparse data structure */
  if (prog->scop->options->autosa->block_sparse) {
    print_sparse_macros(top->kernel, hls);
  }

  /* Print the helper functions in the program. */
  print_drain_merge_funcs(top->kernel, drain_merge_funcs, n_drain_merge_funcs, hls);

  /* Print the host data serialization function. */
  print_host_serialize_funcs(top->kernel, modules, n_modules, hls);

  /* Print the default AST. */
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                                                       &print_host_user_catapult, &data);

  /* Print the macros definitions in the program. */
  p = autosa_print_macros(p, tree);
  p = isl_ast_node_print(tree, p, print_options);

  /* Print the hw module ASTs. */
  p_module = isl_printer_to_file(ctx, hls->kernel_c);
  p_module = isl_printer_set_output_format(p_module, ISL_FORMAT_C);

  /* Print the local buffer definition */
  p_module = isl_printer_end_line(p_module);
  for (int i = 0; i < n_modules; i++) {
    if (modules[i]->type == PE_MODULE)
      continue;
    if (modules[i]->n_var > 0) {
      for (int j = 0; j < modules[i]->n_var; j++)
        p_module = print_local_array_struct(p_module, modules[i], &modules[i]->var[j]);
        p_module = isl_printer_end_line(p_module);
    }
  }
  p_module = print_str_new_line(p_module, "#include <mc_scverify.h>");
  p_module = isl_printer_end_line(p_module);

  for (int i = 0; i < n_modules; i++)
  {
    if (modules[i]->is_filter && modules[i]->is_buffer)
    {
      /* Print out the definitions for inter_trans and intra_trans function calls. */
      /* Intra transfer function */
      p_module = autosa_print_intra_trans_module(p_module, modules[i], prog, hls, 0); // todo
 
      /* Inter transfer function */
      p_module = autosa_print_inter_trans_module(p_module, modules[i], prog, hls, 0); // todo
      if (modules[i]->boundary)
        p_module = autosa_print_inter_trans_module(p_module, modules[i], prog, hls, 1); // todo
    }

    p_module = autosa_print_default_module(p_module, modules[i], prog, hls, 0);
 
    if (modules[i]->boundary)
    {
      /* Print out the definitions for boundary trans function calls. */
      p_module = autosa_print_default_module(p_module, modules[i], prog, hls, 1);
    }      
  }
  isl_printer_free(p_module);

  return p;
}

static __isl_give isl_printer *print_top_module_headers_catapult(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_hw_top_module *top, struct hls_info *hls)
{
  struct autosa_kernel *kernel = top->kernel;

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"#pragma hls_design top\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");  
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"class kernel0 {\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_indent(p, 2);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");  
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"public:\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_indent(p, 2);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");  
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"kernel0() {}\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");  
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"#pragma hls_design interface\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"void CCS_BLOCK(run)(");
  p = print_kernel_arguments(p, prog, top->kernel, 1, hls); // todo
  p = isl_printer_print_str(p, ")\");");
  p = isl_printer_end_line(p);
  
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"{\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

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

static __isl_give isl_printer *print_top_module_call_inst(
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
      return autosa_kernel_print_module_call_inst(p, stmt, data->prog, data->hls->target);
  }

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

  if (hls->target == CATAPULT_HW)
    p = print_top_module_headers_catapult(p, prog, top, hls);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, 2);");
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

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, -2);");
  p = isl_printer_end_line(p);

  /* Print the private fields */
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"private:\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_indent(p, 2);");

  /* Print the function calls */
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"/* Module Declaration */\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  for (int i = 0; i < top->n_module_calls; i++) {
    print_options = isl_ast_print_options_alloc(ctx);
    print_options = isl_ast_print_options_set_print_user(print_options,
                                                         &print_top_module_call_inst, &hw_data);
    p = isl_ast_node_print(top->module_call_wrapped_trees[i],
                           p, print_options);
  }
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"/* Module Declaration */\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  /* Print the fifo decls */
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"/* FIFO Declaration */\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  
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
      p = print_fifo_type_catapult(p, group, module->data_pack_inter);
      p = isl_printer_print_str(p, " ");
      p = isl_printer_print_str(p, fifo_name);      
      p = isl_printer_print_str(p, ";\");");
      p = isl_printer_end_line(p);
      p = print_str_new_line(p, "p = isl_printer_end_line(p);");      

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

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");    
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"/* FIFO Declaration */\");");  
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  p = print_str_new_line(p, "p = isl_printer_indent(p, -2);");
  p = print_str_new_line(p, "p = isl_printer_indent(p, -2);");

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"};\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  //if (hls->target == XILINX_HW)
  //{
  //  if (!hls->hls)
  //  {
  //    p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  //    p = print_str_new_line(p, "p = isl_printer_print_str(p, \"}\");");
  //    p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  //  }
  //}

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

/* This function prints the tcl file for the catapult HLS project. */
static void print_tcl_code(
  struct autosa_prog *prog, 
  struct autosa_hw_module **modules,
  int n_modules,
  struct hls_info *hls)
{
  isl_ctx *ctx = prog->ctx;
  isl_printer *p;
  
  p = isl_printer_to_file(ctx, hls->tcl);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);

  p = print_str_new_line(p, "solution new -state initial");
  p = print_str_new_line(p, "solution options defaults");
  p = print_str_new_line(p, "solution options set /Input/CppStandard c++11");
  p = print_str_new_line(p, "solution options set /Output/GenerateCycleNetlist false");
  p = print_str_new_line(p, "solution options set /Flows/SCVerify/USE_CCS_BLOCK true");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "solution file add ./");
  p = isl_printer_print_str(p, hls->kernel_prefix);
  p = isl_printer_print_str(p, "_kernel.h -type CHEADER");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "solution file add ./");
  p = isl_printer_print_str(p, hls->kernel_prefix);
  p = isl_printer_print_str(p, "_kernel_hw.h -type CHEADER");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "solution file add ./");
  p = isl_printer_print_str(p, hls->kernel_prefix);
  p = isl_printer_print_str(p, ".h -type CHEADER");
  p = isl_printer_end_line(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "solution file add ./");
  p = isl_printer_print_str(p, hls->kernel_prefix);
  p = isl_printer_print_str(p, "_host.cpp -type C++");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "directive set -PIPELINE_RAMP_UP true");
  p = print_str_new_line(p, "directive set -PROTOTYPING_ENGINE oasys");
  p = print_str_new_line(p, "directive set -CLUSTER_TYPE combinational");
  p = print_str_new_line(p, "directive set -CLUSTER_FAST_MODE false");
  p = print_str_new_line(p, "directive set -CLUSTER_RTL_SYN false");
  p = print_str_new_line(p, "directive set -CLUSTER_OPT_CONSTANT_INPUTS true");
  p = print_str_new_line(p, "directive set -CLUSTER_ADDTREE_IN_COUNT_THRESHOLD 0");
  p = print_str_new_line(p, "directive set -CLUSTER_ADDTREE_IN_WIDTH_THRESHOLD 0");
  p = print_str_new_line(p, "directive set -ROM_THRESHOLD 64");
  p = print_str_new_line(p, "directive set -PROTOTYPE_ROM true");
  p = print_str_new_line(p, "directive set -CHARACTERIZE_ROM false");
  p = print_str_new_line(p, "directive set -OPT_CONST_MULTS use_library");
  p = print_str_new_line(p, "directive set -CLOCK_OVERHEAD 20.000000");
  p = print_str_new_line(p, "directive set -RESET_CLEARS_ALL_REGS use_library");
  p = print_str_new_line(p, "directive set -START_FLAG {}");
  p = print_str_new_line(p, "directive set -READY_FLAG {}");
  p = print_str_new_line(p, "directive set -DONE_FLAG {}");
  p = print_str_new_line(p, "directive set -TRANSACTION_DONE_SIGNAL true");
  p = print_str_new_line(p, "directive set -STALL_FLAG false");
  p = print_str_new_line(p, "directive set -IDLE_SIGNAL {}");
  p = print_str_new_line(p, "directive set -REGISTER_IDLE_SIGNAL false");
  p = print_str_new_line(p, "directive set -ARRAY_SIZE 1024");
  p = print_str_new_line(p, "directive set -CHAN_IO_PROTOCOL use_library");
  p = print_str_new_line(p, "directive set -IO_MODE super");
  p = print_str_new_line(p, "directive set -UNROLL no");
  p = print_str_new_line(p, "directive set -REALLOC true");
  p = print_str_new_line(p, "directive set -MUXPATH true");
  p = print_str_new_line(p, "directive set -TIMING_CHECKS true");
  p = print_str_new_line(p, "directive set -ASSIGN_OVERHEAD 0");
  p = print_str_new_line(p, "directive set -REGISTER_SHARING_LIMIT 0");
  p = print_str_new_line(p, "directive set -REGISTER_SHARING_MAX_WIDTH_DIFFERENCE 8");
  p = print_str_new_line(p, "directive set -SAFE_FSM false");
  p = print_str_new_line(p, "directive set -NO_X_ASSIGNMENTS true");
  p = print_str_new_line(p, "directive set -REG_MAX_FANOUT 0");
  p = print_str_new_line(p, "directive set -FSM_BINARY_ENCODING_THRESHOLD 64");
  p = print_str_new_line(p, "directive set -FSM_ENCODING none");
  p = print_str_new_line(p, "directive set -LOGIC_OPT false");
  p = print_str_new_line(p, "directive set -MEM_MAP_THRESHOLD 32");
  p = print_str_new_line(p, "directive set -REGISTER_THRESHOLD 256");
  p = print_str_new_line(p, "directive set -MERGEABLE true");
  p = print_str_new_line(p, "directive set -SPECULATE true");
  p = print_str_new_line(p, "directive set -DESIGN_GOAL area");

  p = print_str_new_line(p, "go new");
  p = print_str_new_line(p, "solution library add mgc_Xilinx-VIRTEX-uplus-2LV_beh -- -rtlsyntool Vivado -manufacturer Xilinx -family VIRTEX-uplus -speed -2LV -part xcvu11p-flga2577-2LV-e");
  p = print_str_new_line(p, "solution library add Xilinx_RAMS");
  p = print_str_new_line(p, "solution library add Xilinx_ROMS");
  p = print_str_new_line(p, "solution library add amba");
  p = print_str_new_line(p, "solution library add ccs_fpga_hic");
  p = print_str_new_line(p, "solution library add Xilinx_FIFO");

  p = print_str_new_line(p, "go libraries");
  p = print_str_new_line(p, "directive set -CLOCKS {clk {-CLOCK_PERIOD 5.0 -CLOCK_EDGE rising -CLOCK_UNCERTAINTY 0.0 -CLOCK_HIGH_TIME 2.5 -RESET_SYNC_NAME rst -RESET_ASYNC_NAME arst_n -RESET_KIND sync -RESET_SYNC_ACTIVE high -RESET_ASYNC_ACTIVE low -ENABLE_ACTIVE high}}");

  p = print_str_new_line(p, "go assembly");
  p = print_str_new_line(p, "directive set -FIFO_DEPTH 1");

  /* Set all modules with identifiers to direct input. */
  const char *dims[] = {"idx", "idy", "idz"};
  for (int i = 0; i < n_modules; i++) {
    int n = isl_id_list_n_id(modules[i]->inst_ids);
    if (modules[i]->is_filter && modules[i]->is_buffer) {
      /* Intra transfer function */      
      if (n > 0) {
        for (int j = 0; j < n; j++) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "directive set /kernel0/");
          p = isl_printer_print_str(p, modules[i]->name);
          p = isl_printer_print_str(p, "_intra_trans/");
          p = isl_printer_print_str(p, dims[j]);
          p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
          p = isl_printer_end_line(p);
        }
      }

      /* Inter transfer function */
      if (n > 0) {
        for (int j = 0; j < n; j++) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "directive set /kernel0/");
          p = isl_printer_print_str(p, modules[i]->name);
          p = isl_printer_print_str(p, "_inter_trans/");
          p = isl_printer_print_str(p, dims[j]);
          p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
          p = isl_printer_end_line(p);
        }
      }

      if (modules[i]->boundary) {
        if (n > 0) {
          for (int j = 0; j < n; j++) {
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "directive set /kernel0/");
            p = isl_printer_print_str(p, modules[i]->name);
            p = isl_printer_print_str(p, "_inter_trans_boundary/");
            p = isl_printer_print_str(p, dims[j]);
            p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
            p = isl_printer_end_line(p);
          }
        }
      }
    }

    /* Default module */
    if (n > 0) {
      for (int j = 0; j < n; j++) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "directive set /kernel0/");
        p = isl_printer_print_str(p, modules[i]->name);
        p = isl_printer_print_str(p, "/");
        p = isl_printer_print_str(p, dims[j]);
        p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
        p = isl_printer_end_line(p);
      }

      /* Serialize */
      if (modules[i]->to_mem && modules[i]->options->autosa->host_serialize) {
        for (int j = 0; j < n; j++) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "directive set /kernel0/");
          p = isl_printer_print_str(p, modules[i]->name);
          p = isl_printer_print_str(p, "_serialize/");
          p = isl_printer_print_str(p, dims[j]);
          p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
          p = isl_printer_end_line(p);
        }
      }

      if (modules[i]->boundary) {
        for (int j = 0; j < n; j++) {
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "directive set /kernel0/");
          p = isl_printer_print_str(p, modules[i]->name);
          p = isl_printer_print_str(p, "_boundary/");
          p = isl_printer_print_str(p, dims[j]);
          p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
          p = isl_printer_end_line(p);
        }

        /* Serialize */
        if (modules[i]->to_mem && modules[i]->options->autosa->host_serialize) {
          for (int j = 0; j < n; j++) {
            p = isl_printer_start_line(p);
            p = isl_printer_print_str(p, "directive set /kernel0/");
            p = isl_printer_print_str(p, modules[i]->name);
            p = isl_printer_print_str(p, "_boundary_serialize/");
            p = isl_printer_print_str(p, dims[j]);
            p = isl_printer_print_str(p, ":rsc -MAP_TO_MODULE {[DirectInput]}");
            p = isl_printer_end_line(p);
          }
        } 
      }
    }
  }

  /* Set local buffer properties. */
  for (int i = 0; i < n_modules; i++) {
    if (modules[i]->type == PE_MODULE)
      continue;
    for (int j = 0; j < modules[i]->n_var; j++) {
      struct autosa_kernel_var *var;
      var = (struct autosa_kernel_var *)&modules[i]->var[j];
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "directive set /kernel0/");
      p = isl_printer_print_str(p, modules[i]->name);
      p = isl_printer_print_str(p, "/");
      p = isl_printer_print_str(p, modules[i]->name);
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, var->name);
      p = isl_printer_print_str(p, "_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL");
      p = isl_printer_end_line(p);

      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "directive set /kernel0/");
      p = isl_printer_print_str(p, modules[i]->name);
      p = isl_printer_print_str(p, "/");
      p = isl_printer_print_str(p, modules[i]->name);
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, var->name);
      if (modules[i]->double_buffer)
        p = isl_printer_print_str(p, "_inst:cns -STAGE_REPLICATION 2");
      else
        p = isl_printer_print_str(p, "_inst:cns -STAGE_REPLICATION 1");
      p = isl_printer_end_line(p);

      /* word width */
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "directive set /kernel0/");
      p = isl_printer_print_str(p, modules[i]->name);
      p = isl_printer_print_str(p, "/");
      p = isl_printer_print_str(p, modules[i]->name);
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_str(p, var->name);
      p = isl_printer_print_str(p, "_inst -WORD_WIDTH ");
      p = isl_printer_print_int(p, var->array->size * 8 * var->n_lane);
      p = isl_printer_end_line(p);

      if (modules[i]->boundary) {
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "directive set /kernel0/");
        p = isl_printer_print_str(p, modules[i]->name);
        p = isl_printer_print_str(p, "_boundary");
        p = isl_printer_print_str(p, "/");
        p = isl_printer_print_str(p, modules[i]->name);
        //p = isl_printer_print_str(p, "_boundary_");
        p = isl_printer_print_str(p, "_");
        p = isl_printer_print_str(p, var->name);
        p = isl_printer_print_str(p, "_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL");
        p = isl_printer_end_line(p);

        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "directive set /kernel0/");
        p = isl_printer_print_str(p, modules[i]->name);
        p = isl_printer_print_str(p, "_boundary");
        p = isl_printer_print_str(p, "/");
        p = isl_printer_print_str(p, modules[i]->name);
        //p = isl_printer_print_str(p, "_boundary_");
        p = isl_printer_print_str(p, "_");
        p = isl_printer_print_str(p, var->name);
        if (modules[i]->double_buffer)
          p = isl_printer_print_str(p, "_inst:cns -STAGE_REPLICATION 2");
        else
          p = isl_printer_print_str(p, "_inst:cns -STAGE_REPLICATION 1");
        p = isl_printer_end_line(p);

        /* word width */
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "directive set /kernel0/");
        p = isl_printer_print_str(p, modules[i]->name);
        p = isl_printer_print_str(p, "_boundary");
        p = isl_printer_print_str(p, "/");
        p = isl_printer_print_str(p, modules[i]->name);
        //p = isl_printer_print_str(p, "_boundary_");
        p = isl_printer_print_str(p, "_");
        p = isl_printer_print_str(p, var->name);
        p = isl_printer_print_str(p, "_inst -WORD_WIDTH ");
        p = isl_printer_print_int(p, var->array->size * 8 * var->n_lane);
        p = isl_printer_end_line(p);
      }
    }
  }

  p = print_str_new_line(p, "go architect");
  p = print_str_new_line(p, "// Insert directives for dependence if necessary");
  p = print_str_new_line(p, "// Example: directive set /kernel0/PE/run/for:read_mem(local_C:rsc.@) -IGNORE_DEPENDENCY_FROM {for:write_mem(local_C:rsc.@) for:write_mem(local_C:rsc.@)}");
  
  p = print_str_new_line(p, "go allocate");
  p = print_str_new_line(p, "go extract");

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
  /* Print the separate TCL file. */
  print_tcl_code(prog, modules, n_modules, hls);

  return p;
}

/* Generate systolic arrays using Catapult HLS C.
 */
int generate_autosa_catapult_hls_c(isl_ctx *ctx, struct ppcg_options *options,
                                   const char *input)
{
  struct hls_info hls;
  int r;

  hls.target = CATAPULT_HW;  
  hls.hls = 1;
  hls.ctx = ctx;
  hls.output_dir = options->autosa->output_dir;
  hls.hcl = options->autosa->hcl;
  hls_open_files(&hls, input);

  r = generate_sa(ctx, input, hls.host_c, options, &print_hw, &hls);

  hls_close_files(&hls);

  return r;
}