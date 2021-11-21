#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
for design_idx in {0..17}
do    
    python main.py --workload=mm --stop-after-time=10 --use-db=0 --unit-task-method=genetic --design-idx=$design_idx --objective=energy --profiling
done

cp -r outdir/* tmp/
cd -
