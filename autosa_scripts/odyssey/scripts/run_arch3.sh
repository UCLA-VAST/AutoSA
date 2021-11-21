#!/bin/bash

cd ..
rm -rf outdir/*
rm -rf tmp/*

#python main.py --workload=vgg16 --stop-after-time=10 --use-db=0 --n-worker=32 --explore-multi-acc --explore-fusion --method=customized2 --max-n-array=8
#python main.py --workload=resnet50 --stop-after-time=10 --use-db=0 --n-worker=32 --explore-multi-acc --explore-fusion --method=customized2 --max-n-array=8
#python main.py --workload=mobilenetv2 --stop-after-time=10 --use-db=0 --n-worker=32 --explore-multi-acc --explore-fusion --method=customized2 --max-n-array=8

python main.py --workload=vgg16 --stop-after-time=10 --use-db=0 --n-worker=32 --explore-multi-acc --explore-fusion --method=customized2 --max-n-array=8 --batch-size=16
python main.py --workload=resnet50 --stop-after-time=10 --use-db=0 --n-worker=32 --explore-multi-acc --explore-fusion --method=customized2 --max-n-array=8 --batch-size=16
python main.py --workload=mobilenetv2 --stop-after-time=10 --use-db=0 --n-worker=32 --explore-multi-acc --explore-fusion --method=customized2 --max-n-array=8 --batch-size=16

cp -r outdir/* tmp/
cd -
