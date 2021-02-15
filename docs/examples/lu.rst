LU Decomposition (Small)
========================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This is an example of small-size LU decomposition. 
The design files can be found at ``${AUTOSA_ROOT}/autosa_tests/lu``.
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

    ./autosa ./autosa_tests/lu/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[-1,-1,-1];kernel[]->latency[]}" \
    --simd-info=./autosa_tests/lu/simd_info.json \
    --use-cplusplus-template \
    --no-reschedule \
    --hls

.. note:: 

    Compared to other examples, for LU decomposition, we add some additional arguments.
    ``--use-cplusplus-template``: This argument enables AutoSA to generate C code using 
    C++ template as different PEs will have different functionalities in this array.
    ``--no-reschedule``: This is due to the limtation of current ISL scheduler which 
    will generate a new program without any permutable loops that prohibit the transformation
    to systolic arrays. Therefore, we disable the ISL auto-scheduling in this application.

    Besides, the input source code has been modified to make sure all dependences are uniform.
    AutoSA lacks the ability to automatically uniformize the program and requires human
    modification for such cases.

After compilation, you will find all generated files under the directory
``${AUTOSA_ROOT}/autosa.tmp/output/src``. 
Copy the ``hls_script.tcl`` to the directory ``autosa.tmp/output``.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/lu/hls_script.tcl ${AUTOSA_ROOT}/autosa.tmp/output/

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

    ./autosa ./autosa_tests/lu/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[-1,-1,-1];kernel[]->latency[]}" \
    --simd-info=./autosa_tests/lu/simd_info.json \
    --use-cplusplus-template \
    --no-reschedule

Now instead of HLS host code, an OpenCL host code is generated.  

Please refer to other examples for the instructions on using Xilinx Vitis for generating the bitstream.