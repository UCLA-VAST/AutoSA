Matrix Multiplication in int8 (Large)
=====================================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This is an example of large-size matrix multiplication in int8.
The design files can be found at ``${AUTOSA_ROOT}/autosa_tests/large/mm_int8``.
The testing environment is summarized in the table below.

+--------------------------+-----------------------------------------------+
| **Target FPGA**          | Xilinx Alveo U250                             |
+--------------------------+-----------------------------------------------+
| **FPGA Synthesis Tools** | Xilinx Vivado HLS 2019.2, Xilinx Vitis 2019.2 |
+--------------------------+-----------------------------------------------+
| **CPU**                  | Intel(R) Xeon(R) CPU E5-2699 v3 @ 2.30GHz     |
+--------------------------+-----------------------------------------------+

C Simulation
------------

Run the following example command to generate one design with HLS host code.

.. code:: bash

    ./autosa ./autosa_tests/large/mm_int8/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[264,256,64];kernel[]->latency[11,32];kernel[]->simd[64]}" \
    --simd-info=./autosa_tests/large/mm_int8/simd_info.json \
    --host-serialize \
    --data-pack-sizes="{kernel[]->A[32,32,64];kernel[]->B[32,32,64];kernel[]->C[32,32,64]}" \
    --no-isl-sink \
    --hls

After compilation, you will find all generated files under the directory 
``${AUTOSA_ROOT}/autosa.tmp/output/src``. 
Copy the ``hls_script.tcl`` to the directory ``autosa.tmp/output``.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/large/mm_int8/hls_script.tcl ${AUTOSA_ROOT}/autosa.tmp/output/

Run the TCL script to perform C simulation.

.. code:: bash

    cd ${AUTOSA_ROOT}/autosa.tmp/output/
    vivado_hls -f hls_script.tcl

You should see ``Passed`` printed out in your terminal showing that 
C simulation is performed successfully.   

Bitstream Generation
--------------------

If you need to generate the bitstream for on-board testing, simply remove the ``--hls``
flag from the previous AutoSA command.

.. code:: bash

    ./autosa ./autosa_tests/large/mm_int8/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[264,256,64];kernel[]->latency[11,32];kernel[]->simd[64]}" \
    --simd-info=./autosa_tests/large/mm_int8/simd_info.json \
    --host-serialize \
    --data-pack-sizes="{kernel[]->A[32,32,64];kernel[]->B[32,32,64];kernel[]->C[32,32,64]}" \
    --no-isl-sink

Now instead of HLS host code, an OpenCL host code is generated.   

As for int8, we notice that the default coding style for reduction trees in Xilinx HLS C 
will lead to inferior performance.
The default coding style is as below:

.. code:: c

    for (ap_uint<7> c8 = 0; c8 <= 63; c8 += 1) {
    #pragma HLS UNROLL
      local_C[c7][c6] = (local_C[c7][c6] + (local_A[0][c8] * local_B[0][c8]));
    }

If we synthesize the default PE using Vitis, each MAC is maped to one DSP and we get 64 DSPs for this 
reduction tree. 

Alternatively, if we manually unroll the reduction tree, using the following coding style,
only 32 DSPs are generated.

.. code:: c

    data_t mul_5_0_0 = local_A[0][0] * local_B[0][0];
    data_t add_5_0 = mul_5_0_0 + local_A[0][1] * local_B[0][1];
    data_t mul_5_1_0 = local_A[0][2] * local_B[0][2];
    data_t add_5_1 = mul_5_1_0 + local_A[0][3] * local_B[0][3];
    ...
    #pragma HLS RESOURCE variable=mul_5_0_0 core=Mul_LUT
    #pragma HLS RESOURCE variable=mul_5_1_0 core=Mul_LUT
    ...
    local_C[c7][c6] += add_0_0;

As you may notice, we map half the multipliers to LUTs instead. 
This helps to balance the resource usage of this design and enables us to place more 
PEs on-chip.

This part can't be done automatically at present, we provide a simple Python script 
to generate this code, and the user will have to replace the code manually in the design code.

