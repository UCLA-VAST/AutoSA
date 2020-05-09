#!/bin/sh
# Initialize ISL and PET
git submodule init
git submodule update
(cd src/isl; git submodule init imath; git submodule update imath)
# Patch ISL and PPCG files

# Compilation
cd src
./autogen.sh
./configure
make
#make check

cd - 
cp src/autosa ./
