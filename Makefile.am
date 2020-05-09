if BUNDLED_ISL
    MAYBE_ISL = isl
    ISL_LA = $(top_builddir)/isl/libisl.la
    LOCAL_ISL_LA = isl/libisl.la
endif
if BUNDLED_PET
    MAYBE_PET = pet
    PET_LA = $(top_builddir)/pet/libpet.la
endif

SUBDIRS = $(MAYBE_ISL) $(MAYBE_PET) .

FORCE:
isl/libisl.la: FORCE
	cd isl; $(MAKE) $(AM_MAKEFLAGS) libisl.la
pet/libpet.la: FORCE
	cd pet; $(MAKE) $(AM_MAKEFLAGS) libpet.la

ACLOCAL_AMFLAGS = -I m4

LIB_ISL = $(ISL_LA) @ISL_LIBS@
LIB_PET = $(PET_LA) @PET_LIBS@

AM_CPPFLAGS = @ISL_CFLAGS@ @PET_CFLAGS@
LDADD = $(LIB_PET) $(LIB_ISL)

bin_PROGRAMS = autosa
autosa_SOURCES = \
	ppcg_src/cpu.c \
	ppcg_src/cpu.h \
	ppcg_src/cuda.c \
	ppcg_src/cuda.h \
	ppcg_src/opencl.c \
	ppcg_src/opencl.h \
	ppcg_src/cuda_common.h \
	ppcg_src/cuda_common.c \
	ppcg_src/gpu.c \
	ppcg_src/gpu.h \
	ppcg_src/gpu_array_tile.c \
	ppcg_src/gpu_array_tile.h \
	ppcg_src/gpu_group.c \
	ppcg_src/gpu_group.h \
	ppcg_src/gpu_hybrid.c \
	ppcg_src/gpu_hybrid.h \
	ppcg_src/gpu_print.c \
	ppcg_src/gpu_print.h \
	ppcg_src/gpu_tree.c \
	ppcg_src/gpu_tree.h \
	ppcg_src/grouping.c \
	ppcg_src/grouping.h \
	ppcg_src/hybrid.c \
	ppcg_src/hybrid.h \
	ppcg_src/schedule.c \
	ppcg_src/schedule.h \
	ppcg_src/ppcg_options.c \
	ppcg_src/ppcg_options.h \
	ppcg_src/ppcg.c \
	ppcg_src/ppcg.h \
	ppcg_src/print.c \
	ppcg_src/print.h \
	ppcg_src/util.c \
	ppcg_src/util.h \
	ppcg_src/version.c

TESTS = @extra_tests@
EXTRA_TESTS = opencl_test.sh polybench_test.sh
TEST_EXTENSIONS = .sh

BUILT_SOURCES = gitversion.h

CLEANFILES = gitversion.h

EXTRA_DIST = \
	examples \
	ocl_utilities.c \
	ocl_utilities.h \
	tests

dist-hook:
	echo @GIT_HEAD_VERSION@ > $(distdir)/GIT_HEAD_ID

gitversion.h: @GIT_HEAD@
	$(AM_V_GEN)echo '#define GIT_HEAD_ID "'@GIT_HEAD_VERSION@'"' > $@
