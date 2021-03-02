#include "stdio.h"
#include "stdlib.h"
#include "math.h"

//#define PC
#define DC
//#define FC

typedef float data_t;
// point-wise conv
#define PC_O 16
#define PC_I 16
#define PC_R 8
#define PC_C 8
#define PC_K 3

// depth-wise conv
#define DC_O 16
#define DC_I 16
#define DC_R 8
#define DC_C 8
#define DC_K 3

// fc
#define FC_I 16
#define FC_J 16
