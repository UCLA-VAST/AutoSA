/*
 * Copyright 2010-2011 INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include <isl/constraint.h>

#include "schedule.h"

/* Construct a map that maps a domain of dimensionality "len"
 * to another domain of the same dimensionality such that
 * coordinate "first" of the range is equal to the sum of the "wave_len"
 * coordinates starting at "first" in the domain.
 * The remaining coordinates in the range are equal to the corresponding ones
 * in the domain.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *wavefront(__isl_take isl_dim *dim, int len,
        int first, int wave_len)
{
    int i;
    isl_int v;
    isl_basic_map *bmap;
    isl_constraint *c;

    isl_int_init(v);

    dim = isl_dim_add(dim, isl_dim_in, len);
    dim = isl_dim_add(dim, isl_dim_out, len);
    bmap = isl_basic_map_universe(isl_dim_copy(dim));

    for (i = 0; i < len; ++i) {
        if (i == first)
            continue;

        c = isl_equality_alloc(isl_dim_copy(dim));
        isl_int_set_si(v, -1);
        isl_constraint_set_coefficient(c, isl_dim_in, i, v);
        isl_int_set_si(v, 1);
        isl_constraint_set_coefficient(c, isl_dim_out, i, v);
        bmap = isl_basic_map_add_constraint(bmap, c);
    }

    c = isl_equality_alloc(isl_dim_copy(dim));
    isl_int_set_si(v, -1);
    for (i = 0; i < wave_len; ++i)
        isl_constraint_set_coefficient(c, isl_dim_in, first + i, v);
    isl_int_set_si(v, 1);
    isl_constraint_set_coefficient(c, isl_dim_out, first, v);
    bmap = isl_basic_map_add_constraint(bmap, c);

    isl_dim_free(dim);
    isl_int_clear(v);

    return isl_map_from_basic_map(bmap);
}

/* Construct a map from a len-dimensional domain to
 * a (len-n)-dimensional domain that projects out the n coordinates
 * starting at first.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *project_out(__isl_take isl_dim *dim,
    int len, int first, int n)
{
    int i, j;
    isl_constraint *c;
    isl_basic_map *bmap;
    isl_int v;

    isl_int_init(v);

    dim = isl_dim_add(dim, isl_dim_in, len);
    dim = isl_dim_add(dim, isl_dim_out, len - n);
    bmap = isl_basic_map_universe(isl_dim_copy(dim));

    for (i = 0, j = 0; i < len; ++i) {
        if (i >= first && i < first + n)
            continue;
        c = isl_equality_alloc(isl_dim_copy(dim));
        isl_int_set_si(v, -1);
        isl_constraint_set_coefficient(c, isl_dim_in, i, v);
        isl_int_set_si(v, 1);
        isl_constraint_set_coefficient(c, isl_dim_out, j, v);
        bmap = isl_basic_map_add_constraint(bmap, c);
        ++j;
    }
    isl_dim_free(dim);

    isl_int_clear(v);

    return isl_map_from_basic_map(bmap);
}

/* Construct a projection that maps a src_len dimensional domain
 * to its first dst_len coordinates.
 * "dim" prescribes the parameters.
 */
__isl_give isl_map *projection(__isl_take isl_dim *dim,
    int src_len, int dst_len)
{
    return project_out(dim, src_len, dst_len, src_len - dst_len);
}

/* Extend "set" with unconstrained coordinates to a total length of "dst_len".
 */
__isl_give isl_set *extend(__isl_take isl_set *set, int dst_len)
{
    int n_set;
    isl_dim *dim;
    isl_map *map;

    dim = isl_set_get_dim(set);
    n_set = isl_dim_size(dim, isl_dim_set);
    dim = isl_dim_drop(dim, isl_dim_set, 0, n_set);
    map = projection(dim, dst_len, n_set);
    map = isl_map_reverse(map);

    return isl_set_apply(set, map);
}

/* Extract the access in stmt->text starting at position identifier
 * and of length identifier_len, and return a corresponding cuda_stmt_access.
 *
 * The access in C notation is first copied to "buffer" (which
 * has been allocated by the caller and should be of sufficient size)
 * and slightly modified to a map in isl notation.
 * This string is then parsed by isl.
 */
static struct cuda_stmt_access *stmt_extract_access(struct cuda_stmt *stmt,
	char *buffer,
	int identifier, int identifier_len, int index, int index_len)
{
	int i;
	int pos = 0;
	unsigned nparam = isl_set_dim(stmt->domain, isl_dim_param);
	unsigned nvar = isl_set_dim(stmt->domain, isl_dim_set);
	isl_ctx *ctx = isl_set_get_ctx(stmt->domain);
	struct cuda_stmt_access *access;

	access = isl_alloc_type(ctx, struct cuda_stmt_access);
	assert(access);
	access->text_offset = identifier;
	access->text_len = (index - identifier) + index_len;
	access->next = NULL;
	access->read = 1;
	access->write = 0;

	pos += sprintf(buffer, "[");
	for (i = 0; i < nparam; ++i) {
		if (i)
			pos += sprintf(buffer + pos, ",");
		pos += sprintf(buffer + pos, "%s",
			isl_set_get_dim_name(stmt->domain, isl_dim_param, i));
	}
	pos += sprintf(buffer + pos, "] -> { %s[",
			isl_set_get_tuple_name(stmt->domain));
	for (i = 0; i < nvar; ++i) {
		if (i)
			pos += sprintf(buffer + pos, ",");
		pos += sprintf(buffer + pos, "%s",
			    isl_set_get_dim_name(stmt->domain, isl_dim_set, i));
	}
	pos += sprintf(buffer + pos, "] -> ");
	memcpy(buffer + pos, stmt->text + identifier, identifier_len);
	pos += identifier_len;
	pos += sprintf(buffer + pos, "[");
	for (i = 1; i < index_len - 1; ++i) {
		if (stmt->text[index + i] == ']') {
			buffer[pos++] = ',';
			++i;
		} else
			buffer[pos++] = stmt->text[index + i];
	}
	pos += sprintf(buffer + pos, "] }");
	access->access = isl_map_read_from_str(ctx, buffer, -1);

	return access;
}

