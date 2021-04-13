AC_INIT([autosa], [0.02], [jiewang@cs.ucla.edu])
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG

# AX_CHECK_OPENMP
# AX_CHECK_OPENCL
# if test $HAVE_OPENCL = yes; then
# 	extra_tests="$extra_tests opencl_test.sh"
# fi

AX_SUBMODULE(isl,build|bundled|system,bundled)
AM_CONDITIONAL(BUNDLED_ISL, test $with_isl = bundled)
AM_CONDITIONAL(BUILD_ISL, test $with_isl = build)

AC_SUBST(ISL_CFLAGS)
AC_SUBST(ISL_LIBS)
AC_SUBST(ISL_SRCDIR)
AC_SUBST(ISL_BUILDDIR)
case "$with_isl" in
bundled)
	ISL_CFLAGS="-I\$(top_srcdir)/isl/include -I\$(top_builddir)/isl/include"
	ISL_CFLAGS="$ISL_CFLAGS"
  ISL_SRCDIR="$srcdir/isl"
  ISL_BUILDDIR=isl
	ppcg_configure_args="$ppcg_configure_args --with-isl-builddir=../isl"
	ppcg_configure_args="$ppcg_configure_args --with-isl=build"
	#ppcg_configure_args="$ppcg_configure_args --with-clang=system"
	ppcg_configure_args="$ppcg_configure_args --with-clang=no"
  PACKAGE_CFLAGS_ISL='-I${prefix}/include'
	;;
build)
  ISL_SRCDIR="$isl_srcdir"
	ISL_BUILDDIR=`echo @abs_builddir@ | $with_isl_builddir/config.status --file=-`
	ISL_CFLAGS="-I$isl_srcdir/include -I$ISL_BUILDDIR/include"
	ISL_CFLAGS="$ISL_CFLAGS"
	ISL_LIBS="$with_isl_builddir/libisl.la"
  PACKAGE_CFLAGS_ISL='-I${prefix}/include'
	;;
system)
	PKG_CHECK_MODULES([ISL], [isl])
  PACKAGE_CFLAGS_ISL="$ISL_CFLAGS"
  ;;
esac
AM_CONDITIONAL(HAVE_ISL_BUILDDIR, test "x$ISL_BUILDDIR" != "x")

AX_SUBMODULE(barvinok,bundled|system,bundled)
AM_CONDITIONAL(BUNDLED_BARVINOK, test $with_barvinok = bundled)
AM_CONDITIONAL(BUILD_BARVINOK, test $with_barvinok = build)

AC_SUBST(BARVINOK_CFLAGS)
AC_SUBST(BARVINOK_LIBS)
AC_SUBST(BARVINOK_SRCDIR)
AC_SUBST(BARVINOK_BUILDDIR)
case "$with_barvinok" in
bundled)
  BARVINOK_CFLAGS="$BARVINOK_CFLAGS -I\$(top_srcdir)/barvinok -I\$(top_builddir)/barvinok"
  BARVINOK_CFLAGS="$BARVINOK_CFLAGS"
  BARVINOK_SRCDIR="$srcdir/barvinok"
  BARVINOK_BUILDDIR=barvinok
  ;;
build)
  BARVINOK_SRCDIR="$barvinok_srcdir"
  BARVINOK_CFLAGS="$BARVINOK_CFLAGS"
  BARVINOK_BUILDDIR=`echo @abs_builddir@ | $with_BARVINOK_builddir/config.status --file=-`
  BARVINOK_CFLAGS="-I$barvinok_srcdir/ -I$BARVINOK_BUILDDIR/"
  BARVINOK_LIBS="$with_barvinok_builddir/libisl.la"
  ;;
system)
  PKG_CHECK_MODULES([BARVINOK], [barvinok])
  PACKAGE_CFLAGS_BARVINOK="$BARVINOK_CFLAGS"
  ;;
esac
AM_CONDITIONAL(HAVE_BARVINOK_BUILDDIR, test "x$BARVINOK_BUILDDIR" != "x")

AX_SUBMODULE(pet,bundled|system,bundled)
AM_CONDITIONAL(BUNDLED_PET, test $with_pet = bundled)
AM_CONDITIONAL(BUILD_PET, test $with_pet = build)

AC_SUBST(PET_CFLAGS)
AC_SUBST(PET_LIBS)
AC_SUBST(PET_BUILDDIR)
case "$with_pet" in
bundled)
	PET_CFLAGS="$PET_CFLAGS -I\$(top_srcdir)/pet/include"
	;;
build)
  PET_BUILDDIR=`echo @abs_builddir@ | $with_pet_builddir/config.status --file=-`
  PET_CFLAGS="-I$pet_srcdir/include"
  ;;
system)
	PKG_CHECK_MODULES([PET], [pet])
  PACKAGE_CFLAGS_PET="$PET_CFLAGS"
	;;
esac

# AC_SUBST(POLYBENCH_DIR)
# AC_SUBST(extra_tests)
# AC_ARG_WITH([polybench],
# 	[AS_HELP_STRING([--with-polybench=DIR], [PolyBench location])],
# 	[
# 	if test -f "$with_polybench/utilities/benchmark_list"; then
# 		POLYBENCH_DIR=$with_polybench
# 		extra_tests="$extra_tests polybench_test.sh"
# 	fi
# 	])

# AX_DETECT_GIT_HEAD

AC_CONFIG_FILES(Makefile)
# AC_CONFIG_FILES([polybench_test.sh], [chmod +x polybench_test.sh])
# AC_CONFIG_FILES([opencl_test.sh], [chmod +x opencl_test.sh])
if test $with_isl = bundled; then
	AC_CONFIG_SUBDIRS(isl)
fi
if test $with_barvinok = bundled; then
  AC_CONFIG_SUBDIRS(barvinok)
fi
if test $with_pet = bundled; then
	AC_CONFIG_SUBDIRS(pet)
fi
AC_CONFIG_COMMANDS_POST([
	dnl pass on arguments to subdir configures, but don't
	dnl add them to config.status
	ac_configure_args="$ac_configure_args $ppcg_configure_args"
])
AC_OUTPUT
