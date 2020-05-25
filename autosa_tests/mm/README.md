# Matrix Multiplication (Small)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U200 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mm/kernel.c
autosa_tests/mm/kernel.h
autosa_tests/mm/simd_info.json
autosa_tests/mm/Makefile
autosa_tests/mm/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/mm/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --AutoSA-two-level-buffer --AutoSA-uram --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[0]->array_part[16,16,16];kernel[0]->array_part_L2[2,2,2];kernel[0]->latency[8,8];kernel[0]->simd[2]}" --AutoSA-simd-info=./autosa_tests/mm/simd_info.json
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mm/Makefile autosa.tmp/output/
cp autosa_tests/mm/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```