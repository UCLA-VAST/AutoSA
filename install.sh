#!/bin/sh
# Initialize ISL and PET
git submodule init
git submodule update
(cd src/isl; git submodule init imath; git submodule update imath)
(cd src/barvinok; ./get_submodules.sh)

# Install python packages
pip3 install -r requirements.txt

# Patch ISL
echo "Patch ISL"
(cd ./autosa_scripts/ppcg_changes/isl; ./isl_patch.sh)

# Compilation
(cd src; echo "autogen"; ./autogen.sh; echo "configure"; ./configure; echo "make"; make -j4)

# Cleanup 
cp ./autosa_scripts/autosa.py ./autosa
(mkdir autosa.tmp; cd autosa.tmp; mkdir output optimizer; cd output; mkdir src latency_est resource_est)
