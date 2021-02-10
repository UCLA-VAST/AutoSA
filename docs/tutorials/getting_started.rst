Getting Started
===============

**Author**: Jie Wang (jiewang@cs.ucla.edu)

In this tutorial, we will give an overview of the compilation process of AutoSA 
and demonstrate it with an example.

The Compilation Flow of AutoSA
------------------------------

The figure below shows the overall compilation flow of AutoSA.

.. image:: images/flow.png
    :align: center

The input code of AutoSA is a C code that describes the algorithm to be mapped to
the systolic array. AutoSA is built on the polyhedral framework, which takes SCoP (static control of parts) 
programs as the input. In addition, AutoSA assumes that all the dependences of the input
programs have been rendered uniform before the compilation.

The example code below describes the matrix multiplication and serves as the input to AutoSA.

.. code:: c

    #pragma scop
    for (int i = 0; i < I; i++)        
      for (int j = 0; j < J; j++)   {
        C[i][j] = 0;
        for (int k = 0; k < K; k++)
          C[i][j] += A[i][k] * B[k][j];
      }
    #pragma endscop

Note that we insert the pragma

.. code:: c

    #pragma scop

before the code fragment and insert the pragma

.. code:: c

    #pragma endscop

after the code fragment to annotate the code region to be analyzed and transformed by the compiler.    

In the next step, a polyhedral representation of the input code is extracted. AutoSA 
uses `integer set library (ISL) <http://isl.gforge.inria.fr/>`_ for manipulating the polyhedral IR.
After extracting the polyhedral IR, AutoSA will perform an initial transformation of the program using the 
ISL scheduler. The ISL scheduler aims to transform the program to maximize the locality and parallelism.
The transformed program by ISL will be the input to the rest steps of AutoSA.
For more details about the ISL scheduler, please refer to the ISL manual. Readers are also 
recommended to read this paper [PLUTO08]_ for more details about the scheduling algorithm used by ISL.

The next stage, named as *legality check*, checks if the input program can legally be
mapped to a systolic array. At that stage, we simply check if all dependences are uniform.

A complete systolic array architecture consists of both the PE array and the on-chip I/O network. 
AutoSA separates the process of building these two components into two stages: 
*computation and communication management*. 
The stage of computation management constructs the PE and optimizes its micro-architecture. 
After that, the stage of communication management builds the I/O network for transferring data between PEs and the external memory. 

After the previous stages, AutoSA generates the AST from the optimized program. 
The AST is then traversed to generate the final design for the target hardware.
At present, AutoSA can generate Xilinx HLS C, Intel OpenCL, and Mentor Graphics Catapult C.

The stages of computation and communication management involve multiple optimization techniques, 
each introducing several tuning options. 
AutoSA implements tunable knobs for these techniques which can be set by users manually or tuned by an auto-tuner.

An Example
----------

The example code above can be found at ``${AUTOSA_ROOT}/autosa_tests/mm_getting_started/kernel.c``.

Generating Hardware Code
^^^^^^^^^^^^^^^^^^^^^^^^

To compile the code to Xilinx HLS C for Xilinx Vitis toolkit, run the code below.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm/simd_info.json --host-serialize

The generated code can be found in the directory ``${AUTOSA_ROOT}/autosa.tmp/output/src/`.
For detailed information of AutoSA compilation options, please run

.. code:: bash

    ./autosa --help

or refer to `AutoSA Compilation Options`_.

Generating FPGA Bitstream
^^^^^^^^^^^^^^^^^^^^^^^^^

Set up the Xilinx Vitis development kit. Run the following commands.

.. code:: bash

    source /opt/Xilinx/Vitis/2019.2/settings64.sh
    source /opt/xilinx/xrt/setup.sh

Execute the makefile to build the design.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_tests/mm_getting_started/Makefile autosa.tmp/output/
    cp ${AUTOSA_ROOT}/autosa_tests/mm_getting_started/connectivity.cfg autosa.tmp/output/
    cd ${AUTOSA_ROOT}/autosa.tmp/output
    make all

.. admonition:: Makefile Options

    * ``MODE := hw_emu``: Set the build configuration mode to HW Emulation, other modes: ``sw_emu``|``hw``
    * ``PLATFORM := xilinx_u250_xdma_201830_2``: Select the target platform
    * ``KERNEL_SRC := `src/kernel_kernel.cpp`: List the kernel source files
    * ``HOST_SRC := src/kernel_host.cpp``: List the host source files

The ``connectivity.cfg`` describes the DRAM port mapping. 
For more details about how to change the DRAM port mapping, 
please refer to the Xilinx tutorials: `Using Multiple DDR Banks <https://xilinx.github.io/Vitis-Tutorials/2020-1/docs/bloom/6_using-multiple-ddr.html>`_.

