# Tensor Times Matrix (TTM)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U200 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/ttm/kernel.c
autosa_tests/ttm/kernel.h
autosa_tests/ttm/simd_info.json
autosa_tests/ttm/Makefile
autosa_tests/ttm/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/ttm/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --AutoSA-two-level-buffer --AutoSA-uram --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[0]->array_part[20,256,4,128];kernel[0]->array_part_L2[13,2,16,4];kernel[0]->latency[2,32,2];kernel[0]->simd[8]}" --AutoSA-simd-info=./autosa_tests/ttm/simd_info.json
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/ttm/Makefile autosa.tmp/output/
cp autosa_tests/ttm/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```