#include "kernel.h"

int main(int argc, char **argv) {
//  data_t A[I][K], B[K][J], C[I][J], C_golden[I][J]; 
  data_t A[I][K], B[J][K], C[I][J], C_golden[I][J]; // gemm0,3
//  data_t A[K][I], B[K][J], C[I][J], C_golden[I][J]; // gemm4

  for (int i = 0; i < I; i++) 
    for (int k = 0; k < K; k++) {
      A[i][k] = k;
//      A[k][i] = k;
    }

  for (int j = 0; j < J; j++)
    for (int k = 0; k < K; k++) {
      B[j][k] = k;
//      B[k][j] = k;
    }

#pragma scop
  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C[i][j] = 0;
      for (int k = 0; k < K; k++) {
        C[i][j] = C[i][j] + A[i][k] * B[j][k];
//        C[i][j] = C[i][j] + A[k][i] * B[k][j];
      }
    }
#pragma endscop

  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C_golden[i][j] = 0;
      for (int k = 0; k < K; k++) {
        C_golden[i][j] = C_golden[i][j] + A[i][k] * B[j][k];
//        C_golden[i][j] = C_golden[i][j] + A[k][i] * B[k][j];
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
