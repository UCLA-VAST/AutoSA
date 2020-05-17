#include <isl/ctx.h>

#include "autosa_xilinx_hls_c.h"
#include "autosa_common.h"
#include "autosa_print.h"
#include "autosa_trans.h"
#include "autosa_codegen.h"
#include "autosa_utils.h"

struct print_host_user_data {
	struct hls_info *hls;
	struct autosa_prog *prog;
  struct autosa_hw_top_module *top;
};

struct print_hw_module_data {
  struct hls_info *hls;
  struct autosa_prog *prog;
  struct autosa_hw_module *module;
};

/* Print the includes for Xilinx OpenCL host.  
 */
static void print_xilinx_host_header(FILE *fp)
{
  fprintf(fp, "#include <iostream>\n");
  fprintf(fp, "#include <vector>\n");
  fprintf(fp, "#include <fstream>\n\n");

  fprintf(fp, "#define CL_HPP_CL_1_2_DEFAULT_BUILD\n");
  fprintf(fp, "#define CL_HPP_TARGET_OPENCL_VERSION 120\n");
  fprintf(fp, "#define CL_HPP_MINIMUM_OPENCL_VERSION 120\n");
  fprintf(fp, "#define CL_HPP_ENABLE_PROGRAM_CONSTRUCTION_FROM_ARRAY_COMPATIBILITY 1\n");
  fprintf(fp, "#define CL_USE_DEPRECATED_OPENCL_1_2_APIS\n\n");

  fprintf(fp, "#include <CL/cl2.hpp>\n");
  fprintf(fp, "#include <CL/cl_ext_xilinx.h>\n\n");
  
  fprintf(fp, "#define OCL_CHECK(error,call)                                       \\\n");
  fprintf(fp, "    call;                                                           \\\n");
  fprintf(fp, "    if (error != CL_SUCCESS) {                                      \\\n");
  fprintf(fp, "      printf(\"%%s:%%d Error calling \" #call \", error code is: %%d\\n\",  \\\n");
  fprintf(fp, "              __FILE__,__LINE__, error);                            \\\n");
  fprintf(fp, "      exit(EXIT_FAILURE);                                           \\\n");
  fprintf(fp, "    }\n\n");

  fprintf(fp, "std::string xclbin_file_name;\n\n");

  fprintf(fp, "template <typename T>\n");
  fprintf(fp, "struct aligned_allocator\n");
  fprintf(fp, "{\n");
  fprintf(fp, "  using value_type = T;\n");
  fprintf(fp, "  T* allocate(std::size_t num)\n");
  fprintf(fp, "  {\n");
  fprintf(fp, "    void* ptr = nullptr;\n");
  fprintf(fp, "    if (posix_memalign(&ptr,4096,num*sizeof(T)))\n");
  fprintf(fp, "      throw std::bad_alloc();\n");
  fprintf(fp, "    return reinterpret_cast<T*>(ptr);\n");
  fprintf(fp, "  }\n");
  fprintf(fp, "  void deallocate(T* p, std::size_t num)\n");
  fprintf(fp, "  {\n");
  fprintf(fp, "    free(p);\n");
  fprintf(fp, "  }\n");
  fprintf(fp, "};\n\n");

  fprintf(fp, "cl::Program::Binaries import_binary_file()\n");
  fprintf(fp, "{\n");
  fprintf(fp, "    std::cout << \"\\n Loading: \"<< xclbin_file_name.c_str() << \"\\n\";\n");
  fprintf(fp, "    std::ifstream bin_file(xclbin_file_name.c_str(), std::ifstream::binary);\n");
  fprintf(fp, "    bin_file.seekg (0, bin_file.end);\n");
  fprintf(fp, "    unsigned nb = bin_file.tellg();\n");
  fprintf(fp, "    bin_file.seekg (0, bin_file.beg);\n");
  fprintf(fp, "    char *buf = new char [nb];\n");
  fprintf(fp, "    bin_file.read(buf, nb);\n");
  fprintf(fp, "\n");
  fprintf(fp, "    cl::Program::Binaries bins;\n");
  fprintf(fp, "    bins.push_back({buf,nb});\n");
  fprintf(fp, "    return bins;\n");
  fprintf(fp, "}\n\n");

  fprintf(fp, "std::vector<cl::Device> get_devices() {\n");
  fprintf(fp, "    size_t i;\n");
  fprintf(fp, "    cl_int err;\n");
  fprintf(fp, "    std::vector<cl::Platform> platforms;\n");
  fprintf(fp, "    OCL_CHECK(err, err = cl::Platform::get(&platforms));\n");
  fprintf(fp, "    cl::Platform platform;\n");
  fprintf(fp, "    for (i  = 0 ; i < platforms.size(); i++){\n");
  fprintf(fp, "        platform = platforms[i];\n");
  fprintf(fp, "        OCL_CHECK(err, std::string platformName = platform.getInfo<CL_PLATFORM_NAME>(&err));\n");
  fprintf(fp, "        if (platformName == \"Xilinx\"){\n");
  fprintf(fp, "            std::cout << \"\\nFound Platform\" << std::endl;\n");
  fprintf(fp, "            std::cout << \"\\nPlatform Name: \" << platformName.c_str() << std::endl;\n");
  fprintf(fp, "            break;\n");
  fprintf(fp, "        }\n");
  fprintf(fp, "    }\n");
  fprintf(fp, "    if (i == platforms.size()) {\n");
  fprintf(fp, "        std::cout << \"Error: Failed to find Xilinx platform\" << std::endl;\n");
  fprintf(fp, "        exit(EXIT_FAILURE);\n");
  fprintf(fp, "    }\n");
  fprintf(fp, "    //Getting ACCELERATOR Devices and selecting 1st such device\n");
  fprintf(fp, "    std::vector<cl::Device> devices;\n");
  fprintf(fp, "    OCL_CHECK(err, err = platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &devices));\n");
  fprintf(fp, "    return devices;\n");
  fprintf(fp, "}\n");
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
  if (!info->host_c) {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

  if (!info->hls) {
    /* OpenCL host */
    strcpy(name + len, "_host.hpp");
    strcpy(dir + len_dir, name);
    info->host_h = fopen(dir, "w");
    print_xilinx_host_header(info->host_h);
    fprintf(info->host_c, "#include \"%s\"\n", name);
  }

  strcpy(name + len, "_kernel_modules.cpp");
  strcpy(dir + len_dir, name);
  info->kernel_c = fopen(dir, "w");
  if (!info->kernel_c) {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

  strcpy(name + len, "_kernel.h");
  strcpy(dir + len_dir, name);
  info->kernel_h = fopen(dir, "w");
  if (!info->kernel_h) {
    printf("[AutoSA] Error: Can't open the file: %s\n", dir);
    exit(1);
  }

  fprintf(info->host_c, "#include <assert.h>\n");
  fprintf(info->host_c, "#include <stdio.h>\n");
  if (info->hls) {
    fprintf(info->host_c, "#include \"%s\"\n\n", name); 
  }

  fprintf(info->kernel_c, "#include \"%s\"\n", name);  

  strcpy(name + len, "_top_gen.cpp");
  strcpy(dir + len_dir, name);
  info->top_gen_c = fopen(dir, "w");

  strcpy(name + len, "_top_gen.h");
  strcpy(dir + len_dir, name);
  info->top_gen_h = fopen(dir, "w");
  
  fprintf(info->top_gen_c, "#include <isl/printer.h>\n");
  fprintf(info->top_gen_c, "#include \"%s\"\n", name);  

  fprintf(info->kernel_h, "#include <ap_int.h>\n");
  fprintf(info->kernel_h, "#include <hls_stream.h>\n");
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

  fclose(info->kernel_c);
  fclose(info->kernel_h);
  fclose(info->host_c);
  if (!info->hls) {
    fclose(info->host_h);
  }
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
  for (int i = 0; i < group->n_io_buffer; i++) {
    struct autosa_io_buffer *buf = group->io_buffers[i];
    bool insert = true;
    int pos = 0;
    for (pos = 0; pos < *n_factor; pos++) {
      if (buf->n_lane > data_pack_factors[pos]) {
        if (pos < *n_factor - 1) {
          if (buf->n_lane < data_pack_factors[pos + 1]) {
            // insert @pos+1
            pos++;
            break;
          }
        }
      } else if (buf->n_lane == data_pack_factors[pos]) {
        insert = false;
        break;
      }
    }

    if (!insert) 
      continue;

    *n_factor = *n_factor + 1;
    data_pack_factors = (int *)realloc(data_pack_factors, 
          sizeof(int) * (*n_factor));
    for (int j = *n_factor - 1; j > pos; j--) {
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
static isl_stat print_data_types_xilinx(
  struct autosa_hw_top_module *top, struct hls_info *hls)
{
  isl_printer *p;
  struct autosa_kernel *kernel;

  kernel = top->kernel;
  p = isl_printer_to_file(kernel->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  for (int i = 0; i < kernel->n_array; i++) {
    struct autosa_local_array_info *local = &kernel->array[i]; 
    int *data_pack_factors = NULL;
    int n_factor = 0;
    /* IO group */
    for (int n = 0; n < local->n_io_group; n++) {
      struct autosa_array_ref_group *group = local->io_groups[n];
      data_pack_factors = extract_data_pack_factors(data_pack_factors, &n_factor, group);
    }
    /* Drain group */
    if (local->drain_group)
      data_pack_factors = extract_data_pack_factors(data_pack_factors, &n_factor, local->drain_group);

    for (int n = 0; n < n_factor; n++) {
      if (data_pack_factors[n] != 1) {
        int width;
        width = local->array->size * 8 * data_pack_factors[n];
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "typedef ap_uint<");
        p = isl_printer_print_int(p, width);
        p = isl_printer_print_str(p, "> ");
        p = isl_printer_print_str(p, local->array->name);
        p = isl_printer_print_str(p, "_t");
        p = isl_printer_print_int(p, data_pack_factors[n]);
        p = isl_printer_print_str(p, ";");
        p = isl_printer_end_line(p);
      }
    }
    free(data_pack_factors);
  }

  isl_printer_free(p);

  return isl_stat_ok;
}

static __isl_give isl_printer *find_device_xilinx(__isl_take isl_printer *p)
{
  p = print_str_new_line(p, "if (argc != 2) {");
  p = isl_printer_indent(p, 4);
  p = print_str_new_line(p, "std::cout << \"Usage: \" << argv[0] << \" <XCLBIN File>\" << std::endl;");
  p = print_str_new_line(p, "return EXIT_FAILURE;");
  p = isl_printer_indent(p, -4);
  p = print_str_new_line(p, "}");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "cl_int err;");
  p = print_str_new_line(p, "std::vector<cl::Device> devices = get_devices();");
  p = print_str_new_line(p, "cl::Device device = devices[0];");
  p = print_str_new_line(p, "std::string device_name = device.getInfo<CL_DEVICE_NAME>();");
  p = print_str_new_line(p, "std::cout << \"Found Device=\" << device_name.c_str() << std::endl;");
  p = print_str_new_line(p, "// Creating Context and Command Queue for selected device");
  p = print_str_new_line(p, "cl::Context context(device);");
  p = print_str_new_line(p, "cl::CommandQueue q(context, device);");
  p = print_str_new_line(p, "// Import XCLBIN");
  p = print_str_new_line(p, "xclbin_file_name = argv[1];");
  p = print_str_new_line(p, "cl::Program::Binaries kernel_bins = import_binary_file();");
  p = print_str_new_line(p, "// Program and Kernel");
  p = print_str_new_line(p, "devices.resize(1);");
  p = print_str_new_line(p, "cl::Program program(context, devices, kernel_bins);");
  p = print_str_new_line(p, "cl::Kernel krnl(program, \"kernel0\");");

//  p = print_str_new_line(p, "std::string binaryFile = argv[1];");
//  p = print_str_new_line(p, "cl_int err;");
//  p = print_str_new_line(p, "cl::Context context;");
//  p = print_str_new_line(p, "cl::Kernel krnl;");
//  p = print_str_new_line(p, "cl::CommandQueue q;");
//  p = print_str_new_line(p, "// get_xil_devices() is a utility API which will find the xilinx");
//  p = print_str_new_line(p, "// platforms and will return list of devices connected to Xilinx platform");
//  p = print_str_new_line(p, "auto devices = xcl::get_xil_devices();");
//  p = print_str_new_line(p, "// read_binary_file() is a utility API which will load the binaryFile");
//  p = print_str_new_line(p, "// and will return the pointer to file buffer");
//  p = print_str_new_line(p, "auto fileBuf = xcl::read_binary_file(binaryFile);");
//  p = print_str_new_line(p, "cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};");
//  p = print_str_new_line(p, "int valid_device = 0;");
//  p = print_str_new_line(p, "for (unsigned int i = 0; i < devices.size(); i++) {");
//  p = isl_printer_indent(p, 4);
//  p = print_str_new_line(p, "auto device = devices[i];");
//  p = print_str_new_line(p, "// Creating Context and Command Queue for selected Device");
//  p = print_str_new_line(p, "OCL_CHECK(err, context = cl::Context({device}, NULL, NULL, NULL, &err));");
//  p = print_str_new_line(p, "OCL_CHECK(err, q = cl::CommandQueue(context, {device}, CL_QUEUE_PROFILING_ENABLE, &err));");
//  p = print_str_new_line(p, "std::cout << \"Trying to program device[\" << i");
//  p = isl_printer_indent(p, 4);
//  p = print_str_new_line(p, "<< \"]: \" << device.getInfo<CL_DEVICE_NAME>() << std::endl;");
//  p = isl_printer_indent(p, -4);
//  p = print_str_new_line(p, "OCL_CHECK(err, cl::Program program(context, {device}, bins, NULL, \%err));");
//  p = print_str_new_line(p, "if (err != CL_SUCCESS) {");
//  p = isl_printer_indent(p, 4);
//  p = print_str_new_line(p, "std::cout << \"Failed to program device[\" << i << \"] with xclbin file!\\n\";");
//  p = isl_printer_indent(p, -4);
//  p = print_str_new_line(p, "} else {");
//  p = isl_printer_indent(p, 4);  
//  p = print_str_new_line(p, "std::cout << \"Device[\" << i << \"]: program successful!\\n\";");  
//  p = print_str_new_line(p, "OCL_CHECK(err, krnl = cl::Kernel(program, \"kernel0\", &err));");
//  p = print_str_new_line(p, "valid_device++");
//  p = print_str_new_line(p, "break; // we break because we found a valid device");
//  p = isl_printer_indent(p, -4);
//  p = print_str_new_line(p, "}");
//  p = isl_printer_indent(p, -4);
//  p = print_str_new_line(p, "}");
//  p = print_str_new_line(p, "if (valid_device == 0) {");
//  p = isl_printer_indent(p, 4);
//  p = print_str_new_line(p, "std::cout << \"Failed to program any device found, exit!\\n\";");
//  p = print_str_new_line(p, "exit(EXIT_FAILURE);");
//  p = isl_printer_indent(p, -4);
//  p = print_str_new_line(p, "}");
  p = isl_printer_end_line(p);
}

static __isl_give isl_printer *declare_and_allocate_device_arrays_xilinx(
  __isl_take isl_printer *p, struct autosa_prog *prog)
{
  p = print_str_new_line(p, "// Allocate Memory in Host Memory");
  for (int i = 0; i < prog->n_array; i++) {
    struct autosa_array_info *array = &prog->array[i];
    if (!autosa_array_requires_device_allocation(array))
      continue;
    
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "std::vector<");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, ", aligned_allocator<");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, ">> ");
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "(");
    p = autosa_array_info_print_data_size(p, array);
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);
  }
  p = isl_printer_end_line(p);

  /* Initialize buffer. */
  p = print_str_new_line(p, "// Initialize Host Buffers");
  for (int i = 0; i < prog->n_array; i++) {
    struct autosa_array_info *array = &prog->array[i];
    if (!autosa_array_requires_device_allocation(array))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "std::copy(reinterpret_cast<");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, " *>(");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "), reinterpret_cast<");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, " *>(");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ") + ");
    p = autosa_array_info_print_data_size(p, array);
    p = isl_printer_print_str(p, ", dev_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ".begin());");
    p = isl_printer_end_line(p);
  }
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "// Allocate Buffer in Global Memory");
  p = print_str_new_line(p, "// Buffers are allocated using CL_MEM_USE_HOST_PTR for efficient memory and");
  p = print_str_new_line(p, "// Device-to-host communication");
  for (int i = 0; i < prog->n_array; i++) {
    int indent1, indent2;
    struct autosa_array_info *array = &prog->array[i];
    if (!autosa_array_requires_device_allocation(array))
      continue;

    p = print_str_new_line(p, "OCL_CHECK(err,");
    indent1 = strlen("OCL_CHECK(");
    p = isl_printer_indent(p, indent1);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "cl::Buffer buffer_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "(context,");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, strlen("cl::Buffer buffer_") + strlen(array->name) + 1);
    p = print_str_new_line(p, "CL_MEM_USE_HOST_PTR | CL_MEM_READ_WRITE,");
    p = isl_printer_start_line(p);
    p = autosa_array_info_print_size(p, array);
    p = isl_printer_print_str(p, ",");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "dev_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ".data(),");
    p = isl_printer_end_line(p);
    p = print_str_new_line(p, "&err));");
    p = isl_printer_indent(p, -(strlen("cl::Buffer buffer_") + strlen(array->name) + 1));
    p = isl_printer_indent(p, -indent1);
  }
  p = isl_printer_end_line(p);
  p = print_str_new_line(p, "auto fpga_begin = std::chrono::high_resolution_clock::now();");
  p = isl_printer_end_line(p);

  return p;
}

