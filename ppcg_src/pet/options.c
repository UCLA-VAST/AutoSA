/*
 * Copyright 2011     Leiden University. All rights reserved.
 * Copyright 2013-2014 Ecole Normale Superieure. All rights reserved.
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

#include <pet.h>
#include "options.h"
#include "version.h"

struct isl_arg_choice pet_signed_overflow[] = {
	{"avoid",	PET_OVERFLOW_AVOID},
	{"ignore",	PET_OVERFLOW_IGNORE},
	{0}
};

ISL_ARGS_START(struct pet_options, pet_options_args)
ISL_ARG_BOOL(struct pet_options, autodetect, 0, "autodetect", 0, NULL)
ISL_ARG_BOOL(struct pet_options, detect_conditional_assignment,
	0, "detect-conditional-assignment", 1, NULL)
ISL_ARG_BOOL(struct pet_options, encapsulate_dynamic_control,
	0, "encapsulate-dynamic-control", 0,
	"encapsulate all dynamic control in macro statements")
ISL_ARG_BOOL(struct pet_options, pencil, 0, "pencil", 1,
	"support pencil builtins and pragmas")
ISL_ARG_CHOICE(struct pet_options, signed_overflow, 0,
	"signed-overflow", pet_signed_overflow, PET_OVERFLOW_AVOID,
	"how to handle signed overflows")
ISL_ARG_STR_LIST(struct pet_options, n_path, paths, 'I', "include-path",
	"path", NULL)
ISL_ARG_STR_LIST(struct pet_options, n_define, defines, 'D', NULL,
	"macro[=defn]", NULL)
ISL_ARG_VERSION(&pet_print_version)
ISL_ARGS_END

ISL_ARG_DEF(pet_options, struct pet_options, pet_options_args)
ISL_ARG_CTX_DEF(pet_options, struct pet_options, pet_options_args)

ISL_CTX_SET_BOOL_DEF(pet_options, struct pet_options, pet_options_args,
	autodetect)
ISL_CTX_GET_BOOL_DEF(pet_options, struct pet_options, pet_options_args,
	autodetect)

ISL_CTX_SET_BOOL_DEF(pet_options, struct pet_options, pet_options_args,
	detect_conditional_assignment)
ISL_CTX_GET_BOOL_DEF(pet_options, struct pet_options, pet_options_args,
	detect_conditional_assignment)

ISL_CTX_SET_BOOL_DEF(pet_options, struct pet_options, pet_options_args,
	encapsulate_dynamic_control)
ISL_CTX_GET_BOOL_DEF(pet_options, struct pet_options, pet_options_args,
	encapsulate_dynamic_control)

ISL_CTX_SET_CHOICE_DEF(pet_options, struct pet_options, pet_options_args,
	signed_overflow)
ISL_CTX_GET_CHOICE_DEF(pet_options, struct pet_options, pet_options_args,
	signed_overflow)

/* Create an isl_ctx that references the pet options.
 */
isl_ctx *isl_ctx_alloc_with_pet_options()
{
	struct pet_options *options;

	options = pet_options_new_with_defaults();
	return isl_ctx_alloc_with_options(&pet_options_args, options);
}
