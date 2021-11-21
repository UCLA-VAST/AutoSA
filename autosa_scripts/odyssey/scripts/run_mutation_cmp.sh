#!/bin/bash

# Use solver by default
# Set epsilon to 0 when only using the factorization mutation
cd ..
rm -rf outdir/*
rm -rf tmp/*
python main.py --workload=mm --stop-after-time=20 --use-db=0 --unit-task-method=genetic --design-idx=3 --profiling
cp -r outdir/* tmp/
cd -