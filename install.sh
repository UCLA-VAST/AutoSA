#!/bin/sh
# Initialize ISL and PET
git submodule init
git submodule update
(cd src/isl; git submodule init imath; git submodule update imath)

# Install python packages
pip3 install -r requirements.txt

echo "Patch ISL"
# Patch ISL
cd ./autosa_scripts/ppcg_changes/isl
./isl_patch.sh
cd -

# Compilation
cd src
echo "autogen"
./autogen.sh
echo "configure"
./configure
echo "make"
make -j4
#make check

# Cleanup
cd - 
cp ./autosa_scripts/autosa.py ./autosa
