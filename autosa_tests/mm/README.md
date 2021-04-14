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
autosa_tests/mm/hls_script.tcl
```

__Command__:
To run the HLS flow for C/RTL simulation
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize --hls
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `hls_script.tcl` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mm/hls_script.tcl autosa.tmp/output/
```

Run the TCL script to build the HLS project.

```
cd autosa.tmp/output
vivado_hls -f hls_script.tcl
```

Alternatively, if you need to generate the bitstream for on-board testing, simply remove the `--hls` flag from the AutoSA command.
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize
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
make check
```

__Other Test Cases__:
Below we provide some other test cases for you to try out.
1. 1D systolic array
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[0];kernel[]->array_part[32,32,32];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize --hls
```

2. 2D systolic array
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[32,4,32];kernel[]->latency[16,16];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize --hls --local-reduce --reduce-op="+" --simd-touch-space
```