/* Construct an access to the given iterator.
 */
static struct cuda_stmt_access *iterator_access(struct cuda_stmt *stmt,
	int identifier, int len, int pos)
{
	isl_ctx *ctx = isl_set_get_ctx(stmt->domain);
	struct cuda_stmt_access *access;
	isl_constraint *c;
	isl_map *map;

	map = isl_map_from_domain(isl_set_copy(stmt->domain));
	map = isl_map_add_dims(map, isl_dim_out, 1);
	c = isl_equality_alloc(isl_map_get_dim(map));
	isl_constraint_set_coefficient_si(c, isl_dim_in, pos, 1);
	isl_constraint_set_coefficient_si(c, isl_dim_out, 0, -1);
	map = isl_map_add_constraint(map, c);

	access = isl_alloc_type(ctx, struct cuda_stmt_access);
	assert(access);
	access->text_offset = identifier;
	access->text_len = len;
	access->next = NULL;
	access->read = 1;
	access->write = 0;

	access->access = map;

	return access;
}

/* Check if the identifier matches one of the iterators and
 * if so return an access to that iterator.
 */
static struct cuda_stmt_access *stmt_extract_iterator(struct cuda_stmt *stmt,
	int identifier, int len)
{
	int i;
	unsigned n = isl_set_dim(stmt->domain, isl_dim_set);

	for (i = 0; i < n; ++i) {
		const char *name;
		name = isl_set_get_dim_name(stmt->domain, isl_dim_set, i);
		if (!strncmp(name, stmt->text + identifier, len) &&
		    name[len] == '\0')
			return iterator_access(stmt, identifier, len, i);
	}

	return NULL;
}

static int is_identifier(int c)
{
	return isalnum(c) || c == '_';
}

static int is_assignment(const char *text, int pos, int *compound)
{
	if (text[pos] != '=')
		return 0;
	if (pos > 0 && text[pos - 1] == '=')
		return 0;
	if (text[pos + 1] == '=')
		return 0;
	if (pos >= 1 && text[pos - 1] == '>' &&
	    !(pos >= 2 && text[pos - 2] == '>'))
		return 0;
	if (pos >= 1 && text[pos - 1] == '<' &&
	    !(pos >= 2 && text[pos - 2] == '<'))
		return 0;

	*compound = pos >= 1 && strchr("+-*/%&|^<>", text[pos - 1]);

	return 1;
}

/* Extract accesses from stmt->text and store them in stmt->accesses.
 * dim describes the parameters.
 */
void stmt_extract_accesses(struct cuda_stmt *stmt)
{
	int i, j;
	size_t text_len = strlen(stmt->text);
	size_t len = 50;
	char *buffer;
	int identifier = -1;
	int end = -1;
	unsigned nparam = isl_set_dim(stmt->domain, isl_dim_param);
	unsigned nvar = isl_set_dim(stmt->domain, isl_dim_set);
	struct cuda_stmt_access **next_access = &stmt->accesses;

	for (i = 0; i < nparam; ++i)
		len += strlen(isl_set_get_dim_name(stmt->domain,
							isl_dim_param, i));
	for (i = 0; i < nvar; ++i)
		len += strlen(isl_set_get_dim_name(stmt->domain, isl_dim_set, i));
	buffer = isl_alloc_array(isl_set_get_ctx(stmt->domain), char, len);
	assert(buffer);

	stmt->accesses = NULL;
	for (i = 0; i < text_len; ++i) {
		if (identifier < 0 && isalpha(stmt->text[i])) {
			identifier = i;
			end = -1;
		} else if (identifier >= 0 && end < 0 &&
			   is_identifier(stmt->text[i]))
			continue;
		else if (identifier >= 0 && end < 0 && isblank(stmt->text[i]))
			end = i;
		else if (identifier >= 0 && end >= 0 && isblank(stmt->text[i]))
			continue;
		else if (identifier >= 0 && stmt->text[i] == '[') {
			if (end < 0)
				end = i;
			for (j = i + 1; j < text_len; ++j)
				if (stmt->text[j] == ']' &&
				    stmt->text[j + 1] != '[')
					break;
			*next_access = stmt_extract_access(stmt, buffer,
				identifier, end - identifier, i, j - i + 1);
			next_access = &(*next_access)->next;
			end = identifier = -1;
			i = j;
		} else if (identifier >= 0) {
			if (end < 0)
				end = i;
			*next_access = stmt_extract_iterator(stmt, identifier,
							    end - identifier);
			if (*next_access)
				next_access = &(*next_access)->next;
			end = identifier = -1;
		} else {
			int compound;

			end = identifier = -1;
			/* If we find an assignment and we have seen a single
			 * access, that access must be a write.
			 */
			if (is_assignment(stmt->text, i, &compound) &&
			    stmt->accesses && !stmt->accesses->next) {
				stmt->accesses->write = 1;
				if (!compound)
					stmt->accesses->read = 0;
			}
		}
	}

	free(buffer);
}
