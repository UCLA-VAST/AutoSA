#!/bin/sh

EXEEXT=
srcdir=.

for i in $srcdir/tests/*.c; do
	echo $i;
	(./pet$EXEEXT $i > test.scop &&
	 ./pet_scop_cmp$EXEEXT test.scop ${i%.c}.scop) || exit
done

for i in $srcdir/tests/autodetect/*.c; do
	echo $i;
	(./pet$EXEEXT --autodetect $i > test.scop &&
	 ./pet_scop_cmp$EXEEXT test.scop ${i%.c}.scop) || exit
done

for i in $srcdir/tests/encapsulate/*.c; do
	echo $i;
	(./pet$EXEEXT --encapsulate-dynamic-control $i > test.scop &&
	 ./pet_scop_cmp$EXEEXT test.scop ${i%.c}.scop) || exit
done

rm test.scop