static __isl_give isl_printer *declare_and_allocate_cpu_arrays_xilinx(
  __isl_take isl_printer *p, struct autosa_prog *prog)
{
  for (int i = 0; i < prog->n_array; i++) {
    struct autosa_array_info *array = &prog->array[i];
    if (!autosa_array_requires_device_allocation(array))
      continue;

    p = isl_printer_start_line(p);
    p = autosa_print_array_type(p, array);
    p = isl_printer_print_str(p, " *dev_");
    p = isl_printer_print_str(p, array->name);

    p = isl_printer_print_str(p, " = (");
    p = autosa_print_array_type(p, array);
    p = isl_printer_print_str(p, " *)malloc(");
    p = autosa_array_info_print_data_size(p, array);
    p = isl_printer_print_str(p, " * sizeof(");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, "));");
    p = isl_printer_end_line(p);
  }
  p = isl_printer_end_line(p);
  return p;
}

/* Print code for initializing the device for execution of the transformed
 * code. This includes declaring locally defined variables as well as
 * declaring and allocating the required copies of arrays on the device.
 */
static __isl_give isl_printer *init_device_xilinx(__isl_take isl_printer *p,
	struct autosa_prog *prog, int hls)
{
	p = autosa_print_local_declarations(p, prog);
  if (!hls) {
    p = find_device_xilinx(p);
    p = declare_and_allocate_device_arrays_xilinx(p, prog); 
  } else {
    p = declare_and_allocate_cpu_arrays_xilinx(p, prog);
  }

