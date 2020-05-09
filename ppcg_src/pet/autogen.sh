#!/bin/sh
if test -f isl/autogen.sh; then
	(cd isl; ./autogen.sh)
fi
autoreconf -i
