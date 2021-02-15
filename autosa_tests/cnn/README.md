# Convolutional Neural Network (Single Layer, Small)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

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
./autosa ./autosa_tests/cnn/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[8,8,4,8];kernel[]->latency[4,2,4];kernel[]->simd[1,1,1,2]}" --simd-info=./autosa_tests/cnn/simd_info.json --host-serialize
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
