#!/bin/sh
if test -f ppcg_src/isl/autogen.sh; then
	(cd ppcg_src/isl; ./autogen.sh)
fi
if test -f ppcg_src/pet/autogen.sh; then
	(cd ppcg_src/pet; ./autogen.sh)
fi
autoreconf -i
