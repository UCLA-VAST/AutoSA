if BUNDLED_ISL
    MAYBE_ISL = isl
    ISL_LA = $(top_builddir)/isl/libisl.la -lgmp
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

bin_PROGRAMS = ppcg
ppcg_SOURCES = \
	cpu.c \
	cpu.h \
	cuda.c \
	cuda.h \
	cuda_common.h \
	cuda_common.c \
	gpu.c \
	gpu.h \
	gpu_print.c \
	gpu_print.h \
	schedule.c \
	schedule.h \
	ppcg_options.c \
	ppcg_options.h \
	ppcg.c \
	ppcg.h \
	print.c \
	print.h \
	version.c

TESTS = @extra_tests@
EXTRA_TESTS = polybench_test.sh

dist-hook:
	echo @GIT_HEAD_VERSION@ > $(distdir)/GIT_HEAD_ID

gitversion.h: @GIT_HEAD@
	$(AM_V_GEN)echo '#define GIT_HEAD_ID "'@GIT_HEAD_VERSION@'"' > $@