	return p;
}

static __isl_give isl_printer *autosa_free_cpu_arrays_xilinx(
  __isl_take isl_printer *p, struct autosa_prog *prog)
{
  p = print_str_new_line(p, "// Clean up resources");
  for (int i = 0; i < prog->n_array; i++) {
    struct autosa_array_info *array = &prog->array[i];
    if (!autosa_array_requires_device_allocation(&prog->array[i]))
      continue;

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "free(dev_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);
  }

  return p;
}

/* Print code for clearing the device after execution of the transformed code.
 * In particular, free the memory that was allocated on the device.
 */
static __isl_give isl_printer *clear_device_xilinx(__isl_take isl_printer *p,
	struct autosa_prog *prog, int hls)
{
  if (hls) 
	  p = autosa_free_cpu_arrays_xilinx(p, prog);
  else {
    p = print_str_new_line(p, "q.finish();");
    p = isl_printer_end_line(p);
    p = print_str_new_line(p, "auto fpga_end = std::chrono::high_resolution_clock::now();");
    p = isl_printer_end_line(p);
    p = print_str_new_line(p, "// Calculate Time");
    p = print_str_new_line(p, "std::chrono::duration<double> fpga_duration = fpga_end - fpga_begin;");
    p = print_str_new_line(p, "std::cout << \"FPGA Time: \" << fpga_duration.count() << \" s\" << std::endl;");
    p = isl_printer_end_line(p);
    /* Restore buffer */
    p = print_str_new_line(p, "// Restore Data from Host Buffers");
    for (int i = 0; i < prog->n_array; i++) {
      struct autosa_array_info *array = &prog->array[i];
      if (!autosa_array_requires_device_allocation(array))
        continue;
  
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "std::copy(dev_");
      p = isl_printer_print_str(p, array->name);
      p = isl_printer_print_str(p, ".begin(), dev_");
      p = isl_printer_print_str(p, array->name);
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

/* Print code to "p" for copying "array" from the host to the device
 * in its entirety.  The bounds on the extent of "array" have
 * been precomputed in extract_array_info and are used in
 * gpu_array_info_print_size.
 */
static __isl_give isl_printer *copy_array_to_device_xilinx(
  __isl_take isl_printer *p,
	struct autosa_array_info *array, int hls)
{
  int indent;
  if (!hls) {
    p = print_str_new_line(p, "OCL_CHECK(err,");
    indent = strlen("OCL_CHECK(");
    p = isl_printer_indent(p, indent);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "err = q.enqueueMigrateMemObjects({buffer_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "}, 0));");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, -indent);
  } else {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "memcpy(dev_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ", ");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ", ");
    p = autosa_array_info_print_data_size(p, array);
    p = isl_printer_print_str(p, " * sizeof(");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, "));");
    p = isl_printer_end_line(p);
  }

	return p;
}

/* Print code to "p" for copying "array" back from the device to the host
 * in its entirety.  The bounds on the extent of "array" have
 * been precomputed in extract_array_info and are used in
 * polysa_array_info_print_size.
 */
