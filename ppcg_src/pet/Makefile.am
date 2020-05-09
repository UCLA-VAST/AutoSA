if BUNDLED_ISL
    MAYBE_ISL = isl
    ISL_LA = $(top_builddir)/isl/libisl.la
endif

SUBDIRS = $(MAYBE_ISL) .
DIST_SUBDIRS = $(MAYBE_ISL)

FORCE:
isl/libisl.la: FORCE
	cd isl; $(MAKE) $(AM_MAKEFLAGS) libisl.la

ACLOCAL_AMFLAGS = -I m4

LIB_ISL = $(ISL_LA) @ISL_LIBS@

lib_LTLIBRARIES = libpet.la
bin_PROGRAMS = @extra_bin_programs@
noinst_PROGRAMS = @extra_noinst_programs@ pet_codegen pet_check_code
EXTRA_PROGRAMS = pet pet_scop_cmp
TESTS = @extra_tests@
EXTRA_TESTS = pet_test.sh codegen_test.sh
TEST_EXTENSIONS = .sh

include_HEADERS = include/pet.h

BUILT_SOURCES = \
	gitversion.h

CLEANFILES = \
	gitversion.h \
	isl.py

DISTCLEANFILES = \
	pet-uninstalled.sh \
	pet-uninstalled.pc \
	pet.pc \
	pet.pc.in

EXTRA_DIST = \
	interface/isl.py.top \
	interface/pet.py \
	tests

PET_INCLUDES = -I$(srcdir) -I$(srcdir)/include

AM_CFLAGS = $(PET_INCLUDES) @ISL_CFLAGS@
AM_CXXFLAGS = $(PET_INCLUDES) $(CLANG_CXXFLAGS) @ISL_CFLAGS@
AM_LDFLAGS = $(CLANG_LDFLAGS)

libpet_la_SOURCES = \
	aff.h \
	aff.c \
	array.h \
	array.c \
	clang_compatibility.h \
	clang.h \
	clang.cc \
	context.h \
	context.c \
	expr.h \
	expr.c \
	expr_access_type.h \
	expr_arg.h \
	expr_arg.c \
	expr_plus.h \
	expr_plus.cc \
	filter.h \
	filter.c \
	id.h \
	id.cc \
	inlined_calls.h \
	inlined_calls.cc \
	inliner.h \
	inliner.cc \
	isl_id_to_pet_expr.h \
	isl_id_to_pet_expr.c \
	killed_locals.h \
	killed_locals.cc \
	loc.h \
	loc.c \
	maybe_pet_expr.h \
	nest.h \
	nest.c \
	options.h \
	options.c \
	patch.h \
	patch.c \
	pet_expr_to_isl_pw_aff.h \
	pet_expr_to_isl_pw_aff.c \
	print.c \
	print.h \
	tree.h \
	tree.c \
	tree2scop.h \
	tree2scop.c \
	scan.h \
	scan.cc \
	scop.h \
	scop.c \
	scop_plus.h \
	scop_plus.cc \
	skip.h \
	skip.c \
	state.h \
	substituter.h \
	substituter.cc \
	summary.h \
	summary.c \
	value_bounds.h \
	value_bounds.c \
	version.h \
	version.cc \
	pet.cc
libpet_la_LDFLAGS = -version-info @versioninfo@ $(AM_LDFLAGS) $(CLANG_RFLAG)
libpet_la_LIBADD = -lclangFrontend -lclangParse -lclangSema \
	$(LIB_CLANG_EDIT) -lclangAnalysis \
	-lclangAST -lclangLex -lclangBasic -lclangDriver -lclangSerialization \
	$(CLANG_LIBS) $(CLANG_LDFLAGS) $(LIB_ISL)

pet_CFLAGS = $(AM_CFLAGS) @LIBYAML_CPPFLAGS@
pet_LDFLAGS = $(AM_LDFLAGS) @LIBYAML_LDFLAGS@
pet_SOURCES = \
	dummy.cc \
	emit.c \
	scop_yaml.h \
	main.c
pet_LDADD = libpet.la $(LIB_ISL) -lyaml

pet_scop_cmp_CFLAGS = $(AM_CFLAGS) @LIBYAML_CPPFLAGS@
pet_scop_cmp_LDFLAGS = @LIBYAML_LDFLAGS@
pet_scop_cmp_LDADD = libpet.la $(LIB_ISL) -lyaml
pet_scop_cmp_SOURCES = \
	dummy.cc \
	scop_yaml.h \
	parse.c \
	pet_scop_cmp.c

pet_codegen_CFLAGS = $(AM_CFLAGS)
pet_codegen_LDFLAGS =
pet_codegen_LDADD = libpet.la $(LIB_ISL)
pet_codegen_SOURCES = \
	dummy.cc \
	pet_codegen.c

pet_check_code_CFLAGS = $(AM_CFLAGS)
pet_check_code_LDFLAGS =
pet_check_code_LDADD = libpet.la $(LIB_ISL)
pet_check_code_SOURCES = \
	dummy.cc \
	pet_check_code.c

if HAVE_ISL_BUILDDIR
# dummy library that captures the dependencies on all headers
# that are relevant for the bindings
noinst_LIBRARIES = libdep.a
libdep_a_CPPFLAGS = $(DEFAULT_INCLUDES) @ISL_CFLAGS@
libdep_a_SOURCES = all.c

isl.py: libdep.a interface/isl.py.top
	(cat $(srcdir)/interface/isl.py.top && \
		@ISL_BUILDDIR@/interface/extract_interface$(EXEEXT) \
			--language=python \
			$(DEFAULT_INCLUDES) @ISL_CFLAGS@ \
			"@ISL_SRCDIR@/all.h") \
			> $@ || (rm $@ && false)
endif

dist-hook: isl.py
	cp $< $(distdir)/
	echo @GIT_HEAD_VERSION@ > $(distdir)/GIT_HEAD_ID

gitversion.h: @GIT_HEAD@
	$(AM_V_GEN)echo '#define GIT_HEAD_ID "'@GIT_HEAD_VERSION@'"' > $@
