/*
 * Copyright 2010-2011 INRIA Saclay
 *
 * Use of this software is governed by the GNU LGPLv2.1 license
 *
 * Written by Sven Verdoolaege, INRIA Saclay - Ile-de-France,
 * Parc Club Orsay Universite, ZAC des vignes, 4 rue Jacques Monod,
 * 91893 Orsay, France
 */

#include "gpucode.h"

void print_cloog_macros(FILE *dst)
{
    fprintf(dst,
        "#define floord(n,d) (((n)<0) ? -((-(n)+(d)-1)/(d)) : (n)/(d))\n");
    fprintf(dst,
        "#define ceild(n,d)  (((n)<0) ? -((-(n))/(d)) : ((n)+(d)-1)/(d))\n");
    fprintf(dst, "#define max(x,y)    ((x) > (y) ? (x) : (y))\n");
    fprintf(dst, "#define min(x,y)    ((x) < (y) ? (x) : (y))\n");
}

static void print_expr(struct clast_expr *e, FILE *dst);
static void print_stmt(struct gpucode_info *info, struct clast_stmt *s);

void print_indent(FILE *dst, int indent)
{
    fprintf(dst, "%*s", indent, "");
}

static void print_name(struct clast_name *n, FILE *dst)
{
    fprintf(dst, "%s", n->name);
}

static void print_term(struct clast_term *t, FILE *dst)
{
    if (!t->var) {
	cloog_int_print(dst, t->val);
    } else {
	if (!cloog_int_is_one(t->val)) {
		cloog_int_print(dst, t->val);
		fprintf(dst, "*");
	}
        if (t->var->type == clast_expr_red)
            fprintf(dst, "(");
        print_expr(t->var, dst);
        if (t->var->type == clast_expr_red)
            fprintf(dst, ")");
    }
}

static void print_bin(struct clast_binary *b, FILE *dst)
{
    const char *s1, *s2, *s3;
    switch (b->type) {
        case clast_bin_mod:
	    s1 = "(", s2 = ")%", s3 = "";
            break;
        case clast_bin_div:
            s1 = "(", s2 = ")/(", s3 = ")";
            break;
        case clast_bin_cdiv:
            s1 = "ceild(", s2 = ", ", s3 = ")";
            break;
        case clast_bin_fdiv:
            s1 = "floord(", s2 = ", ", s3 = ")";
            break;
        default:
            assert(0);
    }
    fprintf(dst, "%s", s1);
    print_expr(b->LHS, dst);
    fprintf(dst, "%s", s2);
    cloog_int_print(dst, b->RHS);
    fprintf(dst, "%s", s3);
}

static void print_red(struct clast_reduction *r, FILE *dst)
{
    int i;
    const char *s1, *s2, *s3;

    if (r->n == 1) {
        print_expr(r->elts[0], dst);
        return;
    }

    switch (r->type) {
    case clast_red_sum:
        s1 = "", s2 = " + ", s3 = "";
        break;
    case clast_red_max:
        s1 = "max(", s2 = ", ", s3 = ")";
        break;
    case clast_red_min:
        s1 = "min(", s2 = ", ", s3 = ")";
        break;
    default:
        assert(0);
    }

    for (i = 1; i < r->n; ++i)
        fprintf(dst, "%s", s1);
    print_expr(r->elts[0], dst);
    for (i = 1; i < r->n; ++i) {
	if (r->type == clast_red_sum && r->elts[i]->type == clast_expr_term &&
		cloog_int_is_neg(((struct clast_term *) r->elts[i])->val)) {
	    struct clast_term *t = (struct clast_term *) r->elts[i];
	    cloog_int_neg(t->val, t->val);
	    fprintf(dst, " - ");
	    print_expr(r->elts[i], dst);
	    cloog_int_neg(t->val, t->val);
	} else {
	    fprintf(dst, "%s", s2);
	    print_expr(r->elts[i], dst);
	}
        fprintf(dst, "%s", s3);
    }
}

static void print_expr(struct clast_expr *e, FILE *dst)
{
    switch (e->type) {
    case clast_expr_name:
        print_name((struct clast_name*) e, dst);
        break;
    case clast_expr_term:
        print_term((struct clast_term*) e, dst);
        break;
    case clast_expr_red:
        print_red((struct clast_reduction*) e, dst);
        break;
    case clast_expr_bin:
        print_bin((struct clast_binary*) e, dst);
        break;
    default:
        assert(0);
    }
}

static void print_ass(struct clast_assignment *a, FILE *dst, int indent,
	int first_ass)
{
    print_indent(dst, indent);
    if (first_ass)
	fprintf(dst, "int ");
    fprintf(dst, "%s = ", a->LHS);
    print_expr(a->RHS, dst);
    fprintf(dst, ";\n");
}

