/*
 * Copyright 2013      Ecole Normale Superieure
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege,
 * Ecole Normale Superieure, 45 rue d’Ulm, 75230 Paris, France
 */

#include <assert.h>

#include "rewrite.h"

/* Copy the contents of "input" from offset "start" to "end" to "output".
 */
void copy(FILE *input, FILE *output, long start, long end)
{
	char buffer[1024];
	size_t n, m;

	if (end < 0) {
		fseek(input, 0, SEEK_END);
		end = ftell(input);
	}

	fseek(input, start, SEEK_SET);

	while (start < end) {
		n = end - start;
		if (n > 1024)
			n = 1024;
		n = fread(buffer, 1, n, input);
		assert(n > 0);
		m = fwrite(buffer, 1, n, output);
		assert(n == m);
		start += n;
	}
}
