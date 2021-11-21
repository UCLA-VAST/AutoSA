#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
#for design_idx in {0..17}
for design_idx in 0
#for design_idx in {14..14}
#for design_idx in 6 7 8 12 13 14 15 16 17
do
    #python main.py --workload=mm --stop-after-time=10 --use-db=0 --unit-task-method=genetic --design-idx=$design_idx --profiling
    # Solver cmp
    python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --design-idx=$design_idx --profiling 
    # Imperfect pruning
    #python main.py --workload=mm --stop-after-time=10 --use-db=0 --unit-task-method=genetic --design-idx=$design_idx --profiling --objective=off_chip_comm
done

cp -r outdir/* tmp/
cd -
