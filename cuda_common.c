/*
 * Copyright 2010      INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "cuda_common.h"
#include "rewrite.h"

/* Open the "input" file for reading and open the host .cu file
 * and the kernel .hu and .cu files for writing.
 * Add the necessary includes and copy all code from the input
 * file up to the openscop pragma to the host .cu file.
 */
void cuda_open_files(struct cuda_info *info, const char *input)
{
    char name[PATH_MAX];
    const char *base;
    const char *ext;
    int len;

    base = strrchr(input, '/');
    if (base)
        base++;
    else
        base = input;
    ext = strrchr(base, '.');
    len = ext ? ext - base : strlen(base);

    memcpy(name, base, len);
    strcpy(name + len, "_host.cu");
    info->host_c = fopen(name, "w");

    strcpy(name + len, "_kernel.cu");
    info->kernel_c = fopen(name, "w");

    strcpy(name + len, "_kernel.hu");
    info->kernel_h = fopen(name, "w");
    fprintf(info->host_c, "#include <assert.h>\n");
    fprintf(info->host_c, "#include \"%s\"\n", name);
    fprintf(info->kernel_c, "#include \"%s\"\n", name);
    fprintf(info->kernel_h, "#include \"cuda.h\"\n\n");

    info->input = fopen(input, "r");
    copy_before_scop(info->input, info->host_c);
}

/* Copy all code starting at the endscop pragma from the input
 * file to the host .cu file and close all input and output files.
 */
void cuda_close_files(struct cuda_info *info)
{
    copy_after_scop(info->input, info->host_c);
    fclose(info->input);
    fclose(info->kernel_c);
    fclose(info->kernel_h);
    fclose(info->host_c);
}
