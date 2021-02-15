# Matrix Multiplication in int16 (Large)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/mm_int16/kernel.c
autosa_tests/large/mm_int16/kernel.h
autosa_tests/large/mm_int16/simd_info.json
autosa_tests/large/mm_int16/Makefile
autosa_tests/large/mm_int16/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/large/mm_int16/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[256,256,32];kernel[]->latency[16,16];kernel[]->simd[32]}" --simd-info=./autosa_tests/large/mm_int16/simd_info.json --host-serialize --data-pack-sizes="{kernel[]->A[32,32,64];kernel[]->B[32,32,64];kernel[]->C[32,32,64]}"
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/mm_int16/Makefile autosa.tmp/output/
cp autosa_tests/large/mm_int16/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```