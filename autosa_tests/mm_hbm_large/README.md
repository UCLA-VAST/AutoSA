# Matrix Multiplication (HBM)

This is an example of large-size matrix multiplication using High Bandwidth Memory (HBM).

Board        | Software Version
-------------|-----------------
Xilinx Alveo U280 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mm_hbm_large/kernel.c
autosa_tests/mm_hbm_large/kernel.h
autosa_tests/mm_hbm_large/simd_info.json
autosa_tests/mm_hbm_large/Makefile
autosa_tests/mm_hbm_large/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/mm_hbm_large/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --AutoSA-two-level-buffer --AutoSA-uram --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[0]->array_part[260,128,256];kernel[0]->array_part_L2[4,4,4];kernel[0]->latency[26,16];kernel[0]->simd[8];kernel[0]->hbm_A[2];kernel[0]->hbm_B[4];kernel[0]->hbm_C_drain[4]}" --AutoSA-simd-info=./autosa_tests/mm_hbm_large/simd_info.json --AutoSA-hbm 
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mm_hbm_large/Makefile autosa.tmp/output/
cp autosa_tests/mm_hbm_large/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```

__Performance__:
LUT             | FF              | BRAM         | URAM         | DSP           
----------------|-----------------|--------------|--------------|----------------
485194 (37.24%) | 773071 (29.67%) | 608 (30.16%) | 112 (11.67%) | 3204 (35.51%) 

MHz | Kernel Runtime(s) | GFLOPs
----|-------------------|-------
271 | 0.00859206        | 253.8
