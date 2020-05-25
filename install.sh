#!/bin/sh
# Initialize ISL and PET
git submodule init
git submodule update
(cd src/isl; git submodule init imath; git submodule update imath)

# Install python packages
pip3.6 install -r requirements.txt

# Patch ISL
cd ./autosa_scripts/ppcg_changes/isl
./isl_patch.sh
cd -

# Compilation
cd src
#./autogen.sh
./configure
make
#make check

# Cleanup
cd - 
cp ./autosa_scripts/autosa.py ./autosa
