/*
 * Copyright 2011 Leiden University. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 * 
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY LEIDEN UNIVERSITY ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LEIDEN UNIVERSITY OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as
 * representing official policies, either expressed or implied, of
 * Leiden University.
 */ 

#include <assert.h>
#include <stdio.h>
#include <isl/arg.h>

#include "scop.h"
#include "scop_yaml.h"

struct options {
	char *scop1;
	char *scop2;
};

ISL_ARGS_START(struct options, options_args)
ISL_ARG_ARG(struct options, scop1, "scop1", NULL)
ISL_ARG_ARG(struct options, scop2, "scop2", NULL)
ISL_ARGS_END

ISL_ARG_DEF(options, struct options, options_args)

/* Given two YAML descriptions of pet_scops, check whether they
 * represent equivalent scops.
 * If so, return 0.  Otherwise, return 1.
 */
int main(int argc, char **argv)
{
	isl_ctx *ctx;
	struct options *options;
	struct pet_scop *scop1, *scop2;
	FILE *file1, *file2;
	int equal;

	options = options_new_with_defaults();
	assert(options);
	argc = options_parse(options, argc, argv, ISL_ARG_ALL);
	ctx = isl_ctx_alloc_with_options(&options_args, options);

	file1 = fopen(options->scop1, "r");
	assert(file1);
	file2 = fopen(options->scop2, "r");
	assert(file2);

	scop1 = pet_scop_parse(ctx, file1);
	scop2 = pet_scop_parse(ctx, file2);

	equal = pet_scop_is_equal(scop1, scop2);

	pet_scop_free(scop2);
	pet_scop_free(scop1);

	fclose(file2);
	fclose(file1);
	isl_ctx_free(ctx);

	return equal >= 0 && equal ? 0 : 1;
}
