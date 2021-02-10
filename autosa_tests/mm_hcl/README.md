# Matrix Multiplication (Small)

Board        | Software Version
-------------|-----------------
Xilinx Alveo U250 | Xilinx Vitis 2019.2

__Files__:
```
autosa_tests/mm_hcl/kernel.c
autosa_tests/mm_hcl/kernel.h
autosa_tests/mm_hcl/simd_info.json
autosa_tests/mm_hcl/hls_script.tcl
```

__Command__:
This is an internal test example for HeteroCL integration.

## Transposition

First, HeteroCL might provide AutoSA with transposed input matrices. We consider four test cases here.

1. A_B: Both input matrices A and B keep the row major.

Set `TRANS` to `A_B` in `kernel.c`.
Use the following command to compile the program.
```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl
```

The generated files can be found under `autosa.tmp/output`.
You may verify the design using Xilinx HLS.

```bash
cp ./autosa_tests/mm_hcl/hls_script.tcl ./autosa.tmp/output/
cd ./autosa.tmp/output
vivado_hls -f hls_script.tcl
```

You may notice here that we didn't use SIMD vectorization. The reason is that by default AutoSA will only examine the time loops (loops not mapped to the PE dimensions, aka, space loops). In this example, only loop k is available.
However, with the default layout `A[i][k]`, `B[k][j]`, and `C[i][j]`, as k is not the last-varying dimension of matrix B, it can't be used for vectorization.

To enable vectorization, we could enable AutoSA to use space loops as candidates as well. In this example, loop j can be used for vectorization.
Note that loop j is invariant to `A[i][k]` and leads to stride-one access for `B[k][j]`. However, before using this loop as the vectorization loop, we have to 
turn off the latency hiding optimization on loop j. The reason is that 
the loop j is tiled for latency hiding before vectorization, the remaining tiled loop is no longer consecutive as it is now mapped to hyper tiles. And therefore, the array access `B[k][j]` is no longer coalesced under this loop and 
SIMD vectorization opportunity is lost. 

To make use of SIMD vectorization, use the following command.
```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,1]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --simd-touch-space
```

We add `--simd-touch-space` to consider space loops as well for vectorization. To use loop j for vectorization, we set latency tiling factors to `[8,1]` which means that only loop i is tiled for latency hiding. AutoSA will dump out the possible loops for SIMD vectorization. Take a look at the file `tuning.json` under the directory `autosa.tmp/output`

```json
"simd": {
    "tilable_loops": [16,16],
    "scores": [13,13],
    "legal": [1,0]
}
```

AutoSA identifies two candidate loops. The first loop is the loop j, and the second loop is the loop k.
However, layout transformation is required for loop k.
Therefore, the `legal` value is set to 0 for the second loop.

Now to apply SIMD vectorization, use the following command.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,1];kernel[]->simd[8,1]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --simd-touch-space
```

A complete design with loop j vectorized is generated now.

2. AT_B: The input matrix A is transposed to column major, and the matrix B keeps the column major.

Set `TRANS` to `AT_B` in `kernel.c`.
Use the following command to compile the program.
```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl
```

To enable SIMD vectorization, let's take a look at the array accesses `A[k][i]`, `B[k][j]`, and `C[i][j]`.
In this case, loop j can be used for vectorization as long as it is avoided during the latency hiding. 
Use the following command to only tile loop i for latency hiding.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,1]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --simd-touch-space
```

Similarly you may check `tuning.json` for more detailed information. Finally use the command below to generated a vectorized design.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,1];kernel[]->simd[8,1]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --simd-touch-space
```

3. A_BT: The input matrix A remains the row major, and the matrix B is transposed to column major.

Set `TRANS` to `A_BT` in `kernel.c`.
Run the following command first.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl
```

In this case, AutoSA already detects a SIMD candidate loop k and will stop.
Array accesses in the current layout are `A[i][k]`, `B[j][k]`, and `C[i][j]`. Therefore, loop k can be used as the SIMD loop. Let's set the SIMD factor to 8 by using the following command to generate a complete design.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl
```

4. AT_BT: Both matrix A and B are transposed to column major.

Set `TRANS` to `AT_BT` in `kernel.c`.

Run the following command first.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl
```

An unvectorized design is generated.
Array accesses in the current layout are `A[k][i]`, `B[j][k]`, and `C[i][j]`. In this case, none of the loops can be used for vectorization.

