#!/bin/sh
autoreconf -i
if test -f isl/autogen.sh; then
	(cd isl; ./autogen.sh)
fi
if test -f barvinok/autogen.sh; then
  (cd barvinok; ./autogen.sh)
fi
if test -f pet/autogen.sh; then
	(cd pet; ./autogen.sh)
fi
