# Matrix Multiplication with Block Sparsity (Large)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/large/mm_block_sparse/kernel.c
autosa_tests/large/mm_block_sparse/kernel.h
autosa_tests/large/mm_block_sparse/simd_info.json
autosa_tests/large/mm_block_sparse/Makefile
autosa_tests/large/mm_block_sparse/connectivity.cfg
autosa_tests/large/mm_block_sparse/hls_script.tcl
```

__Command__:
To run the HLS flow for C/RTL simulation
```bash
./autosa ./autosa_tests/large/mm_block_sparse/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[256,256,512];kernel[]->latency[32,32];kernel[]->simd[8]}" --simd-info=./autosa_tests/large/mm_block_sparse/simd_info.json --host-serialize --hls --block-sparse --block-sparse-ratio="{kernel[]->A[4,8]}"
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
./autosa ./autosa_tests/large/mm_block_sparse/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[256,256,512];kernel[]->latency[32,32];kernel[]->simd[8]}" --simd-info=./autosa_tests/mm_block_sparse/simd_info.json --host-serialize --block-sparse --block-sparse-ratio="{kernel[]->A[4,8]}"
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