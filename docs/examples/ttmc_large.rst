Chain of Tensor-matrix multiplications (TTMc) (Large)
=====================================================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This is an example of large-size Chain of Tensor-matrix multiplications.
The design files can be found at ``${AUTOSA_ROOT}/autosa_tests/large/ttmc``.
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

    ./autosa ./autosa_tests/large/ttmc/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[16,64,16,32];kernel[]->latency[1,8,8];kernel[]->simd[8,1]}" \
    --simd-info=./autosa_tests/large/ttmc/simd_info.json \
    --host-serialize \
    --hls

After compilation, you will find all generated files under the directory 
``${AUTOSA_ROOT}/autosa.tmp/output/src``. 
Copy the ``hls_script.tcl`` to the directory ``autosa.tmp/output``.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/large/ttmc/hls_script.tcl ${AUTOSA_ROOT}/autosa.tmp/output/

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

    ./autosa ./autosa_tests/large/ttmc/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[16,64,16,32];kernel[]->latency[1,8,8];kernel[]->simd[8,1]}" \
    --simd-info=./autosa_tests/large/ttmc/simd_info.json \
    --host-serialize

Now instead of HLS host code, an OpenCL host code is generated.   

We have prepared a template Makefile for Xilinx Vitis tools.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/large/ttmc/Makefile ${AUTOSA_ROOT}/autosa.tmp/output/
    cp ${AUTOSA_ROOT}/autosa_tests/large/ttmc/connectivity.cfg ${AUTOSA_ROOT}/autosa.tmp/output/

To generate the bitstream, use the command below.

.. code:: bash

    make all

After the bitstream is generated, use the following command to run it on-board.    

.. code:: bash

    make check

Below is the resource and frequency information we collected for this design.

+-----+-----------------+------------------+--------------+---------------+
| MHz | LUT             | REG              | BRAM         | DSP           |
+-----+-----------------+------------------+--------------+---------------+
| 201 | 621584 (41.43%) | 1016231 (32.57%) | 479 (21.01%) | 8192 (66.75%) |
+-----+-----------------+------------------+--------------+---------------+

You could also test the generated design on board. We have listed the performance of the design 
in the table below.

+-----------------+---------------+---------+
| Kernel Time (s) | Host Time (s) | GFLOPs  |
+-----------------+---------------+---------+
| 0.168946        | 1.8771        | 610.131 |
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
| Unoptimized | 201 | 621584 (41.43%) | 1016231 (32.57%) | 479 (21.01%) | 8192 (66.75%) |
+-------------+-----+-----------------+------------------+--------------+---------------+
| Optimized   | 300 | 622878 (41.53%) | 1010672 (32.40%) | 479 (21.01%) | 8192 (66.75%) |
+-------------+-----+-----------------+------------------+--------------+---------------+

+-------------+-----------------+---------------+---------+
| Designs     | Kernel Time (s) | Host Time (s) | GFLOPs  |
+-------------+-----------------+---------------+---------+
| Unoptimized | 0.168946        | 1.8771        | 610.131 |
+-------------+-----------------+---------------+---------+
| Optimized   | 0.112436        | 1.25489       | 916.781 |
+-------------+-----------------+---------------+---------+