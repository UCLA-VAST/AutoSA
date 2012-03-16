if BUNDLED_ISL
    MAYBE_ISL = isl
    ISL_LA = $(top_builddir)/isl/libisl.la -lgmp
    LOCAL_ISL_LA = isl/libisl.la
endif
if BUNDLED_PET
    MAYBE_PET = pet
    PET_LA = $(top_builddir)/pet/libpet.la
endif
if BUNDLED_CLOOG
    MAYBE_CLOOG = cloog
    CLOOG_LA = $(top_builddir)/cloog/libcloog-isl.la
endif

SUBDIRS = $(MAYBE_ISL) $(MAYBE_PET) $(MAYBE_CLOOG) .

FORCE:
isl/libisl.la: FORCE
	cd isl; $(MAKE) $(AM_MAKEFLAGS) libisl.la
pet/libpet.la: FORCE
	cd pet; $(MAKE) $(AM_MAKEFLAGS) libpet.la
cloog/libcloog-isl.la: $(LOCAL_ISL_LA) FORCE
	cd cloog; $(MAKE) $(AM_MAKEFLAGS) libcloog-isl.la

ACLOCAL_AMFLAGS = -I m4

LIB_ISL = $(ISL_LA) @ISL_LIBS@
LIB_PET = $(PET_LA) @PET_LIBS@
LIB_CLOOG = $(CLOOG_LA) @CLOOG_LIBS@

AM_CPPFLAGS = @ISL_CFLAGS@ @PET_CFLAGS@ @CLOOG_CFLAGS@
LDADD = $(LIB_CLOOG) $(LIB_PET) $(LIB_ISL)

bin_PROGRAMS = ppcg
ppcg_SOURCES = \
	cuda.c \
	cuda.h \
	cuda_common.h \
	cuda_common.c \
	gpucode.c \
	gpucode.h \
	rewrite.c \
	rewrite.h \
	schedule.c \
	schedule.h \
	ppcg_options.c \
	ppcg_options.h \
	ppcg.c
