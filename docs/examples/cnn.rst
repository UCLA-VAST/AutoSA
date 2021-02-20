Convolutional Neural Network (Single Layer, Small)
==================================================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This is an example of small-size CNN. 
The design files can be found at ``${AUTOSA_ROOT}/autosa_tests/cnn``.
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

    ./autosa ./autosa_tests/cnn/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[8,8,4,8];kernel[]->latency[4,2,4];kernel[]->simd[1,1,1,2]}" \
    --simd-info=./autosa_tests/cnn/simd_info.json \
    --host-serialize \
    --no-reverse-order \
    --hls

After compilation, you will find all generated files under the directory 
``${AUTOSA_ROOT}/autosa.tmp/output/src``. 
Copy the ``hls_script.tcl`` to the directory ``autosa.tmp/output``.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/cnn/hls_script.tcl ${AUTOSA_ROOT}/autosa.tmp/output/

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

    ./autosa ./autosa_tests/cnn/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4];kernel[]->array_part[8,8,4,8];kernel[]->latency[4,2,4];kernel[]->simd[1,1,1,2]}" \
    --simd-info=./autosa_tests/cnn/simd_info.json \
    --host-serialize \
    --no-reverse-order

Now instead of HLS host code, an OpenCL host code is generated.    

We have prepared a template Makefile for Xilinx Vitis tools.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/cnn/Makefile ${AUTOSA_ROOT}/autosa.tmp/output/
    cp ${AUTOSA_ROOT}/autosa_tests/cnn/connectivity.cfg ${AUTOSA_ROOT}/autosa.tmp/output/

Set the proper ``PLATFORM`` in the Makefile. 
By default, we set it to ``xilinx_u250_xdma_201830_2``.
You may notice that we also copy a file ``connectivity.cfg`` here.
This file assigns the DDR bank mapping for the design. 
By default, we map pointers A, B, C to DDR bank 0, 1, 2.
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

It will take a few hours to finish. After the bitstream is generated,
use the following command to run it on-board.    

.. code:: bash

    make check