#include "stdio.h"
#include "stdlib.h"
#include "math.h"

typedef float data_t;
#define O 512
#define I 512
#define R 60
#define C 56
#define K 3

//// Small test 
//#define O 16
//#define I 16
//#define R 8
//#define C 8
//#define K 3

// cmd:
// ./autosa ./autosa_tests/cnn/kernel.c --AutoSA-config=./autosa_config/autosa_config.json --target=autosa_hls_c --AutoSA-autosa --AutoSA-two-level-buffer --AutoSA-uram --isl-schedule-whole-component --AutoSA-output-dir=./autosa.tmp/output --sa-sizes="{kernel[0]->array_part[8,8,4,8];kernel[0]->array_part_L2[1,1,1,2];kernel[0]->latency[4,4,2];kernel[0]->simd[-1,-1,-1,2]}" --AutoSA-simd-info=./autosa_tests/cnn/simd_info.json
// Requires modification of 'cout_drain_IO_L1_out_intra_trans and cout_drain_IO_L3_out to achieve II=1
