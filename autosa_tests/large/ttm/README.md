# Tensor Times Matrix (TTM)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/ttm/kernel.c
autosa_tests/large/ttm/kernel.h
autosa_tests/large/ttm/simd_info.json
autosa_tests/large/ttm/Makefile
autosa_tests/large/ttm/connectivity.cfg
```

__Command__:
```c
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`. Copy the `Makefile` and `connectivity.cfg` to the directory `autosa.tmp/output`.

```
cp autosa_tests/large/ttm/Makefile autosa.tmp/output/
cp autosa_tests/large/ttm/connectivity.cfg autosa.tmp/output/
```

Execute the makefile to build the design.

```
cd autosa.tmp/output
make all
```