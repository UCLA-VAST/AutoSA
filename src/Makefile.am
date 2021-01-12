if BUNDLED_ISL
    MAYBE_ISL = isl
    ISL_LA = $(top_builddir)/isl/libisl.la
    LOCAL_ISL_LA = isl/libisl.la
endif
if BUNDLED_BARVINOK
    MAYBE_BARVINOK = barvinok
    BARVINOK_LA = $(top_builddir)/barvinok/libbarvinok.la
endif 
if BUNDLED_PET
    MAYBE_PET = pet
    PET_LA = $(top_builddir)/pet/libpet.la
endif

SUBDIRS = $(MAYBE_ISL) $(MAYBE_BARVINOK) $(MAYBE_PET) .

FORCE:
isl/libisl.la: FORCE
	cd isl; $(MAKE) $(AM_MAKEFLAGS) libisl.la
barvinok/libbarvinok.la: FORCE
	cd barvinok; $(MAKE) $(AM_MAKEFLAGS) libbarvinok.la
pet/libpet.la: FORCE
	cd pet; $(MAKE) $(AM_MAKEFLAGS) libpet.la

ACLOCAL_AMFLAGS = -I m4

LIB_ISL = $(ISL_LA) @ISL_LIBS@
LIB_BARVINOK = $(BARVINOK_LA) @BARVINOK_LIBS@
LIB_PET = $(PET_LA) @PET_LIBS@

AM_CPPFLAGS = @ISL_CFLAGS@ @BARVINOK_CFLAGS@ @PET_CFLAGS@
LDADD = $(LIB_PET) $(LIB_ISL) $(LIB_BARVINOK)
AM_CXXFLAGS = -std=c++11

bin_PROGRAMS = autosa
autosa_SOURCES = \
	cpu.c \
	cpu.h \
	cuda.c \
	cuda.h \
	opencl.c \
	opencl.h \
	cuda_common.h \
	cuda_common.c \
	gpu.c \
	gpu.h \
	gpu_array_tile.c \
	gpu_array_tile.h \
	gpu_group.c \
	gpu_group.h \
	gpu_hybrid.c \
	gpu_hybrid.h \
	gpu_print.c \
	gpu_print.h \
	gpu_tree.c \
	gpu_tree.h \
	grouping.c \
	grouping.h \
	hybrid.c \
	hybrid.h \
	schedule.c \
	schedule.h \
	ppcg_options.c \
	ppcg_options.h \
	ppcg.c \
	ppcg.h \
	print.c \
	print.h \
	util.c \
	util.h \
	main.cpp \
	cJSON/cJSON.c \
	autosa_codegen.cpp \
	autosa_comm.cpp \
	autosa_common.cpp \
	autosa_cpu.cpp \
	autosa_intel_opencl.cpp \
	autosa_print.cpp \
	autosa_schedule_tree.cpp \
	autosa_t2s.cpp \
	autosa_trans.cpp \
	autosa_utils.cpp \
	autosa_xilinx_hls_c.cpp  \
	autosa_catapult_hls_c.cpp

#TESTS = @extra_tests@
#EXTRA_TESTS = opencl_test.sh polybench_test.sh
#TEST_EXTENSIONS = .sh

#BUILT_SOURCES = gitversion.h

#CLEANFILES = gitversion.h

#EXTRA_DIST = \
#	examples \
#	ocl_utilities.c \
#	ocl_utilities.h \
#	tests

#dist-hook:
#	echo @GIT_HEAD_VERSION@ > $(distdir)/GIT_HEAD_ID
#
#gitversion.h: @GIT_HEAD@
#	$(AM_V_GEN)echo '#define GIT_HEAD_ID "'@GIT_HEAD_VERSION@'"' > $@
