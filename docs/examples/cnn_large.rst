Convolutional Neural Network (Single Layer, Large)
==================================================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This is an example of large-size matrix multiplication.
The design files can be found at ``${AUTOSA_ROOT}/autosa_tests/large/cnn``.
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

    ./autosa ./autosa_tests/large/cnn/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[64,56,14,64];kernel[]->latency[4,4,7];kernel[]->simd[1,1,8]}" \
    --simd-info=./autosa_tests/large/cnn/simd_info.json \
    --host-serialize \
    --no-reverse-order \
    --hls

After compilation, you will find all generated files under the directory 
``${AUTOSA_ROOT}/autosa.tmp/output/src``. 
Copy the ``hls_script.tcl`` to the directory ``autosa.tmp/output``.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/large/cnn/hls_script.tcl ${AUTOSA_ROOT}/autosa.tmp/output/

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

    ./autosa ./autosa_tests/large/cnn/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[64,56,14,64];kernel[]->latency[4,4,7];kernel[]->simd[1,1,8]}" \
    --simd-info=./autosa_tests/large/cnn/simd_info.json \
    --no-reverse-order \
    --host-serialize

Now instead of HLS host code, an OpenCL host code is generated.   

We have prepared a template Makefile for Xilinx Vitis tools.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/large/cnn/Makefile ${AUTOSA_ROOT}/autosa.tmp/output/
    cp ${AUTOSA_ROOT}/autosa_tests/large/cnn/connectivity.cfg ${AUTOSA_ROOT}/autosa.tmp/output/

To generate the bitstream, use the following command.

.. code:: bash

    make all

After the bitstream is generated,
use the following command to run it on-board.    

.. code:: bash

    make check

.. note::
    
    As this design is rather large, Vitis fails to successfully route the design on-board
    in our experiment.
    We will rely on AutoBridge to route this design.

Using AutoBridge to Boost Frequency
-----------------------------------

You may also try to use `AutoBridge <https://github.com/Licheng-Guo/AutoBridge>`_ 
to boost the design frequency.
We cover how to use AutoBridge to improve the frequency in :ref:`use-autobridge-label`.

The reference AutoBridge scripts used for this example can be found at ``${AUTOSA_ROOT}/autosa_tests/large/cnn``.

The tables below show the detailed comparison results between the original design 
(unoptimized) and the design optimized with AutoBridge (optimized).

+-------------+-----+-----------------+------------------+--------------+---------------+
| Designs     | MHz | LUT             | REG              | BRAM         | DSP           |
+-------------+-----+-----------------+------------------+--------------+---------------+
| Unoptimized | N/A | N/A             | N/A              | N/A          | N/A           |
+-------------+-----+-----------------+------------------+--------------+---------------+
| Optimized   | 265 | 884520 (57.93%) | 1445020 (46.05%) | 697 (29.84%) | 8960 (72.99%) |
+-------------+-----+-----------------+------------------+--------------+---------------+

+-------------+-----------------+---------------+---------+
| Designs     | Kernel Time (s) | Host Time (s) | GFLOPs  |
+-------------+-----------------+---------------+---------+
| Unoptimized | N/A             | N/A           | N/A     |
+-------------+-----------------+---------------+---------+
| Optimized   | 0.015865        | 0.188105      | 932.714 |
+-------------+-----------------+---------------+---------+