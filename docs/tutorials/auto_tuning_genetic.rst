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