Generating Xilinx HLS project
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

AutoSA also supports generate HLS projects. Add the option

.. code:: bash

    --hls

to the command when compiling the program.

AutoSA will generate an HLS host file ``${AUTOSA_ROOT}/autosa.tmp/output/src/kernel_host.cp``
instead of the OpenCL host file generated in the previous step. 
To build the HLS project, use the following commands.

.. code:: bash

    cp ${AUTOSA_ROOT}/autosa_scripts/hls_scripts/hls_script.tcl ${AUTOSA_ROOT}/autosa.tmp/output/
    cd ${AUTOSA_ROOT}/autosa.tmp/output
    vivado_hls -f hls_script.tcl

Using AutoSA in Manual Mode
---------------------------

As mentioned previously, AutoSA can be used in both *manual* and *auto* mode. 
In the auto mode, AutoSA will proceed based on the pre-set policy. In the manual mode,
AutoSA will dump out the optimization choices to users. Users will then provide AutoSA with specific optimization policy, which 
will be applied by AutoSA. 

The tunable knobs of the compilation flow are included in the configuration file
``${AUTOSA_ROOT}/autosa_config/autosa_config.json``. Currently, the following optimization 
stages can be configured in AutoSA.

* **space_time**: 
  This step applies the space-time transformation to transform algorithms to systolic arrays. 
  By default, for each algorithm, multiple systolic arrays will be generated. In the auto mode,
  AutoSA will select one array based on the heuristics. In the manual mode, users will select the 
  array to be processed in the following steps.
* **array_part**: 
  This step partitions the aray into smaller sub-arrays. In the auto mode, all tilable loops 
  that can be used as array partitioning loops will be tiled with a fixed factor. In the manual mode,
  users can select loops to be tiled and provide the compiler with specific tiling factors.
* **array_part_L2**:
  AutoSA allows to generate up to two levels of array partitioning loops. This is helpful to architectures
  with many levels of memory hierarchy. Similarly, in the auto mode, AutoSA decides which loops to be further tiled and 
  selects a fixed tiling factor. Users can make such choices in the manual mode.
* **latency**:
  This step performs the latency hiding in case the innermost loop in the program carries
  dependence which prevents the design to be fully pipelined. Parallel loops in the program can be 
  used as the latency hiding candidate loops. In the auto mode, all parallel loops will be tiled and 
  the point loops will be permuted innermost. In the manual mode, users will have to specify which loops 
  to be chosen and the corresponding tiling factors.
* **simd**:
  This step vectorizes the computation inside PEs. In the auto mode, AutoSA analyzes the program
  and selects the best vectorizable loop with heuristics. In the manual mode, users will select the 
  vectorizable loop.
* **hbm**:
  AutoSA also supports HBM memory. The systolic array will be connected to multiple HBM ports.
  In the auto mode, AutoSA allocates each array to a fixed number of HBM banks. 
  In the manual mode, users select the number of HBM banks to be connected to each array.

.. note:: 

    For more details about the optimization steps in AutoSA, please refer to the tutorial :ref:`construct-and-optimize-array-label`.

To switch between two different modes, modify the modes in ``${AUTOSA_ROOT}/autosa_config/autosa_config.json``.
For example, modify the content in ``autosa_config.json`` to

.. code:: json

    "array_part": {
        "enable": 1,
        "mode": "auto"
    }

to enable the array partitioning to execute in the auto mode. Modify it to 

.. code:: json

    "array_part": {
        "enable": 1,
        "mode": "manual"
    }

