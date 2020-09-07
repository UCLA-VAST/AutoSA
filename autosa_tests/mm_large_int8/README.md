# Matrix Multiplication (Large)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mm_large/kernel.c
autosa_tests/mm_large/kernel.h
autosa_tests/mm_large/simd_info.json
autosa_tests/mm_large/Makefile
autosa_tests/mm_large/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/mm_large/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[256,128,256];kernel[]->array_part_L2[4,4,4];kernel[]->latency[32,16];kernel[]->simd[8]}" --AutoSA-simd-info=./autosa_tests/mm_large/simd_info.json --AutoSA-host-serialize
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mm_large/Makefile autosa.tmp/output/
cp autosa_tests/mm_large/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```

__Tuning__:
Run this command to train the resource model.
```bash
export AUTOSA_PATH=$(pwd)
python3 ./autosa_scripts/optimizer.py -c './autosa ./autosa_tests/mm_large/kernel.c --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-data-pack-sizes="{kernel[]->data_pack[8,32,64]}" --AutoSA-simd-info=./autosa_tests/mm_large/simd_info.json --AutoSA-host-serialize --AutoSA-hls' --info autosa_config/hw_info.json -s autosa_config/optimizer_settings.json --train -p xilinx
```

After resource models are trained, run the following command to search for the best design.
```bash
python3 ./autosa_scripts/optimizer.py -c './autosa ./autosa_tests/mm_large/kernel.c --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-data-pack-sizes="{kernel[]->data_pack[8,32,64]}" --AutoSA-simd-info=./autosa_tests/mm_large/simd_info.json --AutoSA-host-serialize --AutoSA-hls' --info autosa_config/hw_info.json -s autosa_config/optimizer_settings.json --search -p xilinx
```

__Other Test Cases__:
1. 1D systolic array
Tuning
```bash

```