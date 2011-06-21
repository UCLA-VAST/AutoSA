if BUNDLED_ISL
    MAYBE_ISL = isl
    ISL_LA = $(top_builddir)/isl/libisl.la
    LOCAL_ISL_LA = isl/libisl.la
endif
if BUNDLED_CLOOG
    MAYBE_CLOOG = cloog
    CLOOG_LA = $(top_builddir)/cloog/libcloog-isl.la
endif
if BUNDLED_CLAN
    MAYBE_CLAN = clan
    CLAN_LA = \
	$(top_builddir)/clan/source/libclan.la \
	$(top_builddir)/clan/scoplib/scoplib-0.2.0/source/libscoplib.la
endif

SUBDIRS = $(MAYBE_ISL) $(MAYBE_CLOOG) $(MAYBE_CLAN) .

FORCE:
isl/libisl.la: FORCE
	cd isl; $(MAKE) $(AM_MAKEFLAGS) libisl.la
cloog/libcloog-isl.la: $(LOCAL_ISL_LA) FORCE
	cd cloog; $(MAKE) $(AM_MAKEFLAGS) libcloog-isl.la
clan/source/libclan.la: FORCE
	cd clan/source; $(MAKE) $(AM_MAKEFLAGS) libclan.la
clan/scoplib/scoplib-0.2.0/source/libscoplib.la: FORCE
	cd clan/scoplib/scoplib-0.2.0/source; $(MAKE) $(AM_MAKEFLAGS) libscoplib.la

ACLOCAL_AMFLAGS = -I m4

LIB_ISL = $(ISL_LA) @ISL_LIBS@
LIB_CLOOG = $(CLOOG_LA) @CLOOG_LIBS@
LIB_CLAN = $(CLAN_LA) @CLAN_LIBS@

AM_CPPFLAGS = @ISL_CFLAGS@ @CLOOG_CFLAGS@ @CLAN_CFLAGS@
LDADD = $(LIB_CLOOG) $(LIB_ISL) $(LIB_CLAN)

bin_PROGRAMS = ppcg
ppcg_SOURCES = \
	cuda.c \
	cuda.h \
	cuda_common.h \
	cuda_common.c \
	gpucode.c \
	gpucode.h \
	schedule.c \
	schedule.h \
	scoplib_isl.c \
	scoplib_isl.h \
	ppcg_options.c \
	ppcg_options.h \
	ppcg.c
