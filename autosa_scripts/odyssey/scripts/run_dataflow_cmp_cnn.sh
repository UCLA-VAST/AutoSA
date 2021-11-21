#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
#for design_idx in {0..29}
for design_idx in 6 7 8 15 16 17 27 28 29
do
    for layer_idx in {1..13} 
    do    
        python main.py --workload=vgg16_$layer_idx --stop-after-time=10 --use-db=0 --unit-task-method=genetic --design-idx=$design_idx --profiling
    done
done
cp -r outdir/* tmp/
cd -
