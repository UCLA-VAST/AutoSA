# Chain of Tensor-matrix multiplications (TTMc)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/ttmc/kernel.c
autosa_tests/large/ttmc/kernel.h
autosa_tests/large/ttmc/simd_info.json
autosa_tests/large/ttmc/Makefile
autosa_tests/large/ttmc/connectivity.cfg
```

__Command__:
```c
./autosa ./autosa_tests/large/ttmc/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[16,64,16,32];kernel[]->latency[1,8,8];kernel[]->simd[8,1]}" --simd-info=./autosa_tests/large/ttmc/simd_info.json --host-serialize
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/ttmc/Makefile autosa.tmp/output/
cp autosa_tests/large/ttmc/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```