# Matrix Multiplication (HBM)

This is an example of small-size matrix multiplication using high-bandwidth memory (HBM).

Board        | Software Version
-------------|-----------------
Xilinx Alveo U280 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mm_hbm/kernel.c
autosa_tests/mm_hbm/kernel.h
autosa_tests/mm_hbm/simd_info.json
autosa_tests/mm_hbm/Makefile
autosa_tests/mm_hbm/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/mm_hbm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[32,32,32];kernel[]->latency[8,8];kernel[]->simd[2];kernel[]->hbm_A[2];kernel[]->hbm_B[2];kernel[]->hbm_C_drain[2]}" --simd-info=./autosa_tests/mm_hbm/simd_info.json --hbm
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mm_hbm/Makefile autosa.tmp/output/
cp autosa_tests/mm_hbm/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```