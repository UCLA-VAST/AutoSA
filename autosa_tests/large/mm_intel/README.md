# Matrix Multiplication (Large)

Board        | Software Version
-------------|-----------------
Stratix 10 | Intel FPGA SDK for OpenCL 19.4

__Files__:
```
autosa_tests/large/mm_intel/kernel.c
autosa_tests/large/mm_intel/kernel.h
autosa_tests/large/mm_intel/simd_info.json
autosa_tests/large/mm_intel/Makefile
```

__Command__:
```c
./autosa ./autosa_tests/large/mm_intel/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_opencl --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[260,256,512];kernel[]->latency[20,16];kernel[]->simd[8]}" --simd-info=./autosa_tests/large/mm_intel/simd_info.json --host-serialize --loop-infinitize --double-buffer-style=0 --mem-port-map="{kernel[]->A[0];kernel[]->B[1];kernel[]->C[2]}"
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/mm_intel/Makefile autosa.tmp/output/
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