static __isl_give isl_printer *copy_array_from_device_xilinx(
	__isl_take isl_printer *p, struct autosa_array_info *array, int hls)
{
  int indent;
  if (!hls) {
    p = print_str_new_line(p, "OCL_CHECK(err,");
    indent = strlen("OCL_CHECK(");
    p = isl_printer_indent(p, indent);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "err = q.enqueueMigrateMemObjects({buffer_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "}, CL_MIGRATE_MEM_OBJECT_HOST));");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, -indent);
  } else {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "memcpy(");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ", dev_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, ", ");
    p = autosa_array_info_print_data_size(p, array);
    p = isl_printer_print_str(p, " * sizeof(");
    p = isl_printer_print_str(p, array->type);
    p = isl_printer_print_str(p, "));");
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
static __isl_give isl_printer *print_device_node_xilinx(__isl_take isl_printer *p,
	__isl_keep isl_ast_node *node, struct autosa_prog *prog, int hls)
{
	isl_ast_expr *expr, *arg;
	isl_id *id;
	const char *name;
	struct autosa_array_info *array;

	expr = isl_ast_node_user_get_expr(node);
	arg = isl_ast_expr_get_op_arg(expr, 0);
	id = isl_ast_expr_get_id(arg);
	name = isl_id_get_name(id);
	array = (struct autosa_array_info *)isl_id_get_user(id);
	isl_id_free(id);
	isl_ast_expr_free(arg);
	isl_ast_expr_free(expr);

	if (!name)
		return isl_printer_free(p);
	if (!strcmp(name, "init_device"))
		return init_device_xilinx(p, prog, hls); 
	if (!strcmp(name, "clear_device"))
		return clear_device_xilinx(p, prog, hls); 
	if (!array)
		return isl_printer_free(p);

	if (!prefixcmp(name, "to_device"))
		return copy_array_to_device_xilinx(p, array, hls); 
	else
		return copy_array_from_device_xilinx(p, array, hls); 
}

/* Set kernel arguments:
 * - arrays
 * - parameters
 * - host iterators
 */ 
static __isl_give isl_printer *print_set_kernel_arguments_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_kernel *kernel)
{
  int n_arg = 0, n;
  unsigned nparam;
  isl_space *space;
  const char *type;

  /* array */
  for (int i = 0; i < prog->n_array; ++i) {
    int required;

    required = autosa_kernel_requires_array_argument(kernel, i);
    if (required < 0)
      return isl_printer_free(p);
    if (!required)
      continue;

    struct autosa_array_info *array = &prog->array[i];

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "OCL_CHECK(err, err = krnl.setArg(");
    p = isl_printer_print_int(p, n_arg);
    p = isl_printer_print_str(p, ", buffer_");
    p = isl_printer_print_str(p, array->name);
    p = isl_printer_print_str(p, "));");
    p = isl_printer_end_line(p);
    n_arg++;
  }

  /* param */
	space = isl_union_set_get_space(kernel->arrays);
	nparam = isl_space_dim(space, isl_dim_param);
	for (int i = 0; i < nparam; ++i) {
		const char *name;
		name = isl_space_get_dim_name(space, isl_dim_param, i);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "OCL_CHECK(err, err = krnl.setArg(");
    p = isl_printer_print_int(p, n_arg);
    p = isl_printer_print_str(p, ", ");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, "));");
    p = isl_printer_end_line(p);
    n_arg++;
	}
	isl_space_free(space);

  /* host iterator */
	n = isl_space_dim(kernel->space, isl_dim_set);
	type = isl_options_get_ast_iterator_type(prog->ctx);
	for (int i = 0; i < n; ++i) {
		const char *name;
		name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
    
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "OCL_CHECK(err, err = krnl.setArg(");
    p = isl_printer_print_int(p, n_arg);
    p = isl_printer_print_str(p, ", ");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, "));");
    p = isl_printer_end_line(p);
    n_arg++;
	}

  return p;
}

/* Print the header of the given kernel to both gen->hls.kernel_h
 * and gen->hls.kernel_c.
 */
static void print_kernel_headers_xilinx(struct autosa_prog *prog,
	struct autosa_kernel *kernel, struct hls_info *hls)
{
	isl_printer *p;

	p = isl_printer_to_file(prog->ctx, hls->kernel_h);
	p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  if (!hls->hls) {
    p = print_str_new_line(p, "extern \"C\" {");
  }
	p = print_kernel_header(p, prog, kernel, hls); 
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);
  if (!hls->hls) {
    p = print_str_new_line(p, "}");
  }
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
static __isl_give isl_printer *print_host_user_xilinx(__isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options,
  __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  int is_user;
  struct autosa_kernel *kernel;
  struct autosa_kernel_stmt *stmt;
  struct print_host_user_data *data;
  struct hls_info *hls;

  isl_ast_print_options_free(print_options);

  data = (struct print_host_user_data *) user;
  hls = data->hls;

  id = isl_ast_node_get_annotation(node);
  if (!id) {
    return print_device_node_xilinx(p, node, data->prog, hls->hls); 
  }

  is_user = !strcmp(isl_id_get_name(id), "user");
  kernel = is_user ? NULL : (struct autosa_kernel *)isl_id_get_user(id);
  stmt = is_user ? (struct autosa_kernel_stmt *)isl_id_get_user(id) : NULL;
  isl_id_free(id);

  if (is_user)
    return autosa_kernel_print_domain(p, stmt); 

  if (!hls->hls) {
    /* Print OpenCL host. */
    p = ppcg_start_block(p); 
  
    p = print_set_kernel_arguments_xilinx(p, data->prog, kernel);
    p = print_str_new_line(p, "// Launch the kernel");
    p = print_str_new_line(p, "OCL_CHECK(err, err = q.enqueueTask(krnl));");
    p = isl_printer_end_line(p);
  
    /* Print the top kernel generation function */
    /* Disabled by default */
//    p = isl_printer_start_line(p);
//    p = isl_printer_print_str(p, "/* Top Function Generation */");
//    p = isl_printer_end_line(p);
//  
//    p = isl_printer_start_line(p);
//    p = isl_printer_print_str(p, "FILE *f = fopen(\"top.cpp\", \"w\");");
//    p = isl_printer_end_line(p);
//    p = isl_printer_start_line(p);
//    p = isl_printer_print_str(p, "top_generate(");
//    p = print_top_gen_arguments(p, data->prog, kernel, 0);
//    p = isl_printer_print_str(p, ");");
//    p = isl_printer_end_line(p);
//    p = isl_printer_start_line(p);
//    p = isl_printer_print_str(p, "fclose(f);");
//    p = isl_printer_end_line(p);
//    p = isl_printer_start_line(p);
//    p = isl_printer_print_str(p, "/* Top Function Generation */");
//    p = isl_printer_end_line(p);
  
    p = ppcg_end_block(p);   
    p = isl_printer_start_line(p);
    p = isl_printer_end_line(p);
  
  } else {
    /* Print HLS host. */
    p = ppcg_start_block(p);

    p = print_str_new_line(p, "// Launch the kernel");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "kernel");
    p = isl_printer_print_int(p, kernel->id);
    p = isl_printer_print_str(p, "(");
    p = print_kernel_arguments(p, data->prog, kernel, 0, hls);
    p = isl_printer_print_str(p, ");");
    p = isl_printer_end_line(p);

    p = ppcg_end_block(p);
  }
  /* Print the top kernel header */
  print_kernel_headers_xilinx(data->prog, kernel, data->hls);

  return p;
}

/* Print the header of the given module.
 */
static __isl_give isl_printer *print_module_header_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_hw_module *module, 
  int inter, int boundary)
{
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
  p = print_module_arguments(p, prog, module->kernel, module, 1, XILINX_HW, inter, -1, boundary);
  p = isl_printer_print_str(p, ")");

  return p;
}

/* Print the header of the given module to both gen->hls.kernel_h
 * and gen->hls.kernel_c
 * If "inter" is -1, this is a normal module call.
 * If "inter" is 0, this is a intra_trans module call.
 * If "inter" is 1, this is a inter_trans module call.
 */
