/*
 * Copyright 2011      Leiden University. All rights reserved.
 * Copyright 2014      Ecole Normale Superieure. All rights reserved.
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

#include <string.h>

#include "loc.h"

/* A pet_loc object represents a region of the input file.
 * The "start" and "end" fields contain the offsets in the input file
 * of the region, where end points to the first character after the region.
 * "line" is the line number of a line inside the region.
 * "indentation" is (a reasonable guess at) the indentation of the region.
 *
 * A special pet_loc_dummy instance is used to indicate that
 * no offset information is available (yet).
 */
struct pet_loc {
	int ref;
	isl_ctx *ctx;

	unsigned start;
	unsigned end;
	int line;
	char *indent;
};

/* A special pet_loc object that is used to indicate that
 * no region information is available yet.
 *
 * This special pet_loc object cannot be changed.
 * In particular, it is not allowed to call pet_loc_cow on this object.
 */
pet_loc pet_loc_dummy = {
	.ref = -1,
	.ctx = NULL,
	.start = 0,
	.end = 0,
	.line = -1,
	.indent = ""
};

/* Allocate a pet_loc with the given start, end, line number and indentation.
 */
__isl_give pet_loc *pet_loc_alloc(isl_ctx *ctx,
	unsigned start, unsigned end, int line, __isl_take char *indent)
{
	pet_loc *loc;

	if (!indent)
		return NULL;

	loc = isl_alloc_type(ctx, struct pet_loc);
	if (!loc)
		goto error;

	loc->ctx = ctx;
	isl_ctx_ref(ctx);
	loc->ref = 1;

	loc->start = start;
	loc->end = end;
	loc->line = line;
	loc->indent = indent;

	return loc;
error:
	free(indent);
	return NULL;
}

/* Return a pet_loc that is equal to "loc" and that has only one reference.
 *
 * It is not allowed to call pet_loc_cow on pet_loc_dummy.
 * We cannot raise an error in this case because pet_loc_dummy does
 * not have a reference to a valid isl_ctx.
 */
__isl_give pet_loc *pet_loc_cow(__isl_take pet_loc *loc)
{
	if (loc == &pet_loc_dummy)
		return NULL;
	if (!loc)
		return NULL;

	if (loc->ref == 1)
		return loc;
	loc->ref--;
	return pet_loc_alloc(loc->ctx, loc->start, loc->end, loc->line,
				strdup(loc->indent));
}

/* Return an extra reference to "loc".
 *
 * The special pet_loc_dummy object is not reference counted.
 */
__isl_give pet_loc *pet_loc_copy(__isl_keep pet_loc *loc)
{
	if (loc == &pet_loc_dummy)
		return loc;

	if (!loc)
		return NULL;

	loc->ref++;
	return loc;
}

/* Free a reference to "loc" and return NULL.
 *
 * The special pet_loc_dummy object is not reference counted.
 */
__isl_null pet_loc *pet_loc_free(__isl_take pet_loc *loc)
{
	if (loc == &pet_loc_dummy)
		return NULL;
	if (!loc)
		return NULL;
	if (--loc->ref > 0)
		return NULL;

	free(loc->indent);
	isl_ctx_deref(loc->ctx);
	free(loc);
	return NULL;
}

/* Return the offset in the input file of the start of "loc".
 */
unsigned pet_loc_get_start(__isl_keep pet_loc *loc)
{
	return loc ? loc->start : 0;
}

/* Return the offset in the input file of the character after "loc".
 */
unsigned pet_loc_get_end(__isl_keep pet_loc *loc)
{
	return loc ? loc->end : 0;
}

/* Return the line number of a line within the "loc" region.
 */
int pet_loc_get_line(__isl_keep pet_loc *loc)
{
	return loc ? loc->line : -1;
}

/* Return the indentation of the "loc" region.
 */
__isl_keep const char *pet_loc_get_indent(__isl_keep pet_loc *loc)
{
	return loc ? loc->indent : NULL;
}

/* Update loc->start and loc->end to include the region from "start"
 * to "end".
 *
 * Since we may be modifying "loc", it should be different from
 * pet_loc_dummy.
 */
__isl_give pet_loc *pet_loc_update_start_end(__isl_take pet_loc *loc,
	unsigned start, unsigned end)
{
	loc = pet_loc_cow(loc);
	if (!loc)
		return NULL;

	if (start < loc->start)
		loc->start = start;
	if (end > loc->end)
		loc->end = end;

	return loc;
}

/* Update loc->start and loc->end to include the region of "loc2".
 *
 * "loc" may be pet_loc_dummy, in which case we return a copy of "loc2".
 * Similarly, if "loc2" is pet_loc_dummy, then we leave "loc" untouched.
 */
__isl_give pet_loc *pet_loc_update_start_end_from_loc(__isl_take pet_loc *loc,
	__isl_keep pet_loc *loc2)
{
	if (!loc2)
		return pet_loc_free(loc);
	if (loc == &pet_loc_dummy)
		return pet_loc_copy(loc2);
	if (loc2 == &pet_loc_dummy)
		return loc;
	return pet_loc_update_start_end(loc, loc2->start, loc2->end);
}

/* Replace the indentation of "loc" by "indent".
 */
__isl_give pet_loc *pet_loc_set_indent(__isl_take pet_loc *loc,
	__isl_take char *indent)
{
	if (!loc || !indent)
		goto error;

	free(loc->indent);
	loc->indent = indent;
	return loc;
error:
	free(indent);
	return pet_loc_free(loc);
}
