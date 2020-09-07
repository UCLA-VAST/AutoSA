# Matrix Multiplication (Small)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mm/kernel.c
autosa_tests/mm/kernel.h
autosa_tests/mm/simd_info.json
autosa_tests/mm/Makefile
autosa_tests/mm/connectivity.cfg
```

__Command__:
```bash
./autosa ./autosa_tests/mm/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --AutoSA-simd-info=./autosa_tests/mm/simd_info.json --AutoSA-host-serialize
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

__Tuning__:
Run this command to train the resource model.
```bash
python3 ./autosa_scripts/optimizer.py -c './autosa ./autosa_tests/mm/kernel.c --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-data-pack-sizes="{kernel[]->data_pack[8,32,64]}" --AutoSA-simd-info=./autosa_tests/mm/simd_info.json --AutoSA-host-serialize --AutoSA-hls' --info autosa_config/hw_info.json -s autosa_config/optimizer_settings.json --train -p xilinx
```

After resource models are trained, run the following command to search for the best design.
```bash
python3 ./autosa_scripts/optimizer.py -c './autosa ./autosa_tests/mm/kernel.c --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-data-pack-sizes="{kernel[]->data_pack[8,32,64]}" --AutoSA-simd-info=./autosa_tests/mm/simd_info.json --AutoSA-host-serialize --AutoSA-hls' --info autosa_config/hw_info.json -s autosa_config/optimizer_settings.json --search -p xilinx
```

__Other Test Cases__:
1. 1D systolic array
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --autosa --isl-schedule-whole-component --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[0];kernel[]->array_part[32,32,32];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize --hls
```
2. 2D systolic array
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --autosa --isl-schedule-whole-component --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[32,32,32]}" --simd-info=./autosa_tests/mm/simd_info.json --AutoSA-data-pack-sizes="{kernel[]->data_pack[8,32,64]}" --hls --local-reduce --reduce-op="+"
```
Tuning