static isl_stat print_module_headers_xilinx(
  struct autosa_prog *prog, struct autosa_hw_module *module, 
  struct hls_info *hls, int inter, int boundary)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_header_xilinx(p, prog, module, inter, boundary); 
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->kernel_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_header_xilinx(p, prog, module, inter, boundary);
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  return isl_stat_ok;
}

/* Print out variable declarations on Xilinx platforms.
 * The local variable can be mapped to different memory resources:
 * FF, LUTRAM, BRAM, URAM.
 */
static __isl_give isl_printer *print_module_var_xilinx(
  __isl_take isl_printer *p,
	struct autosa_kernel_var *var, int double_buffer, 
  struct autosa_hw_module *module)
{
	int j;
  int use_memory = 0; // 0: FF 1: LUTRAM 2: BRAM 3: URAM
  use_memory = extract_memory_type(module, var, module->options->autosa->uram);

	p = isl_printer_start_line(p);
  if (var->n_lane == 1)
	  p = isl_printer_print_str(p, var->array->type);
  else {
    p = isl_printer_print_str(p, var->array->name);
    p = isl_printer_print_str(p, "_t");
    p = isl_printer_print_int(p, var->n_lane);
  }
	p = isl_printer_print_str(p, " ");
	p = isl_printer_print_str(p,  var->name);
  if (double_buffer)
    p = isl_printer_print_str(p, "_ping");
	for (j = 0; j < isl_vec_size(var->size); ++j) {
		isl_val *v;

		p = isl_printer_print_str(p, "[");
		v = isl_vec_get_element_val(var->size, j);
		p = isl_printer_print_val(p, v);
		isl_val_free(v);
		p = isl_printer_print_str(p, "]");
	}
	p = isl_printer_print_str(p, ";");
	p = isl_printer_end_line(p);
  if (use_memory && var->n_part != 1) {
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
  }
  if (use_memory) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "#pragma HLS RESOURCE variable=");
    p = isl_printer_print_str(p, var->name);
    if (double_buffer)
      p = isl_printer_print_str(p, "_ping");
    p = isl_printer_print_str(p, use_memory == 1? " core=RAM_2P_LUTRAM" : 
              (use_memory == 2? " core=RAM_2P_BRAM" : " core=RAM_2P_URAM"));
    p = isl_printer_end_line(p);
  }

  /* Print pong buffer */
  if (double_buffer) {
  	p = isl_printer_start_line(p);
    if (var->n_lane == 1)
      p = isl_printer_print_str(p, var->array->type);
    else {
      p = isl_printer_print_str(p, var->array->name);
      p = isl_printer_print_str(p, "_t");
      p = isl_printer_print_int(p, var->n_lane);
    }
  	p = isl_printer_print_str(p, " ");
  	p = isl_printer_print_str(p,  var->name);
    if (double_buffer)
      p = isl_printer_print_str(p, "_pong");
  	for (j = 0; j < isl_vec_size(var->size); ++j) {
  		isl_val *v;
  
  		p = isl_printer_print_str(p, "[");
  		v = isl_vec_get_element_val(var->size, j);
  		p = isl_printer_print_val(p, v);
  		isl_val_free(v);
  		p = isl_printer_print_str(p, "]");
  	}
  	p = isl_printer_print_str(p, ";");
  	p = isl_printer_end_line(p);  
    if (use_memory && var->n_part != 1) {
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
    }
    if (use_memory) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "#pragma HLS RESOURCE variable=");
      p = isl_printer_print_str(p, var->name);
      if (double_buffer)
        p = isl_printer_print_str(p, "_pong");
      p = isl_printer_print_str(p, use_memory == 1? " core=RAM_2P_LUTRAM" : 
                (use_memory == 2? " core=RAM_2P_BRAM" : " core=RAM_2P_URAM"));
      p = isl_printer_end_line(p);
    }
  }

	return p;
}

static __isl_give isl_printer *print_module_vars_xilinx(__isl_take isl_printer *p,
  struct autosa_hw_module *module, int inter)
{
  int i, n;
  isl_space *space;  
  const char *type;

  if (inter == -1) {
    for (i = 0; i < module->n_var; ++i)
      p = print_module_var_xilinx(p, &module->var[i], module->double_buffer, module);
  }

  if (module->double_buffer && inter == -1) {
    type = isl_options_get_ast_iterator_type(module->kernel->ctx);

    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "bool arb = 0;");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->in? "bool inter_trans_en = 1;" :
        "bool inter_trans_en = 0;");
    p = isl_printer_end_line(p);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, module->in? "bool intra_trans_en = 0;" :
        "bool intra_trans_en = 1;");
    p = isl_printer_end_line(p);
    /* iterators */
    space = (module->in)? module->intra_space : module->inter_space;
    n = isl_space_dim(space, isl_dim_set);
    for (int i = 0; i < n; i++) {
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

static __isl_give isl_printer *print_module_stmt(__isl_take isl_printer *p,
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
  
  switch (stmt->type) {
//    case POLYSA_KERNEL_STMT_COPY:
//      return autosa_kernel_print_copy(p, stmt);
//    case POLYSA_KERNEL_STMT_SYNC:
//      return print_sync(p, stmt);
    case AUTOSA_KERNEL_STMT_DOMAIN:
      return autosa_kernel_print_domain(p, stmt);
    case AUTOSA_KERNEL_STMT_IO:
      return autosa_kernel_print_io(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_TRANSFER:
      return autosa_kernel_print_io_transfer(p, stmt, hw_data->hls);
    case AUTOSA_KERNEL_STMT_IO_DRAM:
      return autosa_kernel_print_io_dram(p, stmt, hw_data->hls);
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

static __isl_give isl_printer *print_for_xilinx(__isl_take isl_printer *p,
  __isl_take isl_ast_print_options *print_options,
  __isl_keep isl_ast_node *node, void *user)
{
  isl_id *id;
  int pipeline;
  int unroll;

  pipeline = 0;
  unroll = 0;
  id = isl_ast_node_get_annotation(node);

  if (id) {
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
  struct print_hw_module_data hw_data = {hls, prog, module};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);
    
  print_module_headers_xilinx(prog, module, hls, 0, boundary);   
  fprintf(hls->kernel_c, "{\n");
  if (hls->target == XILINX_HW)
    fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  print_module_iterators(hls->kernel_c, module);
  
  p = isl_printer_indent(p, 4);  
  p = print_module_vars_xilinx(p, module, 0);  
  p = isl_printer_end_line(p);

  if (module->double_buffer) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (!intra_trans_en) return;");
    p = isl_printer_end_line(p);
    p = isl_printer_end_line(p);
  }

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                    &print_module_stmt, &hw_data); 
  if (hls->target == XILINX_HW) {
    print_options = isl_ast_print_options_set_print_for(print_options,
                      &print_for_xilinx, &hw_data);
  }

  p = isl_ast_node_print(module->intra_tree, p, print_options);
  p = isl_printer_indent(p, -4);

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
  struct print_hw_module_data hw_data = {hls, prog, module};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);
  
  if (hls->target == XILINX_HW)
    print_module_headers_xilinx(prog, module, hls, 1, boundary);   
  fprintf(hls->kernel_c, "{\n");
  fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  print_module_iterators(hls->kernel_c, module);
  
  p = isl_printer_indent(p, 4);
  if (hls->target == XILINX_HW)
    p = print_module_vars_xilinx(p, module, 1);  
  p = isl_printer_end_line(p);

  if (module->double_buffer) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "if (!inter_trans_en) return;");
    p = isl_printer_end_line(p);
    p = isl_printer_end_line(p);
  }

  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                    &print_module_stmt, &hw_data); 
  if (hls->target == XILINX_HW) {
    print_options = isl_ast_print_options_set_print_for(print_options,
                      &print_for_xilinx, &hw_data);
  }
 
  p = isl_ast_node_print((boundary == 0)? 
        module->inter_tree : module->boundary_inter_tree, p, print_options);
  p = isl_printer_indent(p, -4);

  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);
  
  p = isl_printer_end_line(p);

  return p;
}

