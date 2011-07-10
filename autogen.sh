#!/bin/sh
if test -f isl/autogen.sh; then
	(cd isl; ./autogen.sh)
fi
if test -f pet/autogen.sh; then
	(cd pet; ./autogen.sh)
fi
if test -f cloog/autogen.sh; then
	(cd cloog; ./autogen.sh)
fi
(cd clan; autoreconf -i)
autoreconf -i
