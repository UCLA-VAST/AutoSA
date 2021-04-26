Auto-Tuning (Genetic Search)
===============================================================

**Author**: Jie Wang (jiewang@cs.ucla.edu)

This page introduces an alternative auto-tuning appraoch in addition to the exhaustive search.
This approach leverages genetic search and provides a much faster convergence speed
than the exhaustive search. 

.. note:: 

    This tuner is currently under development and is unstable.

Auto-Tuning Example
-------------------
To tune a certain design, we will first use AutoSA to generate a description file in JSON
format. For the matrix multiplication example, use the following command.

.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --tuning-method=1

Note that we will only need to specify the array to be explored using the argument 
``--sa-sizes="{kernel[]->space_time[3]}"``, and we add a new flag ``--tuning-method=1``
to instruct AutoSA to generate the required description file.

You will find a description file ``kernel3.json`` under the directory ``autosa.tmp/output/tuning``.
This file describes all the necessary infomation about the design used during the auto-tuning, including
the memory and computation information.

Next, we will call the auto-tuner to search the optimal configuration for this design.
Switch to the directory ``autosa_scripts/tuner``.

.. code:: bash

    cd autosa_scripts/tuner

Then call the tuner to start the searching.

.. code:: bash

    python main.py \
    --designs=/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning \
    --stop-after-time=10 \
    --cst=hw_cst \
    --task=mm

The flag ``stop-after-time=10`` tells the tuner to stop searching after 10 seconds.
The flag ``cst=hw_cst`` points to the hardware constraints file ``cst/hw_cst.json``.
The flag ``task=mm`` points to the task configuration file ``task/mm.json`` which describes the 
matrix dimensions of the problem.

You will find the detailed information of the optimal design found by the auto-tuner 
printed in the screen.

More Examples
-------------
Example 1
^^^^^^^^^
Generate design description.

.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[0]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --tuning-method=1

Run the auto-tuner.

.. code:: bash

    python main.py \
    --designs=/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning \
    --stop-after-time=10 \
    --cst=hw_cst \
    --task=mm

Example 2
^^^^^^^^^    
.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[1]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --tuning-method=1

Run the auto-tuner.

.. code:: bash

    python main.py \
    --designs=/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning \
    --stop-after-time=10 \
    --cst=hw_cst \
    --task=mm

Example 3
^^^^^^^^^    
.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[2]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --local-reduce \
    --reduce-op="+" \
    --simd-touch-space \
    --no-isl-sink \
    --tuning-method=1

Run the auto-tuner.

.. code:: bash

    python main.py \
    --designs=/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning \
    --stop-after-time=10 \
    --cst=hw_cst \
    --task=mm    

Example 4
^^^^^^^^^    
.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[4]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --local-reduce \
    --reduce-op="+" \
    --simd-touch-space \
    --no-isl-sink \
    --tuning-method=1

Run the auto-tuner.

.. code:: bash

    python main.py \
    --designs=/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning \
    --stop-after-time=10 \
    --cst=hw_cst \
    --task=mm    

Example 5
^^^^^^^^^    
.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[5]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --local-reduce \
    --reduce-op="+" \
    --simd-touch-space \
    --no-isl-sink \
    --tuning-method=1

Run the auto-tuner.

.. code:: bash

    python main.py \
    --designs=/curr/jaywang/research/autosa/AutoSA/autosa.tmp/output/tuning \
    --stop-after-time=10 \
    --cst=hw_cst \
    --task=mm

Exploring Loop Permutation
--------------------------
At the stage of array partitioning, different loop orderings will lead to different 
on-chip memory usage and latency. By default, AutoSA will select the loop ordering
by heuristics. Specially, loops are ordered based on the ascending order of the loop 
dependence distances. Different loop orderings can also be explored through auto-tuning.
To explore different loop orderings, use the following command.
    
.. code:: bash

    ./autosa ./autosa_tests/mm/kernel.c \
    --config=./autosa_config/autosa_config.json \
    --target=autosa_hls_c \
    --output-dir=./autosa.tmp/output \
    --sa-sizes="{kernel[]->space_time[3]}" \
    --simd-info=./autosa_tests/mm/simd_info.json \
    --host-serialize \
    --hls \
    --tuning-method=1 \
    --explore-loop-permute
    
The newly added flag "--explore-loop-permute" will instruct AutoSA to generate different 
loop orderings that potentially lead to different performance.
AutoSA will iteratively generate all potentially profitable loop orderings and dump 
out their description files. In this example, you should find there new json files 
``kernel3_0.json``, ``kernel3_1.json``, ``kernel3_2.json`` in the directory of 
``./autosa.tmp/output/tuning``. These three designs correspond to loop orderings of 
``i-k-j``, ``k-j-i``, and ``i-j-k`` of the array partitioning loops.

Then, you could follow the above procedures as mentioned to use the auto-tuner to search
the performance for these designs.

Note that we are not simply generating all different loop orderings by enumeration, which
could lead to 3!=6 different orderings. By proper analysis, some of orderings is either 
equivalent to others or submit to inferior performance to others. AutoSA analyzes these 
orderings and automatically prunes away the equivalent or inferior loop orderings.