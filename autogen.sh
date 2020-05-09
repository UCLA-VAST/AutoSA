#!/bin/sh
## ISL
#if test -f src/isl/autogen.sh; then
#	(cd src/isl; ./autogen.sh)
#fi
## PET
#if test -f src/pet/autogen.sh; then
#	(cd src/pet; ./autogen.sh)
#fi
# AutoSA (including PPCG)
if test -f src/autogen.sh; then
  (cd src; ./autogen.sh)
autoreconf -i
