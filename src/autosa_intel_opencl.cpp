#include <isl/ctx.h>

#include "autosa_intel_opencl.h"
#include "autosa_common.h"

static void opencl_open_files(struct hls_info *info, const char *input)
{
  char name[PATH_MAX];
  int len;

  len = ppcg_extract_base_name(name, input);

  strcpy(name + len, "_host.cpp");
  info->host_c = fopen(name, "w");

  strcpy(name + len, "_kernel.c");
  info->kernel_c = fopen(name, "w");

  strcpy(name + len, "_kernel.h");
  info->kernel_h = fopen(name, "w");

  fprintf(info->host_c, "#include <assert.h>\n");
  fprintf(info->host_c, "#include <stdio.h>\n");
  fprintf(info->host_c, "#include <math.h>\n");
  fprintf(info->host_c, "#include <CL/opencl.h>\n");
  fprintf(info->host_c, "#include \"AOCLUtils/aocl_utils.h\"\n");
  fprintf(info->host_c, "#include \"%s\"\n", name); 
  fprintf(info->host_c, "using namespace aocl_utils;\n\n");
  fprintf(info->host_c, "#define AOCX_FIEL \"krnl.aocx\"\n\n");

  /* Print Intel helper function */
  fprintf(info->host_c, "#define HOST\n");
  fprintf(info->host_c, "#define ACL_ALIGNMENT 64\n");
  fprintf(info->host_c, "#ifdef _WIN32\n");
  fprintf(info->host_c, "void *acl_aligned_malloc(size_t size) {\n");
  fprintf(info->host_c, "    return _aligned_malloc(size, ACL_ALIGNMENT);\n");
  fprintf(info->host_c, "}\n");
  fprintf(info->host_c, "void acl_aligned_free(void *ptr) {\n");
  fprintf(info->host_c, "    _aligned_free(ptr);\n");
  fprintf(info->host_c, "}\n");
  fprintf(info->host_c, "#else\n");
  fprintf(info->host_c, "void *acl_aligned_malloc(size_t size) {\n");
  fprintf(info->host_c, "    void *result = NULL;\n");
  fprintf(info->host_c, "    if (posix_memalign(&result, ACL_ALIGNMENT, size) != 0)\n");
  fprintf(info->host_c, "        printf(\"acl_aligned_malloc() failed.\\n\");\n");
  fprintf(info->host_c, "    return result;\n");
  fprintf(info->host_c, "}\n");
  fprintf(info->host_c, "void acl_aligned_free(void *ptr) {\n");
  fprintf(info->host_c, "    free(ptr);\n");
  fprintf(info->host_c, "}\n");
  fprintf(info->host_c, "#endif\n\n");

  fprintf(info->host_c, "void cleanup_host_side_resources();\n");
  fprintf(info->host_c, "void cleanup();\n\n");

  fprintf(info->host_c, "#define CHECK(status) \\\n");
  fprintf(info->host_c, "if (status != CL_SUCCESS) { \\\n");
  fprintf(info->host_c, "    fprintf(stderr, \"error %%d in line %%d.\\n\", status, __LINE__); \\\n");
  fprintf(info->host_c, "    exit(1); \\\n");
  fprintf(info->host_c, "}\n\n");

  fprintf(info->host_c, "#define CHECK_NO_EXIT(status) \\\n");
  fprintf(info->host_c, "if (status != CL_SUCCESS) { \\\n");
  fprintf(info->host_c, "    fprintf(stderr, \"error %%d in line %%d.\\n\", status, __LINE__); \\\n");
  fprintf(info->host_c, "}\n\n");

  fprintf(info->kernel_c, "#include \"%s\"\n", name);  

  strcpy(name + len, "_top_gen.c");
  info->top_gen_c = fopen(name, "w");

  strcpy(name + len, "_top_gen.h");
  info->top_gen_h = fopen(name, "w");

  fprintf(info->host_c, "#include \"%s\"\n", name);  
  fprintf(info->top_gen_c, "#include <isl/printer.h>\n");
  fprintf(info->top_gen_c, "#include \"%s\"\n", name);  
}