# Check if $CC supports openmp.
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
		SAVE_CFLAGS=$CFLAGS
		CFLAGS="$CFLAGS -fopenmp"
		# Using some version of clang, the value of "m" becomes zero
		# after the parallel for loop.
		AC_RUN_IFELSE([AC_LANG_PROGRAM([[
		#include <stdlib.h>

		static void f(int m, double A[m])
		{
			#pragma omp parallel for
			for (int c0 = 0; c0 < m; c0 += 1)
				A[c0] = 0.;
			if (m != 100)
				abort();
		}
		]],[[
		double A[100];

		f(100, A);
		]])],[],[
			AC_MSG_NOTICE([OpenMP support broken, disabling])
			HAVE_OPENMP=no
		],[])
		CFLAGS=$SAVE_CFLAGS
	fi
])
