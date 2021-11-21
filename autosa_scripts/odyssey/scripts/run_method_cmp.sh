#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
for design_idx in {0..17}
do
    python main.py --workload=mm --stop-after-time=300 --use-db=0 --unit-task-method=genetic --profiling --design-idx=$design_idx
    python main.py --workload=mm --stop-after-time=300 --use-db=0 --unit-task-method=random --profiling --design-idx=$design_idx
    python main.py --workload=mm --stop-after-time=300 --use-db=0 --unit-task-method=random_pruning --profiling --design-idx=$design_idx
    python main.py --workload=mm --stop-after-epoch=150000 --use-db=0 --unit-task-method=annealing --profiling --design-idx=$design_idx
    python main.py --workload=mm --stop-after-epoch=300 --use-db=0 --unit-task-method=bayesian --profiling --design-idx=$design_idx
    python main.py --workload=mm --stop-after-time=300 --use-db=0 --unit-task-method=open_tuner --profiling --design-idx=$design_idx
    python main.py --workload=mm --stop-after-epoch=50000 --use-db=0 --unit-task-method=RL --profiling --design-idx=$design_idx
done
cp -r outdir/* tmp/
cd -
