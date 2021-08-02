# LU Decomposition (Small)

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
./autosa ./autosa_tests/lu/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[-1,-1,-1];kernel[]->latency[]}" --simd-info=./autosa_tests/lu/simd_info.json --use-cplusplus-template --no-reschedule --live-range-reordering
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/lu/Makefile autosa.tmp/output/
cp autosa_tests/lu/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```