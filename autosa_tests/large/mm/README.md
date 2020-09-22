# Matrix Multiplication (Large)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/mm/kernel.c
autosa_tests/large/mm/kernel.h
autosa_tests/large/mm/simd_info.json
autosa_tests/large/mm/Makefile
autosa_tests/large/mm/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/large/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[520,512,32];kernel[]->latency[40,32];kernel[]->simd[8]}" --AutoSA-simd-info=./autosa_tests/large/mm/simd_info.json --host-serialize
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/mm/Makefile autosa.tmp/output/
cp autosa_tests/large/mm/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```