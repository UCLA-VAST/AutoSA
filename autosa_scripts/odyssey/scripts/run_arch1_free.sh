#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
for design_idx in 1 4 7 10 13 16 19 22 25 28
do
    #for layer_idx in {1..49}
    #do
    #    python main.py --workload=resnet50_$layer_idx --stop-after-time=10 --use-db=0 --design-idx=$design_idx
    #done    
    for layer_idx in {1..36}
    do
        python main.py --workload=mobilenetv2_$layer_idx --stop-after-time=10 --use-db=0 --design-idx=$design_idx
    done    
    for layer_idx in {1..13}
    do
        python main.py --workload=vgg16_$layer_idx --stop-after-time=10 --use-db=0 --design-idx=$design_idx
    done    
done
cp -r outdir/* tmp/
cd -
