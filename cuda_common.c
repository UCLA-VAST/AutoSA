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

static char *skip_spaces(char *s)
{
    while (isspace(*s))
        ++s;
    return s;
}

static int is_begin_scop(char *line)
{
    line = skip_spaces(line);
    if (*line != '#')
        return 0;
    line = skip_spaces(line + 1);
    if (strncmp(line, "pragma", sizeof("pragma") - 1))
        return 0;
    line = skip_spaces(line + sizeof("pragma") - 1);
    if (strncmp(line, "scop", sizeof("scop") - 1))
        return 0;
    return 1;
}

static int is_end_scop(char *line)
{
    line = skip_spaces(line);
    if (*line != '#')
        return 0;
    line = skip_spaces(line + 1);
    if (strncmp(line, "pragma", sizeof("pragma") - 1))
        return 0;
    line = skip_spaces(line + sizeof("pragma") - 1);
    if (strncmp(line, "endscop", sizeof("endscop") - 1))
        return 0;
    return 1;
}

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
    char line[1024];

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
    while (fgets(line, sizeof(line), info->input)) {
        fprintf(info->host_c, "%s", line);
        if (is_begin_scop(line))
            break;
    }
}

/* Copy all code starting at the endscop pragma from the input
 * file to the host .cu file and close all input and output files.
 */
void cuda_close_files(struct cuda_info *info)
{
    char line[1024];

    while (fgets(line, sizeof(line), info->input)) {
        if (is_end_scop(line)) {
            fprintf(info->host_c, "%s", line);
            break;
        }
    }
    while (fgets(line, sizeof(line), info->input)) {
        fprintf(info->host_c, "%s", line);
    }

    fclose(info->input);
    fclose(info->kernel_c);
    fclose(info->kernel_h);
    fclose(info->host_c);
}
