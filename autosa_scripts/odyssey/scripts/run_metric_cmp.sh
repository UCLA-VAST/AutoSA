#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
#python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=0
#python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=1
#python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=2
#python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=3 --objective=off_chip_comm
python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=3 --objective=dsp_num
#python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=4
#python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --profiling --design-idx=5
cp -r outdir/* tmp/
cd -
