# Check if $CC supports openmp.
# While clang (3.5) accepts the -fopenmp flag, it produces bad code
# on some PolyBench benchmarks, so it is blacklisted.
AC_DEFUN([AX_CHECK_OPENMP], [
	AC_SUBST(HAVE_OPENMP)
	HAVE_OPENMP=no
	AC_MSG_CHECKING([for OpenMP support by $CC])
	echo | $CC -x c - -fsyntax-only -fopenmp -Werror >/dev/null 2>/dev/null
	if test $? -eq 0; then
		HAVE_OPENMP=yes
	fi
	AC_MSG_RESULT($HAVE_OPENMP)
	if test $HAVE_OPENMP = yes; then
		$CC --version | grep clang > /dev/null
		if test $? -eq 0; then
			AC_MSG_NOTICE([clang blacklisted])
			HAVE_OPENMP=no
		fi
	fi
])
