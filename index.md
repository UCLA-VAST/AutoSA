## Introduction

This is a tutorial about how to use AutoSA, a polyhedral-based systolic array compiler on FPGA. Throughout this tutorial, we will show you how to compile a systolic array described in Xilinx HLS C. You could use Xilinx synthesis tools to synthesize the generated designs and map onto Xilinx FPGAs.

Before we start, please make sure you install AutoSA proper following the guidelines on [AutoSA Repo](https://github.com/UCLA-VAST/AutoSA). We have also provided a Docker image that you can use. Use the following command to pull the Docker image and run it directly.
```
docker pull whbldhwj/autosa:latest
```

## Example 1: Matrix Multiplication
In this example, we will show you the basic features of AutoSA that compiles a matrix multiplication kernel into a systolic array. You could also find the example [here](https://github.com/UCLA-VAST/AutoSA/tree/master/autosa_tests/mm).

Our input source code is [kernel.c](https://github.com/UCLA-VAST/AutoSA/blob/master/autosa_tests/mm/kernel.c). In this code, we use pragmas to annotate the code region to compile to systolic arrays.
```C
#pragma scop
for (int i = 0; i < I; i++)
  for (int j = 0; j < J; j++) {
    C[i][j] = 0;
    for (int k = 0; k < K; k++) 
      C[i][j] = C[i][j] + A[i][k] * B[j][k];
  }
#pragma endscop  
```

Then, run the following command to compile this code to a systolic array design.
```bash
./autosa ./autosa_tests/mm/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize --hls
```

You should be able to find the generated files in the directory `autosa.tmp/output/src`. The directory looks like
```
autosa.tmp
└───output
    └── src
        ├── kernel.c
        ├── kernel.h
        ├── kernel_host.cpp
        ├── kernel_kernel.cpp
        └── kernel_kernel.h
```

The files `kernel.c` and `kerne.h` are input files. The file `kernel_host.cpp` is the host file that prepares the data for the kernel, calls the kernel, and verifies the results. The files `kernel_kernel.cpp` and `kernel_kernel.h` contain the Xilinx HLS C code that describes the systolic array.

Now let's try to run the C simulation to verify the correctness of the generate design. Copy the `hls_script.tcl` from `${AUTOSA_ROOT}/autosa_scripts/hls_scripts` to `${AUTOSA_ROOT}/autosa.tmp/output/`.
```bash
cp autosa_scripts/hls_scripts/hls_script.tcl autosa.tmp/output/
cd autosa.tmp/output/
vivado_hls -f hls_script.tcl
```

If everything goes smoothly, you should be able to see the `Passed` message in your terminal. This indicates the C simulation finished successfully without any error.

### Any Questions
If you have any difficulties using AutoSA, please feel free to open an issue in the repo or send an e-mail to me (jiewang@cs.ucla.edu).
