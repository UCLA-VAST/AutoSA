# Convolutional Neural Network (Single Layer, Large)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/cnn/kernel.c
autosa_tests/large/cnn/kernel.h
autosa_tests/large/cnn/simd_info.json
autosa_tests/large/cnn/Makefile
autosa_tests/large/cnn/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/large/cnn/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[64,56,14,64];kernel[]->latency[4,4,7];kernel[]->simd[1,1,8]}" --AutoSA-simd-info=./autosa_tests/large/cnn/simd_info.json
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/cnn/Makefile autosa.tmp/output/
cp autosa_tests/large/cnn/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```