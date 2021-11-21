#!/bin/bash

cd ..
python main.py --workload=vgg16_img2col --stop-after-time=10 --use-db=0 --n-worker=32
python main.py --workload=resnet50_img2col --stop-after-time=10 --use-db=0 --n-worker=32
python main.py --workload=mobilenetv2_img2col --stop-after-time=10 --use-db=0 --n-worker=32
cd -
