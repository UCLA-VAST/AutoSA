# Matrix Multiplication (Small)

Board        | Software Version
-------------|-----------------
Stratix 10 | Intel FPGA SDK for OpenCL 19.4

__Files__:
```
autosa_tests/mm_hcl_intel/kernel.c
autosa_tests/mm_hcl_intel/kernel.h
autosa_tests/mm_hcl_intel/simd_info.json
autosa_tests/mm_hcl_intel/Makefile
```

__Command__:
This is an internal test example for HeteroCL integration.

## Example 1

```c
./autosa ./autosa_tests/mm_hcl_intel/kernel.c \
--config=./autosa_config/autosa_config.json \
--target=autosa_opencl \
--output-dir=./autosa.tmp/output \
--sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" \
--simd-info=./autosa_tests/mm_hcl_intel/simd_info.json \
--host-serialize \
--loop-infinitize \
--double-buffer-style=0 \
--mem-port-map="{kernel[]->A[0];kernel[]->B[1];kernel[]->C[2]}" \
--hcl
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` to the directory `autosa.tmp/output`.

```
cp autosa_tests/mm/Makefile autosa.tmp/output/
```

Execute the makefile to perform software emulation
```
make sw_emu_check
```
or synthesize the design to RTL
```
make hls
```
or generate the bitstream
```
make hw
```

## Example 2

```c
./autosa ./autosa_tests/mm_hcl_intel/kernel2.c \
--config=./autosa_config/autosa_config.json \
--target=autosa_opencl \
--output-dir=./autosa.tmp/output \
--sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[32,32,512];kernel[]->
latency[8,8];kernel[]->simd[1]}" \
--simd-info=./autosa_tests/mm_hcl_intel/simd_info.json \
--host-serialize \
--loop-infinitize \
--double-buffer-style=0 \
--hcl
```