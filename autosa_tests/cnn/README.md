# Convolutional Neural Network (Single Layer)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U200 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/cnn/kernel.c
autosa_tests/cnn/kernel.h
autosa_tests/cnn/simd_info.json
autosa_tests/cnn/Makefile
autosa_tests/cnn/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/cnn/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --AutoSA-two-level-buffer --AutoSA-uram --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[0]->array_part[64,60,14,64];kernel[0]->array_part_L2[1,1,1,8];kernel[0]->latency[8,6,7];kernel[0]->simd[-1,-1,8]}" --AutoSA-simd-info=./autosa_tests/cnn/simd_info.json
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/cnn/Makefile autosa.tmp/output/
cp autosa_tests/cnn/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```