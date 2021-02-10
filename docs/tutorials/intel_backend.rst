Generating Intel OpenCL Design
==============================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

AutoSA can generate systolic arrays in Intel OpenCL. This page shows an example 
about generating a systolic array design for Intel FPGAs. 

.. note:: 

    The Intel OpenCL back-end is not performant currently due to the channel overheads.
    This back-end is provided only for testing purpose.

Generating the Design
---------------------

The design example used by this tutorial is at ``${AUTOSA_ROOT}/autosa_tests/mm_intel``.
Run the following command to generate the systolic array.

.. code:: bash

    ./autosa ./autosa_tests/mm_intel/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_opencl \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->array_part_L2[2,2,2];kernel[]->latency[8,8];kernel[]->simd[2]}" \
    --simd-info=./autosa_tests/mm_intel/simd_info.json \
    --host-serialize \
    --loop-infinitize \
    --double-buffer-style=0 \
    --mem-port-map="{kernel[]->A[0];kernel[]->B[1];kernel[]->C[2]}"

After compilation, you will find the generated designs under the directory
``${AUTOSA_ROOT}/autosa.tmp/output/src``.

We also provide an example Makefile for testing the design.
Copy it to the design directory.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/mm_intel/Makefile ${AUTOSA_ROOT}/autosa.tmp/output/

You may modify the Makefile based on your target FPGA board or use your own Makefile.
In the example Makfile, we target the Intel Stratix 10 board with HBM memory.

.. code:: bash

    AOCL_BOARD ?= s10mx_hbm_es

Set up your local Intel OpenCL SDK environment. Make sure the environment variable 
``INTELFPGAOCLSDKROOT`` is set properly. Then, to perform software emulation, run:

.. code:: bash

    make sw_emu_check

The design will be compiled and simulated on CPU. You should be able to see the following information printed on your terminal.

.. code:: bash

    AOCX file: kernel_sw_emu.aocx

    FPGA Time: 0.146633 s
    Host Time: 0.14696 s
    Passed!

which shows the design is successfully compiled and the simulation passed successfully.

To synthesize the design to RTL, run:

.. code:: bash

    make hls

The design will be synthesized to RTL. This process will take some time to finish.
Intel OpenCL SDK generates the detailed hardware information in HTML format, which 
can be found at ``${AUTOSA_ROOT}/autosa.tmp/output/bin/kernel/reports``.

Lastly, to generate the bitstream, run:

.. code:: bash

    make hw

More Details
------------

Compared to generating Xilinx HLS designs, when generating the Intel OpenCL code, we add the following 
three arguments to the compilation command.

``--loop-infinitize``: Xilinx HLS requires the loops to be bounded. Such a limitation is 
no longer required for Intel OpenCL. Loops can be eliminated if possible as the function can be 
run infinitely. Performing loop infitinization will eliminate the unnecessary outer loops 
in each function to reduce the hardware overheads.

``--double-buffer-style=0``: When generating the double buffer logic, by default, 
we will generate the ping-pong logic explicitly as you may see in the Xilinx HLS code as below.

.. code:: c

    // outer loops
    for (...)
      for (...) {
        // double buffer logic
        if (arb == 0) {
          func1(ping_array);
          func2(pong_array);
        } else if (arb == 1) {
          func1(pong_array);
          func2(ping_array);
        }
      }
      
However, such a coding style no longer works in Intel OpenCL design as Intel OpenCL SDK 
lacks the ability to identify that ``func1`` and ``func2`` can be executed in parallel.
As a temporary solution, we will modify this coding style by inlining the function contents of 
``func1`` and ``func2`` directly. By setting ``--double-buffer-style=0``, we will generate the 
functional double buffering logic for Intel OpenCL. The generated logic looks like below:

.. code:: c

    while (1) {
      if (func1_en) {
        // func1 logic
        ...
      }
      if (func2_en) {
        // func2 logic
        ...
      }      
    }

``--mem-port-map="{kernel[]->A[0];kernel[]->B[1];kernel[]->C[2]}"``: 
As the target FPGA board is equipped with HBM memory, we may assign the global pointer to 
different HBM banks. In Xilinx Vitis flow, we will write a separate configuration file 
to map global pointers to different banks. However, in Intel flow, we will need to code it 
explicitly in the OpenCL kernel code. This arugment is optional. It maps the global pointers 
``A``, ``B``, and ``C`` to bank 0, 1, and 2. You should find the following code in the OpenCL code.

.. code:: c

    __kernel void A_IO_L3_in_serialize(__global volatile __attribute__((buffer_location("HBM0"))) A_t16 *restrict A)

in which we use the ``__attribute__((buffer_location("HBM0")))`` to assign the pointer ``A`` to the bank ``HBM0``.