static __isl_give isl_printer *print_module_core_header_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_hw_module *module, 
  int inter, int boundary, int types)
{
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
  p = isl_printer_print_str(p, "(");
  p = print_module_arguments(p, prog, module->kernel, module, types, 
                              XILINX_HW, inter, -1, boundary);
  p = isl_printer_print_str(p, ")");

  return p;
}

static __isl_give isl_printer *print_module_core_headers_xilinx(
  __isl_take isl_printer *p, struct autosa_prog *prog, 
  struct autosa_hw_module *module, struct hls_info *hls, 
  int inter, int boundary, int types)
{
  p = print_module_core_header_xilinx(p, prog, module, inter, boundary, types);

  return p; 
}

static __isl_give isl_printer *print_module_wrapper_header_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_hw_module *module, 
  int inter, int boundary)
{
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
                             XILINX_HW, inter, -1, boundary);
  p = isl_printer_print_str(p, ")");

  return p;
}

static isl_stat print_module_wrapper_headers_xilinx(
  struct autosa_prog *prog, struct autosa_hw_module *module, 
  struct hls_info *hls, int inter, int boundary)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_wrapper_header_xilinx(p, prog, module, inter, boundary);  
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->kernel_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_module_wrapper_header_xilinx(p, prog, module, inter, boundary); 
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  return isl_stat_ok;
}

/* Print the default module. */
static __isl_give isl_printer *autosa_print_default_module(
  __isl_take isl_printer *p,
  struct autosa_hw_module *module, struct autosa_prog *prog,
  struct hls_info *hls, int boundary)
{
  struct print_hw_module_data hw_data = {hls, prog, module};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  /* Print core. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  if (hls->target == XILINX_HW)
    p = print_module_core_headers_xilinx(p, prog, module, hls, -1, boundary, 1);    
  fprintf(hls->kernel_c, "{\n");
  fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  print_module_iterators(hls->kernel_c, module);
  
  p = isl_printer_indent(p, 4);
  if (hls->target == XILINX_HW)
    p = print_module_vars_xilinx(p, module, -1);  
  p = isl_printer_end_line(p);
  
  if (module->credit && !module->in) {
    if (hls->target == XILINX_HW) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "credit.write(1);");
      p = isl_printer_end_line(p);
    } 
  }
  
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                    &print_module_stmt, &hw_data); 
  if (hls->target == XILINX_HW) {
    print_options = isl_ast_print_options_set_print_for(print_options,
                      &print_for_xilinx, &hw_data);
  }
  
  if (!boundary)
    p = isl_ast_node_print(module->device_tree, p, print_options);
  else
    p = isl_ast_node_print(module->boundary_tree, p, print_options);
  
  if (module->credit && module->in) {
    if (hls->target == XILINX_HW) {
      p = isl_printer_start_line(p);
      p = isl_printer_print_str(p, "int token = credit.read();");
      p = isl_printer_end_line(p);
    }
  }
  
  p = isl_printer_indent(p, -4);
   
  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);
  
  p = isl_printer_end_line(p);

  /* Print wrapper. */
  if (hls->target == XILINX_HW) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);
  
    print_module_wrapper_headers_xilinx(prog, module, hls, -1, boundary); 
  
    fprintf(hls->kernel_c, "{\n");
    p = isl_printer_indent(p, 4);
   
    p = print_module_core_headers_xilinx(p, prog, module, hls, -1, boundary, 0);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);

    p = isl_printer_indent(p, -4);
    fprintf(hls->kernel_c, "}\n");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);
    
    p = isl_printer_end_line(p);
  }

  return p;
}

static __isl_give isl_printer *print_pe_dummy_module_core_header_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_pe_dummy_module *module, int types)
{
  struct autosa_array_ref_group *group = module->io_group;

  p = isl_printer_start_line(p);
  if (types)
    p = isl_printer_print_str(p, "void ");
  // group_name
  p = isl_printer_print_str(p, group->array->name);
  if (group->group_type == AUTOSA_IO_GROUP) {
    if (group->local_array->n_io_group > 1) {
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_int(p, group->nr);
    } 
  } else if (group->group_type == AUTOSA_DRAIN_GROUP) {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, "drain");
  }
  p = isl_printer_print_str(p, "_PE_dummy");
  p = isl_printer_print_str(p, "(");
  p = print_pe_dummy_module_arguments(p, prog, module->module->kernel, 
                                      module, types, XILINX_HW);
  p = isl_printer_print_str(p, ")");

  return p;
}

static __isl_give isl_printer *print_pe_dummy_module_core_headers_xilinx(
  __isl_take isl_printer *p, struct autosa_prog *prog,
  struct autosa_pe_dummy_module *module, struct hls_info *hls, int types)
{
  p = print_pe_dummy_module_core_header_xilinx(p, prog, module, types);
  
  return p;
}

static __isl_give isl_printer *print_pe_dummy_module_wrapper_header_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_pe_dummy_module *module)
{
  struct autosa_array_ref_group *group = module->io_group;

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "void ");
  // group_name
  p = isl_printer_print_str(p, group->array->name);
  if (group->group_type == AUTOSA_IO_GROUP) {
    if (group->local_array->n_io_group > 1) {
      p = isl_printer_print_str(p, "_");
      p = isl_printer_print_int(p, group->nr);
    } 
  } else if (group->group_type == AUTOSA_DRAIN_GROUP) {
    p = isl_printer_print_str(p, "_");
    p = isl_printer_print_str(p, "drain");
  }
  p = isl_printer_print_str(p, "_PE_dummy_wrapper");
  p = isl_printer_print_str(p, "(");
  p = print_pe_dummy_module_arguments(p, prog, module->module->kernel, 
                                      module, 1, XILINX_HW);
  p = isl_printer_print_str(p, ")");

  return p;
}

static isl_stat print_pe_dummy_module_wrapper_headers_xilinx(
  struct autosa_prog *prog, struct autosa_pe_dummy_module *module, 
  struct hls_info *hls)
{
  isl_printer *p;

  p = isl_printer_to_file(prog->ctx, hls->kernel_h);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_pe_dummy_module_wrapper_header_xilinx(p, prog, module);
  p = isl_printer_print_str(p, ";");
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  p = isl_printer_to_file(prog->ctx, hls->kernel_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);
  p = print_pe_dummy_module_wrapper_header_xilinx(p, prog, module);
  p = isl_printer_end_line(p);
  isl_printer_free(p);

  return isl_stat_ok;
}

static __isl_give isl_printer *autosa_print_default_pe_dummy_module(
  __isl_take isl_printer *p,
  struct autosa_pe_dummy_module *pe_dummy_module,
  struct autosa_prog *prog, struct hls_info *hls, int boundary)
{
  struct autosa_hw_module *module = pe_dummy_module->module;
  struct print_hw_module_data hw_data = {hls, prog, module};
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_printer_get_ctx(p);

