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

#include "rewrite.h"

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

void copy_before_scop(FILE *input, FILE *output)
{
	char line[1024];
	while (fgets(line, sizeof(line), input)) {
		fprintf(output, "%s", line);
		if (is_begin_scop(line))
			break;
	}
}

void copy_after_scop(FILE *input, FILE *output)
{
	char line[1024];
	while (fgets(line, sizeof(line), input)) {
		if (is_end_scop(line)) {
			fprintf(output, "%s", line);
			break;
		}
	}
	while (fgets(line, sizeof(line), input)) {
		fprintf(output, "%s", line);
	}
}