In conclusion, when matrix A and B are supplied to AutoSA with different layouts, there are different rules to consider to enable full optimization (specifically, SIMD vectorization). We summarize these rules below.

| Layout |     Latency Hiding     |         SIMD        |  Compilation Flag  |
|:------:|:----------------------:|:-------------------:|:------------------:|
|   A_B  | kernel[]->latency[X,1] | kernel[]->simd[X,1] | --simd-touch-space |
|  AT_B  | kernel[]->latency[X,1] | kernel[]->simd[X,1] | --simd-touch-space |
|  A_BT  | kernel[]->latency[X,X] |  kernel[]->simd[X]  |                    |
|  AT_BT | kernel[]->latency[X,X] |         N/A         |                    |

## Data Packing

In additional to transposition, HeteroCL could also supply AutoSA with pre-packed array. 
By default, AutoSA will try to pack data as much as possible for each array to improve the effective DRAM bandwidth.
The data packing factors can be restrained by using the argument `--data-pack-sizes`. 
For each array, AutoSA allows users to restrain the data packing factors at three levels:

- Innermost level: Data packing factors for L1 I/O modules.
- Outermost level: Data packing factors for I/O modules accessing the DRAM.
- Intermediate level: Data packing factors for I/O modules except L1 or outermost I/O modules.

To restrain any data packing factors in the program. Specify it using the following format.

```bash
--data-pack-sizes="{kernel[]->A[8,32,64]}"
```

Using the above commands, we retrain the innermost level data packing factors to be no greater than 8 bytes (64 bits), 
the intermediate level to be no greater than 32 bytes (256 bits), and the outermost level to be no greater than 64 bytes (512 bits).
Due to the limitation of Xilinx devices, we require the outermost data packing factors to be no greater than 512 bits. 
In addition, as a rule of thumb, we recommend to limit the intermediate level no greater than 256 bits to restrain the FIFO overheads.

Set `TRANS` to `A_BT` in `kernel.c`.
Use the following command to compile the design.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --data-pack-sizes="{kernel[]->A[8,32,64];kernel[]->B[8,32,64];kernel[]->C[8,32,64]}"
```

Now let's take a look at the generated code.
At the top-level function `void autosa_func(A_t16 *A, B_t16 *B, C_t8 *C)`, we have array A packed with 16 elements (512 bits), array B packed with 16 elements (512 bits), and array C packed with 8 elements (256 bits). Although we have specified the maximal outermost packing factor to be 512 bits for each array, only array A and B achieved the maximal packing factor.

For array `C[I][J]`, as we partitoned the whole systolic array with factors `[16,16,16]`, each time the systolic array computes a tile of `C[16][16]`. Furthermore, as this tile is partitioned to be computed in a `2x2` array, each PE generates a sub-tile of `C[8][8]`. Therefore, when draining out the results, we transfer out the data in the size of sub-tile `C[8][8]`. The maximal data packing factor that we can achieve is 8.

If programmers hope to have a larger data packing factor for array C as well, there are two options to consider:

- Use host data serialization. 
- Partition a larger tile inside each PE.

Host serialization requires layout transformation on the host side which makes it difficult to integrate with the existing HeteroCL environment.

The command below shows an example of using a larger latency hiding factor to allocate a larger tile inside each PE.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,32,16];kernel[]->latency[8,16];kernel[]->simd[8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --data-pack-sizes="{kernel[]->A[8,32,64];kernel[]->B[8,32,64];kernel[]->C[8,32,64]}"
```

You can check the generated the source code and find that we have successuflly packed all arrays to 16 elements each.

The last thing to mention is that in the current flow we prioritize SIMD vectorization factors to user-specified data packing factors. In this example, as we specify the SIMD factor to be 8, array A and B will be packed with 8 elements at least. As an example, if running the following commmand which tries to restrain the data packing factors of A and B to 4 elements (16 bytes), AutoSA will ignore this constraint and pack A and B with 8 elements, only array C will be packed with 4 elements.

```bash
./autosa ./autosa_tests/mm_hcl/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,32,16];kernel[]->latency[8,16];kernel[]->simd[8]}" --simd-info=./autosa_tests/mm_hcl/simd_info.json --hls --hcl --data-pack-sizes="{kernel[]->A[8,16,16];kernel[]->B[8,16,16];kernel[]->C[8,16,16]}"
```