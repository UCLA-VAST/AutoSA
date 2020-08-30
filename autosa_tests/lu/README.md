# Matrix Multiplication (Small)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/lu/kernel.c
autosa_tests/lu/kernel.h
autosa_tests/lu/simd_info.json
autosa_tests/lu/Makefile
autosa_tests/lu/connectivity.cfg
```

__Command__:
```bash
./autosa ./autosa_tests/lu/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --AutoSA-hls --AutoSA-double-buffer-style=1 --no-reschedule --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[-1,-1,-1];kernel[]->latency[]}" --AutoSA-simd-info=./autosa_tests/lu/simd_info.json --AutoSA-int-io-dir=1 --no-AutoSA-data-pack
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