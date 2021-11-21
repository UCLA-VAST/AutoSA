#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*
for design_idx in 1 4 7 10 13 16 19 22 25 28
do
    python main.py --workload=vgg16 --stop-after-time=10 --use-db=0 --n-worker=32 --design-idx=$design_idx
    python main.py --workload=resnet50 --stop-after-time=10 --use-db=0 --n-worker=32 --design-idx=$design_idx
    python main.py --workload=mobilenetv2 --stop-after-time=10 --use-db=0 --n-worker=32 --design-idx=$design_idx
done
cp -r outdir/* tmp/
cd -
