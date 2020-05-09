AC_INIT([pet], [0.11.3], [isl-development@googlegroups.com])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
AC_SUBST(versioninfo)
versioninfo=10:3:0

AC_PROG_CC
AC_PROG_CXX
AC_PROG_GREP
AC_PROG_LIBTOOL
AC_PROG_SED

AX_DETECT_CLANG

AX_SUBMODULE(isl,build|bundled|system,bundled)
AM_CONDITIONAL(BUNDLED_ISL, test $with_isl = bundled)

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
	;;
build)
	ISL_SRCDIR="$isl_srcdir"
	ISL_BUILDDIR=`echo @abs_builddir@ | $with_isl_builddir/config.status --file=-`
	ISL_CFLAGS="-I$isl_srcdir/include -I$ISL_BUILDDIR/include"
	ISL_CFLAGS="$ISL_CFLAGS"
	ISL_LIBS="$with_isl_builddir/libisl.la"
	;;
system)
	PKG_CHECK_MODULES([ISL], [isl])
	PACKAGE_CFLAGS_ISL="$ISL_CFLAGS"
esac
AM_CONDITIONAL(HAVE_ISL_BUILDDIR, test "x$ISL_BUILDDIR" != "x")

AX_SUBMODULE(libyaml,system|no,system)
AC_SUBST(LIBYAML_CPPFLAGS)
AC_SUBST(LIBYAML_LDFLAGS)
if test "x$with_libyaml_prefix" != "x"; then
	LIBYAML_CPPFLAGS="-I$with_libyaml_prefix/include"
fi
if test "x$with_libyaml_exec_prefix" != "x"; then
	LIBYAML_LDFLAGS="-L$with_libyaml_exec_prefix/lib"
fi

if test "$with_libyaml" = "system"; then
	SAVE_CPPFLAGS=$CPPFLAGS
	CPPFLAGS="$CPPFLAGS $LIBYAML_CPPFLAGS"
	AC_CHECK_HEADER([yaml.h],[],[with_libyaml="no"])
	CPPFLAGS="$SAVE_CPPFLAGS"
fi

AC_SUBST(extra_bin_programs)
AC_SUBST(extra_noinst_programs)
AC_SUBST(extra_tests)

if test "$with_libyaml" != "no"; then
	extra_bin_programs="pet"
	extra_noinst_programs="pet_scop_cmp"
	extra_tests="pet_test.sh"
fi
if test "$with_isl" != "system"; then
	extra_tests="$extra_tests codegen_test.sh"
fi

PACKAGE_CFLAGS="$PACKAGE_CFLAGS_ISL"
PACKAGE_LIBS="-lpet -lisl"
AX_CREATE_PKGCONFIG_INFO

AX_DETECT_GIT_HEAD

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES(Makefile)
AC_CONFIG_FILES([pet_test.sh], [chmod +x pet_test.sh])
AC_CONFIG_FILES([codegen_test.sh], [chmod +x codegen_test.sh])
AC_CONFIG_FILES(all.c)
if test $with_isl = bundled; then
	AC_CONFIG_SUBDIRS(isl)
fi
AC_CONFIG_COMMANDS_POST([
	dnl pass on arguments to subdir configures, but don't
	dnl add them to config.status
	ac_configure_args="$ac_configure_args --with-clang=system"
])
AC_OUTPUT