  /* Print core. */
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);

  if (hls->target == XILINX_HW)
    p = print_pe_dummy_module_core_headers_xilinx(p, prog, 
                                                  pe_dummy_module, hls, 1);
  
  fprintf(hls->kernel_c, "{\n");
  fprintf(hls->kernel_c, "#pragma HLS INLINE OFF\n");
  print_module_iterators(hls->kernel_c, module);
  
  p = isl_printer_indent(p, 4);
  p = isl_printer_end_line(p);
  
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                    &print_module_stmt, &hw_data); 
  if (hls->target == XILINX_HW) {
    print_options = isl_ast_print_options_set_print_for(print_options,
                      &print_for_xilinx, &hw_data);
  }
  
  p = isl_ast_node_print(pe_dummy_module->device_tree, p, print_options);
  
  p = isl_printer_indent(p, -4);
   
  fprintf(hls->kernel_c, "}\n");
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "/* Module Definition */");
  p = isl_printer_end_line(p);
  
  p = isl_printer_end_line(p);

  /* Print wrapper. */
  if (hls->target == XILINX_HW) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);
  
    print_pe_dummy_module_wrapper_headers_xilinx(prog, pe_dummy_module, hls);
    
    fprintf(hls->kernel_c, "{\n");
    p = isl_printer_indent(p, 4);
    p = print_pe_dummy_module_core_headers_xilinx(p, prog, pe_dummy_module, hls, 0);
    p = isl_printer_print_str(p, ";");
    p = isl_printer_end_line(p);
    p = isl_printer_indent(p, -4);
    fprintf(hls->kernel_c, "}\n");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "/* Module Definition */");
    p = isl_printer_end_line(p);
    
    p = isl_printer_end_line(p);
  }

  return p;
}

static __isl_give isl_printer *autosa_print_host_code(__isl_take isl_printer *p,
  struct autosa_prog *prog, __isl_keep isl_ast_node *tree, 
  struct autosa_hw_module **modules, int n_modules,
  struct autosa_hw_top_module *top,
  struct hls_info *hls)
{
  isl_ast_print_options *print_options;
  isl_ctx *ctx = isl_ast_node_get_ctx(tree);
  struct print_host_user_data data = { hls, prog, top };
  struct print_hw_module_data hw_data = { hls, prog, NULL };
  isl_printer *p_module;

  /* Print the data pack types in the program. */
  print_data_types_xilinx(top, hls);

  /* Print the default AST. */
  print_options = isl_ast_print_options_alloc(ctx);
  print_options = isl_ast_print_options_set_print_user(print_options,
                  &print_host_user_xilinx, &data);

  /* Print the macros definitions in the program. */
  p = autosa_print_macros(p, tree); 
  p = isl_ast_node_print(tree, p, print_options);

  /* Print the hw module ASTs. */
  p_module = isl_printer_to_file(ctx, hls->kernel_c);
  p_module = isl_printer_set_output_format(p_module, ISL_FORMAT_C);

  for (int i = 0; i < n_modules; i++) {
    if (modules[i]->is_filter && modules[i]->is_buffer) {
      /* Print out the definitions for inter_trans and intra_trans function calls */
      /* Intra transfer function */
      p_module = autosa_print_intra_trans_module(p_module, modules[i], prog, hls, 0);
       
      /* Inter transfer function */
      p_module = autosa_print_inter_trans_module(p_module, modules[i], prog, hls, 0);
      if (modules[i]->boundary)
        p_module = autosa_print_inter_trans_module(p_module, modules[i], prog, hls, 1);
    }

    p_module = autosa_print_default_module(p_module, modules[i], prog, hls, 0);
    if (modules[i]->boundary) {
      /* Print out the definitions for boundary trans function calls. */
      p_module = autosa_print_default_module(p_module, modules[i], prog, hls, 1); 
    }
    if (modules[i]->n_pe_dummy_modules > 0) {
      /* Print out the definitions for pe dummy function calls. */
      for (int j = 0; j < modules[i]->n_pe_dummy_modules; j++) {
        p_module = autosa_print_default_pe_dummy_module(
            p_module, modules[i]->pe_dummy_modules[j], prog, hls, 0);
      }
    }
  }
  isl_printer_free(p_module);
    
  return p;
}

/* Declare the AXI interface for each global pointers. 
 */
static __isl_give isl_printer *print_top_module_interface_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_kernel *kernel)
{
  int n;
  unsigned nparam;
  isl_space *space;
  const char *type;

  for (int i = 0; i < kernel->n_array; ++i) {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (autosa_kernel_requires_array_argument(kernel, i) 
        && !autosa_array_is_scalar(local_array->array)) {
      if (local_array->n_io_group_refs > 1) {
        for (int j = 0; j < local_array->n_io_group_refs; j++) {
          p = print_str_new_line(p, "p = isl_printer_start_line(p);");
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE m_axi port=");
          p = isl_printer_print_str(p, local_array->array->name);
          p = isl_printer_print_str(p, "_");
          p = isl_printer_print_int(p, j);
          p = isl_printer_print_str(p, " offset=slave bundle=gmem_");
          p = isl_printer_print_str(p, local_array->array->name);
          p = isl_printer_print_str(p, "_");
          p = isl_printer_print_int(p, j);
          p = isl_printer_print_str(p, "\");");
          p = isl_printer_end_line(p);
          p = print_str_new_line(p, "p = isl_printer_end_line(p);");       
        }
      } else {
        p = print_str_new_line(p, "p = isl_printer_start_line(p);");
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE m_axi port=");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, " offset=slave bundle=gmem_");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, "\");");
        p = isl_printer_end_line(p);
        p = print_str_new_line(p, "p = isl_printer_end_line(p);");
      }
    }
  }

  for (int i = 0; i < kernel->n_array; ++i) {
    struct autosa_local_array_info *local_array = &kernel->array[i];
    if (autosa_kernel_requires_array_argument(kernel, i)) {
      if (local_array->n_io_group_refs > 1) {
        for (int j = 0; j < local_array->n_io_group_refs; j++) {
          p = print_str_new_line(p, "p = isl_printer_start_line(p);");
          p = isl_printer_start_line(p);
          p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE s_axilite port=");
          p = isl_printer_print_str(p, local_array->array->name);
          p = isl_printer_print_str(p, "_");
          p = isl_printer_print_int(p, j);
          p = isl_printer_print_str(p, " bundle=control\");");
          p = isl_printer_end_line(p);
          p = print_str_new_line(p, "p = isl_printer_end_line(p);");
        }
      } else {
        p = print_str_new_line(p, "p = isl_printer_start_line(p);");
        p = isl_printer_start_line(p);
        p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE s_axilite port=");
        p = isl_printer_print_str(p, local_array->array->name);
        p = isl_printer_print_str(p, " bundle=control\");");
        p = isl_printer_end_line(p);
        p = print_str_new_line(p, "p = isl_printer_end_line(p);");
      }
    }
  }

  space = isl_union_set_get_space(kernel->arrays);
  nparam = isl_space_dim(space, isl_dim_param);
  for (int i = 0; i < nparam; i++) {
    const char *name;
    name = isl_space_get_dim_name(space, isl_dim_param, i);
    p = print_str_new_line(p, "p = isl_printer_start_line(p);");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE s_axilite port=");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, " bundle=control\");");
    p = isl_printer_end_line(p);
    p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  }
  isl_space_free(space);

  n = isl_space_dim(kernel->space, isl_dim_set);
  type = isl_options_get_ast_iterator_type(prog->ctx);
  for (int i = 0; i < n; i++) {
    const char *name;
    name = isl_space_get_dim_name(kernel->space, isl_dim_set, i);
    p = print_str_new_line(p, "p = isl_printer_start_line(p);");
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE s_axilite port=");
    p = isl_printer_print_str(p, name);
    p = isl_printer_print_str(p, " bundle=control\");");
    p = isl_printer_end_line(p);
    p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  }

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"#pragma HLS INTERFACE s_axilite port=return bundle=control\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  return p;
}

