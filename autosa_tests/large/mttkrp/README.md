# Matricized Tensor Times Khatri-Rao Product (MTTKRP)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/mttkrp/kernel.c
autosa_tests/large/mttkrp/kernel.h
autosa_tests/large/mttkrp/simd_info.json
autosa_tests/large/mttkrp/Makefile
autosa_tests/large/mttkrp/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/large/mttkrp/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[128,128,2];kernel[]->latency[16,8];kernel[]->simd[8,1]}" --simd-info=./autosa_tests/large/mttkrp/simd_info.json --host-serialize
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/mttkrp/Makefile autosa.tmp/output/
cp autosa_tests/large/mttkrp/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```