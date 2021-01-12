# Matrix Multiplication (Small)

Board        | Software Version
-------------|-----------------
N/A | Mentor Graphics Catapult Ultra 10.5c

__Files__:
```
autosa_tests/mm_catapult/kernel.c
autosa_tests/mm_catapult/kernel.h
autosa_tests/mm_catapult/simd_info.json
```

__Command__:
This project shows the example of using Catapult HLS to generate FPGA designs.

To generate the input code for Catapult HLS, use the command below.
```bash
./autosa ./autosa_tests/mm_catapult/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_catapult_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize
```

After compilation, you will find all generated files under the directory `autosa.tmp/output/src`.
Catapult HLS requires the GUI or TCL to perform the hardware optimization. AutoSA generates an example TCL flow named `kernel_directives.tcl` that can be found in the directory `autosa.tmp/output`.

There are several limitations for the current Catapult HLS flow.
1. Floating point is not supported. We currently supported unsigned short and unsigned int.
2. In order to achieve II=1, programmers need to provide additional dependence information in the TCL file.
3. To successfully pass the C simulation, Catapult HLS requires the use of guards for input fifos. At present, programmers are required to add the guards manually.

Catapult HLS will generate RTL which can be synthesized on the target FPGAs.