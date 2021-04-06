HeteroCL Integration
====================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This page summarizes some issues when integrating AutoSA with HeteroCL.

Issue 1: Generating HCL-compatible outputs
------------------------------------------

To generate HCL-compatible code, we will need to add the flags ``--hcl --hls`` when compiling the program.
Below is the example command:

.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hcl \
    --hls

Issue 2: Generating kernels with AXI Stream interface
-----------------------------------------------------

To generate AXI Stream interface, we will need to enable host serialization and generate
the HLS host by adding the flag ``--axi-stream --hls --host-serialize``.
Below is the example command:

.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hcl \
    --axi-stream \
    --hls

Issue 3: Hanging kernels (pending)
----------------------------------

The 8x8 GEMM kernel without host serialization will hang on-board.
The kernel with host serialization can pass the on-board testing.
We are still debugging this issue.
The command for this design:

.. code:: bash

    ./autosa ./autosa_tests/large/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[256,256,512];kernel[]->latency[32,32];kernel[]->simd[8]}" \
    --simd-info=./autosa_tests/large/mm/simd_info.json \
    --hcl \
    --hls    