As an example, find the script at ``${AUTOSA_ROOT}/autosa_tests/large/mm_int8/unroll.py``.
Modify the parameter ``UNROLL_FACTOR`` and ``DATA_T`` according to your current design.
Then, run:

.. code:: bash

    python3 unroll.py | tee code.c

Now copy the code in ``code.c`` to replace the original reduction loop in ``kernel_kernel.c``.
We have also provided an example file at ``${AUTOSA_ROOT}/autosa_tests/large/mm_int8/kernel_kernel_opt.cpp``.

Now you may follow the normal flow to compile the design.
We have prepared a template Makefile for Xilinx Vitis tools.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/large/mm_int8/Makefile ${AUTOSA_ROOT}/autosa.tmp/output/
    cp ${AUTOSA_ROOT}/autosa_tests/large/mm_int8/connectivity.cfg ${AUTOSA_ROOT}/autosa.tmp/output/

Set the proper ``PLATFORM`` in the Makefile. 
By default, we set it to ``xilinx_u250_xdma_201830_2``.
You may notice that we also copy a file ``connectivity.cfg`` here.
This file assigns the DDR bank mapping for the design. 
By default, we map pointers A, B, C to DDR bank 0, 1, 3.
Lastly, modify the ``MODE`` in the Makefile for performing different tasks.

* ``sw_emu``: C simulation
* ``hw_emu``: RTL simulation
* ``hw``: Bitstream generation

.. note:: 

    When using Vitis flow to perform RTL simulation, nothing needs to change in the source code.
    You may directly set the ``MODE`` to ``hw_emu`` and perform RTL simulation.
    However, by default, we will run the kernel 10 times to collect the average runtime.
    This may significantly prolong the simulation time. Consider reducing the kernel
    launching times to 1 before using RTL simulation.

To generate the bitstream, set the ``MODE`` to ``hw`` and use the command below.

.. code:: bash

    make all

After the bitstream is generated,
use the following command to run it on-board.    

.. code:: bash

    make check

Below is the resource and frequency information we collected for this design.

+-----+-----------------+------------------+--------------+---------------+
| MHz | LUT             | REG              | BRAM         | DSP           |
+-----+-----------------+------------------+--------------+---------------+
| 136 | 653369 (42.80%) | 704056 (22.34%)  | 1364 (58.39%)| 6144 (50.05%) |
+-----+-----------------+------------------+--------------+---------------+

You could also test the generated design on board. We have listed the performance of the design 
in the table below.

+-----------------+---------------+---------+
| Kernel Time (s) | Host Time (s) | TOPs    |
+-----------------+---------------+---------+
| 0.000759123     | 0.0103696     | 2.917   |
+-----------------+---------------+---------+   

Using AutoBridge to Boost Frequency
-----------------------------------

You may also try to use `AutoBridge <https://github.com/Licheng-Guo/AutoBridge>`_ 
to boost the design frequency.
We cover how to use AutoBridge to improve the frequency in :ref:`use-autobridge-label`.

The tables below show the detailed comparison results between the original design 
(unoptimized) and the design optimized with AutoBridge (optimized).

+-------------+-----+-----------------+------------------+--------------+---------------+
| Designs     | MHz | LUT             | REG              | BRAM         | DSP           |
+-------------+-----+-----------------+------------------+--------------+---------------+
| Unoptimized | 136 | 653369 (42.80%) | 704056 (22.34%)  | 1364 (58.39%)| 6144 (50.05%) |
+-------------+-----+-----------------+------------------+--------------+---------------+
| Optimized   | 300 | 730647 (47.87%) | 786680 (24.96%)  | 1364 (58.39%)| 6144 (50.05%) |
+-------------+-----+-----------------+------------------+--------------+---------------+

+-------------+-----------------+---------------+---------+
| Designs     | Kernel Time (s) | Host Time (s) | TOPs    |
+-------------+-----------------+---------------+---------+
| Unoptimized | 0.000759123     | 0.0103696     | 2.917   |
+-------------+-----------------+---------------+---------+
| Optimized   | 0.000302619     | 0.00532768    | 7.318   |
+-------------+-----------------+---------------+---------+