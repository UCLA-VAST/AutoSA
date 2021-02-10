// Uncomment the macro below to apply the layout transformation on array B to enable SIMD vectorization
#define LAYOUT_TRANSFORM

#include "kernel.h"

int main(int argc, char **argv) {
#ifndef LAYOUT_TRANSFORM  
  data_t A[I][K], B[K][J], C[I][J], C_golden[I][J]; 
#else  
  data_t A[I][K], B[J][K], C[I][J], C_golden[I][J];
#endif

  for (int i = 0; i < I; i++) 
    for (int k = 0; k < K; k++) {
      A[i][k] = (data_t)rand() / RAND_MAX;
    }

  for (int j = 0; j < J; j++)
    for (int k = 0; k < K; k++) {
#ifndef LAYOUT_TRANSFORM      
      B[k][j] = (data_t)rand() / RAND_MAX;
#else      
      B[j][k] = (data_t)rand() / RAND_MAX;
#endif      
    }

#pragma scop
  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C[i][j] = 0;
      for (int k = 0; k < K; k++) {
#ifndef LAYOUT_TRANSFORM        
        C[i][j] = C[i][j] + A[i][k] * B[k][j];
#else        
        C[i][j] = C[i][j] + A[i][k] * B[j][k];
#endif        
      }
    }
#pragma endscop

  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C_golden[i][j] = 0;
      for (int k = 0; k < K; k++) {
#ifndef LAYOUT_TRANSFORM        
        C_golden[i][j] = C_golden[i][j] + A[i][k] * B[k][j];
#else
        C_golden[i][j] = C_golden[i][j] + A[i][k] * B[j][k];
#endif        
      }
    }

  int err = 0;
  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      if (fabs((float)C_golden[i][j] - (float)C[i][j]) > 0.001)
        err++;
    }

  if (err)
    printf("Failed with %d errors!\n", err);
  else
    printf("Passed!\n");

  return 0;
}