static void print_guard(struct gpucode_info *info, struct clast_guard *g)
{
    int i;
    int n = g->n;

    print_indent(info->dst, info->indent);
    fprintf(info->dst, "if (");
    for (i = 0; i < n; ++i) {
        if (i > 0)
            fprintf(info->dst," && ");
        fprintf(info->dst,"(");
        print_expr(g->eq[i].LHS, info->dst);
        if (g->eq[i].sign == 0)
            fprintf(info->dst," == ");
        else if (g->eq[i].sign > 0)
            fprintf(info->dst," >= ");
        else
            fprintf(info->dst," <= ");
        print_expr(g->eq[i].RHS, info->dst);
        fprintf(info->dst,")");
    }
    fprintf(info->dst, ") {\n");
    info->indent += 4;
    print_stmt(info, g->then);
    info->indent -= 4;
    print_indent(info->dst, info->indent);
    fprintf(info->dst, "}\n");
}

static void print_for(struct gpucode_info *info, struct clast_for *f)
{
    assert(f->LB && f->UB);
    print_indent(info->dst, info->indent);
    fprintf(info->dst, "for (int %s = ", f->iterator);
    print_expr(f->LB, info->dst);
    fprintf(info->dst, "; %s <= ", f->iterator);
    print_expr(f->UB, info->dst);
    fprintf(info->dst, "; %s", f->iterator);
    if (cloog_int_is_one(f->stride))
	fprintf(info->dst, "++");
    else {
	fprintf(info->dst, " += ");
	cloog_int_print(info->dst, f->stride);
    }
    fprintf(info->dst, ") {\n");
    info->indent += 4;
    if (info->print_for_head)
	info->print_for_head(info, f);
    print_stmt(info, f->body);
    if (info->print_for_foot)
	info->print_for_foot(info, f);
    info->indent -= 4;
    print_indent(info->dst, info->indent);
    fprintf(info->dst, "}\n");
}

static void print_user_stmt(struct clast_user_stmt *u, FILE *dst, int indent)
{
    struct clast_stmt *t;

    print_indent(dst, indent);
    fprintf(dst, "%s", u->statement->name);
    fprintf(dst, "(");
    for (t = u->substitutions; t; t = t->next) {
	assert(CLAST_STMT_IS_A(t, stmt_ass));
	print_expr(((struct clast_assignment *) t)->RHS, dst);
	if (t->next)
	    fprintf(dst, ",");
    }
    fprintf(dst, ");\n");
}

static void print_stmt(struct gpucode_info *info, struct clast_stmt *s)
{
    int first_ass = 1;

    for ( ; s; s = s->next) {
        if (CLAST_STMT_IS_A(s, stmt_root))
            continue;
        if (CLAST_STMT_IS_A(s, stmt_ass)) {
            print_ass((struct clast_assignment *) s, info->dst, info->indent,
		      first_ass);
	    first_ass = 0;
        } else if (CLAST_STMT_IS_A(s, stmt_user)) {
	    if (info->print_user_stmt_list) {
		info->print_user_stmt_list(info, (struct clast_user_stmt *) s);
		return;
	    } else if (info->print_user_stmt)
		info->print_user_stmt(info, (struct clast_user_stmt *) s);
	    else
		print_user_stmt((struct clast_user_stmt *) s, info->dst,
				info->indent);
        } else if (CLAST_STMT_IS_A(s, stmt_for)) {
            print_for(info, (struct clast_for *) s);
        } else if (CLAST_STMT_IS_A(s, stmt_guard)) {
            print_guard(info, (struct clast_guard *) s);
        } else {
            assert(0);
        }
    }
}

void gpu_print_host_stmt(struct gpucode_info *info, struct clast_stmt *s)
{
    print_stmt(info, s);
}

__isl_give isl_set *extract_host_domain(struct clast_user_stmt *u)
{
    return isl_set_from_cloog_domain(cloog_domain_copy(u->domain));
}

/* Extract the set of scattering dimension values for which the given
 * sequence of user statements is executed.
 * In principle, this set should be the same for each of the user
 * statements in the sequence, but we compute the union just to be safe.
 */
__isl_give isl_set *extract_entire_host_domain(struct clast_user_stmt *u)
{
    struct clast_stmt *s;
    isl_set *host_domain = NULL;

    for (s = &u->stmt; s; s = s->next) {
        isl_set *set_i;

        assert(CLAST_STMT_IS_A(s, stmt_user));
        u = (struct clast_user_stmt *) s;

        set_i = extract_host_domain(u);

        if (!host_domain)
            host_domain = set_i;
        else
            host_domain = isl_set_union(host_domain, set_i);
        assert(host_domain);
    }

    return isl_set_coalesce(host_domain);
}
