# Matricized Tensor Times Khatri-Rao Product (MTTKRP)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U200 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mttkrp/kernel.c
autosa_tests/mttkrp/kernel.h
autosa_tests/mttkrp/simd_info.json
autosa_tests/mttkrp/Makefile
autosa_tests/mttkrp/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/mttkrp/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --AutoSA-two-level-buffer --AutoSA-uram --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[0]->array_part[12,512,16];kernel[0]->array_part_L2[1,1,32];kernel[0]->latency[2,64];kernel[0]->simd[8,-1]}" --AutoSA-simd-info=./autosa_tests/mttkrp/simd_info.json
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mttkrp/Makefile autosa.tmp/output/
cp autosa_tests/mttkrp/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```