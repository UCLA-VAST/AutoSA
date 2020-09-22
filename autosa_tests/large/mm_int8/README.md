# Matrix Multiplication in int8 (Large)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/mm_int8/kernel.c
autosa_tests/large/mm_int8/kernel.h
autosa_tests/large/mm_int8/simd_info.json
autosa_tests/large/mm_int8/Makefile
autosa_tests/large/mm_int8/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/large/mm_int8/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[264,256,64];kernel[]->latency[11,32];kernel[]->simd[64]}" --simd-info=./autosa_tests/large/mm_int8/simd_info.json --host-serialize --data-pack-sizes="{kernel[]->data_pack[32,32,64]}"
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/mm_int8/Makefile autosa.tmp/output/
cp autosa_tests/large/mm_int8/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```