static __isl_give isl_printer *print_top_module_headers_xilinx(
  __isl_take isl_printer *p,
  struct autosa_prog *prog, struct autosa_hw_top_module *top, struct hls_info *hls)
{
  struct autosa_kernel *kernel = top->kernel;

  if (!hls->hls) {
    p = print_str_new_line(p, "p = isl_printer_start_line(p);");
    p = print_str_new_line(p, "p = isl_printer_print_str(p, \"extern \\\"C\\\" {\");");
    p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  }

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_print_str(p, \"void kernel");
  p = isl_printer_print_int(p, top->kernel->id);
  p = isl_printer_print_str(p, "(");
  p = print_kernel_arguments(p, prog, top->kernel, 1, hls);
  p = isl_printer_print_str(p, ")\");");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"{\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");

  /* Print out the interface pragmas. */
  p = print_top_module_interface_xilinx(p, prog, kernel);

  /* Print out the dataflow pragma. */
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"#pragma HLS DATAFLOW\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  
  return p;
}

static char *extract_fifo_name_from_fifo_decl_name(isl_ctx *ctx, char *fifo_decl_name) {
  int loc = 0;
  char ch;
  isl_printer *p_str = isl_printer_to_str(ctx);
  char *name = NULL;

  while ((ch = fifo_decl_name[loc]) != '\0') {
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

static char *extract_fifo_width_from_fifo_decl_name(isl_ctx *ctx, char *fifo_decl_name) {
  int loc = 0;
  char ch;
  isl_printer *p_str = isl_printer_to_str(ctx);
  char *name = NULL;

  while ((ch = fifo_decl_name[loc]) != '\0') {
    if (ch == '.')
      break;
    loc++;
  }

  loc++;

  while ((ch = fifo_decl_name[loc]) != '\0') {
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

  switch (stmt->type) {
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

  switch (stmt->type) {
    case AUTOSA_KERNEL_STMT_MODULE_CALL:
      return autosa_kernel_print_module_call(p, stmt, data->prog);
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
  struct print_hw_module_data hw_data = { hls, prog, NULL };

  /* Print the top module ASTs. */
  p = isl_printer_to_file(ctx, hls->top_gen_c);
  p = isl_printer_set_output_format(p, ISL_FORMAT_C);

  print_top_gen_headers(prog, top, hls);
  fprintf(hls->top_gen_c, "{\n");
  p = isl_printer_indent(p, 4);

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

  if (hls->target == XILINX_HW)
    p = print_top_module_headers_xilinx(p, prog, top, hls);
  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, 4);");
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

  for (int i = 0; i < top->n_fifo_decls; i++) {
    /* Generate fifo decl counter. */
    char *fifo_decl_name = top->fifo_decl_names[i];
    char *fifo_name = extract_fifo_name_from_fifo_decl_name(ctx, fifo_decl_name);
    char *fifo_w = extract_fifo_width_from_fifo_decl_name(ctx, fifo_decl_name);
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "fifo_cnt = 0;");
    p = isl_printer_end_line(p);

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
  for (int i = 0; i < top->n_hw_modules; i++) {
    /* Generate module call counter. */
    struct autosa_hw_module *module = top->hw_modules[i];
    char *module_name;

    if (module->is_filter && module->is_buffer) {
      module_name = concat(ctx, module->name, "intra_trans");
      
      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;

      module_name = concat(ctx, module->name, "inter_trans");
      
      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;

      if (module->boundary) {
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
 
    if (module->boundary) {
      module_name = concat(ctx, module->name, "boundary");
      
      n_module_names++;
      module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
      module_names[n_module_names - 1] = module_name;       
    }

    if (module->n_pe_dummy_modules > 0) {
      for (int j = 0; j < module->n_pe_dummy_modules; j++) {
        struct autosa_pe_dummy_module *dummy_module = module->pe_dummy_modules[j];
        struct autosa_array_ref_group *group = dummy_module->io_group;
        isl_printer *p_str = isl_printer_to_str(ctx);
        p_str = autosa_array_ref_group_print_prefix(group, p_str);
        p_str = isl_printer_print_str(p_str, "_PE_dummy");
        module_name = isl_printer_get_str(p_str);
        isl_printer_free(p_str);
        
        n_module_names++;
        module_names = (char **)realloc(module_names, n_module_names * sizeof(char *));
        module_names[n_module_names - 1] = module_name;       
      }
    }
  }
  for (int i = 0; i < n_module_names; i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "int ");
    p = isl_printer_print_str(p, module_names[i]);
    p = isl_printer_print_str(p, "_cnt = 0;");
    p = isl_printer_end_line(p);
  }

  /* Print module calls. */
  for (int i = 0; i < top->n_module_calls; i++) {
    /* Print AST */
    print_options = isl_ast_print_options_alloc(ctx);
    print_options = isl_ast_print_options_set_print_user(print_options,
                      &print_top_module_call_stmt, &hw_data); 
  
    p = isl_ast_node_print(top->module_call_wrapped_trees[i], 
          p, print_options);    
  }

  /* module:module_name:module_cnt. */
  for (int i = 0; i < n_module_names; i++) {
    p = isl_printer_start_line(p);
    p = isl_printer_print_str(p, "fprintf(fd, \"module:");
    p = isl_printer_print_str(p, module_names[i]);
    p = isl_printer_print_str(p, ":\%d\\n\", ");
    p = isl_printer_print_str(p, module_names[i]);
    p = isl_printer_print_str(p, "_cnt);");
    p = isl_printer_end_line(p);
  }
  p = isl_printer_end_line(p);   

  for (int i = 0; i < n_module_names; i++) {
    free(module_names[i]);
  }
  free(module_names);

  p = isl_printer_start_line(p);
  p = isl_printer_print_str(p, "p = isl_printer_indent(p, -4);");
  p = isl_printer_end_line(p);

  p = print_str_new_line(p, "p = isl_printer_start_line(p);");
  p = print_str_new_line(p, "p = isl_printer_print_str(p, \"}\");");
  p = print_str_new_line(p, "p = isl_printer_end_line(p);");
  if (hls->target == XILINX_HW) {
    if (!hls->hls) {
      p = print_str_new_line(p, "p = isl_printer_start_line(p);");
      p = print_str_new_line(p, "p = isl_printer_print_str(p, \"}\");");
      p = print_str_new_line(p, "p = isl_printer_end_line(p);");
    }
  }

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
  p = isl_printer_indent(p, -4);
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
  struct autosa_types *types, void *user)
{
  struct hls_info *hls = (struct hls_info *)user;
  isl_printer *kernel;

  kernel = isl_printer_to_file(isl_printer_get_ctx(p), hls->kernel_c);
  kernel = isl_printer_set_output_format(kernel, ISL_FORMAT_C);
  kernel = autosa_print_types(kernel, types, prog);
  isl_printer_free(kernel);

  if (!kernel)
    return isl_printer_free(p);

  /* Print OpenCL host and kernel function. */
  p = autosa_print_host_code(p, prog, tree, modules, n_modules, top_module, hls); 
  /* Print seperate top module code generation function. */
  print_top_gen_host_code(prog, tree, top_module, hls); 

  return p;
}

/* Generate systolic arrays on Xilinx FPGAs.
 */
int generate_autosa_xilinx_hls_c(isl_ctx *ctx, struct ppcg_options *options,
	const char *input)
{
  struct hls_info hls;
  int r;

  hls.target = XILINX_HW;
  hls.hls = options->autosa->hls;
  hls.ctx = ctx;
  hls.output_dir = options->autosa->output_dir;
  hls_open_files(&hls, input);

  r = generate_sa(ctx, input, hls.host_c, options, &print_hw, &hls);

  hls_close_files(&hls);  
}