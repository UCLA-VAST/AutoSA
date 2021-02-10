#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef float data_t;
#define I 1024
#define J 1024
#define K 1024

// Sparsity [3:4]
//#define VEC_LEN 4
//#define NON_ZERO_NUM 3
//#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)
//#define META_DATA_NUM 1
//#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))

// Sparsity [2:4]
//#define VEC_LEN 4
//#define NON_ZERO_NUM 2
//#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)
//#define META_DATA_NUM 2
//#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))

// Sparsity [1:4]
//#define VEC_LEN 4
//#define NON_ZERO_NUM 1
//#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)
//#define META_DATA_NUM 1
//#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))

// Sparsity [4:8]
#define VEC_LEN 8
#define NON_ZERO_NUM 4
#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)
#define META_DATA_NUM 4
#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))

// Sparsity [3:8]
//#define VEC_LEN 8
//#define NON_ZERO_NUM 3
//#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)
//#define META_DATA_NUM 1
//#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))

// Sparsity [2:8]
//#define VEC_LEN 8
//#define NON_ZERO_NUM 2
//#define COMPRESS_RATIO (VEC_LEN/NON_ZERO_NUM)
//#define META_DATA_NUM 2
//#define EFF_COMPRESS_RATIO (VEC_LEN/(NON_ZERO_NUM+META_DATA_NUM))