to run it in the manual mode.

Below we show how to use AutoSA in manual mode in detail.

Space-Time Transformation
^^^^^^^^^^^^^^^^^^^^^^^^^

In this step, multiple systolic arrays are generated from the input program. We will 
need to select one systolic array to proceeed. We set this step to manual mode in the 
configuration file.

.. code:: json

    "space_time": {
        "mode": "manual"
    }

Then run the command.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output

In the terminal, AutoSA displays a message.

.. code:: bash

    [AutoSA] 6 systolic arrays generated.

AutoSA also generates a file ```${AUTOSA_ROOT}/autosa.tmp/output/tuning.json``,
which includes guidance information for further optimization. In this example,
we have the content below.

.. code:: json

    "space_time": {
        "n_kernel": 6
    }

This tells the user that there are 6 different systolic array candidates generated. 
We may select one of them to proceed. 
For example, we could select the fourth candidate which is a 2D systolic array 
with the data from matrix A transferred horizontally, and data from matrix B 
transferred vertically. Each PE computes one element of ``C[i][j]`` locally, 
which is drained out at last to the external memory. 
The architecture of this array is depicted below.

.. image:: images/mm_array_opt.png
    :width: 300
    :align: center

To guide AutoSA to select this design, supply AutoSA with an additional argument.

.. code:: bash

    --sa-sizes="{kernel[]->space_time[3]}"

which tells AutoSA to select the fourth array (index starting from 0) during the space-time transformation.

Array Partitioning
^^^^^^^^^^^^^^^^^^

In this step, we will tile the space loops to partition the original array into smaller ones. The computation is then scheduled onto the sub-arrays in sequence. 
We first set this step in manual mode. Then run the command:

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3]}"

AutoSA displays new information on the terminal.

.. code:: bash

    [AutoSA] Appy PE optimization.
    [AutoSA] Apply array partitioning.

The ``tuning.json`` contains the content below:

.. code:: json

    "array_part": {
        "tilable_loops": [64, 64, 64],
        "n_sa_dim": 2
    }

This tells users there are three candidate loops that can be tiled. 
The upper bounds of each loop is 64. We may select any tiling factor no greater than 64. 
Besides, AutoSA only supports tiling factors as sub-multiples of the loop bounds for now. 
If the user is interested to understand which three loops are selected as the candidate loops, 
add the option ``--AutoSA-verbose`` to the command and run again.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3]}" --AutoSA-verbose

Below is the printed message from AutoSA.

.. code:: text

    domain: "{ S_0[i, j] : 0 <= i <= 63 and 0 <= j <= 63; S_1[i, j, k] : 0 <= i <= 63 and 0 <= j <= 63 and 0 <= k <= 63 }"
    child:
        context: "{ [] }"        
        child:
            schedule: "[{ S_0[i, j] -> [(i)]; S_1[i, j, k] -> [(i)] }, { S_0[i, j] -> [(j)]; S_1[i, j, k] -> [(j)] }, { S_0[i, j] -> [(0)]; S_1[i, j, k] -> [(k)] }]"
            permutable: 1
            coincident: [ 1, 1, 0 ]
            space_time: [ space, space, time ]
            pe_opt: [ array_part, array_part, array_part ]
            sched_pos: [ 0, 1, 2 ]       
            child:
                sequence:
                - filter: "{ S_0[i, j] }"
                - filter: "{ S_1[i, j, k] }"    

This is the schedule tree of the current program. More details about the schedule tree can be found
in the paper [SCHEDTREE14]_.
The first *domain* node represents the iteration domain of the input program.
The "band" node contains the partial schedule of the loops. 
In the current program, there are three loops :math:`i`, :math:`j`, and :math:`k`.
AutoSA provides verbose loop information. For example, the attribute of coincident indicates 
if the loop is parallel. The pe_opt attribute annotates the candidate loops that can be 
used for array partitioning. In this case, all three loops are tilable and can be used for 
array partitioning.

As an example, we select the tiling factors ``[16,16,16]``. Run hte command below.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16]}"

Latency Hiding
^^^^^^^^^^^^^^

This step performs latency hiding. We will select parallel loops, tile them, and permute the point 
loops innermost to hide the computation latency. 
After the previous step, we will find the content below in the `tuning.json`.

.. code:: json

    "latency": {
        "tilable_loops": [16,16]
    }

Similarly, you may add the argument `--AutoSA-verbose` to find out which loops have 
been selected as the latency hiding candidate loops.

We select the tiling factors ``[8,8]`` to proceed. Run the command below.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}"

SIMD Vectorization    
^^^^^^^^^^^^^^^^^^

In this step, we select the vectorizable loop, tile them, permute the point loop innermost.
The point loop will be unrolled by HLS at last. At present, a loop is set as the candidate loop if 
meeting the following criteria:

* It is a parallel loop or reduction loop that is annotated by users.
* All array references within the loop are stride-one or stride-zero with regard to this loop.
  
.. note::
    
    For the reduction loops, AutoSA requires users to annotate the loop manually. This 
    is done by providing a ``simd_info.json`` file to the compiler. 
    For our example, we can provide a ``simd_info.json`` file with the content below.
    
    .. code:: json

        "kernel3": {
            "reduction": ["y"]
        }

    The ``kernel[index]`` indicates the current array to be analyzed. As mentioned in the step of 
    space-time transformation, we select the 3rd array to proceed.
    The ``reduction`` attribute indicates if the candidate loop is a reduction loop.
    When running the last command
    
    .. code:: bash

        ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}"

    AutoSA will check all the non-parallel loops and prompt messages to ask if the loop is a 
    reduction loop. Alternatively, users can prepare the information in ``simd_info.json`` following the loop sequence 
    as shown in the prompted compilation message.
    
In this example, loops :math:`i` and :math:`j` have been selected as the space loops. Only the loop :math:`k` is left
which is a non-parallel loop. Therefore, we provide the attribute ``"reduction": ["y"]`` to the compiler
as the loop :math:`k` is a reduction loop.

With this information, AutoSA further checks if all array accesses under the loop :math:`k` are 
stride-one or stride-zero. Note that among three array accesses ``C[i][j]``, ``A[i][k]``, and ``B[k][j]``,
access ``C[i][j]`` is stride-zero in regard to loop :math:`k`, and ``A[i][k]`1 is stride-one.
However, ``B[k][j]`` is neither stride-one nor stride-zero. 
A layout transformation is required to make this array 
access to stride-one/zero.
AutoSA will examine the possibility of performing layout transformation to expose more
vectorization possibility. In this case, the following information will be printed in the terminal.

.. code:: bash

    [AutoSA] Array reference (R): { S_1[i, j, k] -> B[k, j] }
    [AutoSA] Layout transform: Permute dim (0) to the innermost

This indicates that AutoSA suggests to permute the first dimension of the array B to innermost to make the loop vectorizable.

.. note:: 

    In the example code, simply uncomment the line below to apply the layout transformation.

    .. code:: c

        #define LAYOUT_TRANSFORM

After modifying the input code with this layout transformation, run the following command.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8]}" --simd-info=./autosa_tests/mm_getting_started/simd_info.json

And we can find the updated ``tuning.json``.

.. code:: json

    "simd": {
        "tilable_loops": [16],
        "scores": [15],
        "legal": [1],
        "sa_dims": [2, 2]
    }

This indicates that the candidate loop has the upper bound of 16. 
We assign a score based on heuristics to each candidate loop. 
The higher the score is, the more hardware-friendly it is when being selected as the SIMD loop. 
The item legal indicates that this loop can be directly used for optimization. 
Otherwise, we will need to perform further layout transformation on the arrays used by the program to expose the SIMD opportunity. 
Since we have already applied the layout transformation, this attribute is set to 1.

We select the tiling factor ``[2]`` and proceed. Run the command below.

.. code:: bash

    ./autosa ./autosa_tests/mm_getting_started/kernel.c --config=./autosa_config/autosa_config.json --target=autosa_hls_c --output-dir=./autosa.tmp/output --sa-sizes="{kernel[]->space_time[3];kernel[]->array_part[16,16,16];kernel[]->latency[8,8];kernel[]->simd[2]}" --simd-info=./autosa_tests/mm_getting_started/simd_info.json

After this step, you should be able to find the files of the generated arrays in ``${AUTOSA_ROOT}/autosa.tmp/output/src``.

AutoSA Compilation Options
--------------------------

* ``--autosa-autosa, --autosa``: generate systolic arrays using AutoSA [default: yes]
* ``--autosa-block-sparse, --block-sparse``: use block sparsity [default: no]
* ``--autosa-block-sparse-ratio, --block-sparse-ratio``: block sparsity ratio (e.g., kernel[]->A[2,4])
* ``--autosa-config, --config``: AutoSA configuration file
* ``--autosa-data-pack, --data-pack``: enable data packing [default: yes]
* ``--autosa-data-pack-sizes, --data-pack-sizs``: data pack sizes upper bounds (bytes) at 
  innermost, intermediate, outermost I/O level [default: kernel[]->data_pack[8,32,64]]
* ``--autosa-double-buffer. --double-buffer``: enable double-buffering for data transfer [default: yes]
* ``--autosa-double-buffer-style, --double-buffer-style``: change double-buffering logic coding style
  (0: while loop 1: for loop) [default: 1]
* ``--autosa-fifo-depth, --fifo-depth``: default FIFO depth [default: 2]
* ``--autosa-hbm, --hbm``: use multi-port DRAM/HBM [default: no]
* ``--autosa-hbm-port-num, --hbm-port-num``: default HBM port number per array [default: 2]
* ``--autosa-hls, --hls``: generate Xilinx HLS host [default: no]
* ``--autosa-host-serialize, --host-serialize``: serialize/deserialize the host data [default: no]
* ``--autosa-insert-hls-dependence, --insert-hls-dependence``: insert Xilinx HLS dependence pragma (alpha version) [default: no]
* ``--autosa-int-io-dir, --int-io-dir``: set the default interior I/O direction (0: [1,x] 1: [x,1]) [default: 0]
* ``--autosa-io-module-embedding, --io-module-embedding``: embed the I/O modules inside PEs if possible [default: no]
* ``--autosa-loop-infinitize, --loop-infinitize``: apply loop infinitization optimization (Intel OpenCL only) [default: no]
* ``--autosa-local-reduce, --local-reduce``: generate non-output-stationary array with local reduction [default: no]
* ``--autosa-reduce-op, --reduce-op``: reduction operator (must be used with local-reduce together)
* ``--autosa-lower-int-io-L1-buffer, lower-int-io-L1-buffer``: lower the L1 buffer for interior I/O modules [default: no]
* ``--autosa-max-sa-dim, --max-sa-dim``: maximal systolic array dimension [default: 2]
* ``--autosa-output-dir, --output-dir``: AutoSA Output directory [default: ./autosa.tmp/output]
* ``--autosa-sa-sizes, --sa-sizes``: per kernel PE optimization tile sizes
* ``--autosa-sa-type=sync|async, --sa-type=sync|async``: systolic array type [default: async]
* ``--autosa-simd-info, --simd-info``: per kernel SIMD information
* ``--autosa-simd-touch-space, --simd-touch-space``: use space loops as SIMD vectorization loops [default: no]
* ``--autosa-two-level-buffer, --two-level-buffer``: enable two-level buffering in I/O modules [default: no]
* ``--autosa-uram, --uram``: use Xilinx FPGA URAM [default: no]
* ``--autosa-use-cplusplus-template, --use-cplusplus-template``: use C++ template in codegen (necessary for irregular PEs) [default: no]
* ``--autosa-verbose, --verbose``: print verbose compilation information [default: no]
* ``--autosa-hcl, --hcl``: generate code for integrating with HeteroCL [default: yes]

Bibliography
------------

.. [PLUTO08] Bondhugula, Uday, et al. "A practical automatic polyhedral parallelizer and locality optimizer." Proceedings of the 29th ACM SIGPLAN Conference on Programming Language Design and Implementation. 2008.
.. [SCHEDTREE14] Verdoolaege, Sven, et al. "Schedule trees." International Workshop on Polyhedral Compilation Techniques, Date: 2014/01/20-2014/01/20, Location: Vienna, Austria. 2014.