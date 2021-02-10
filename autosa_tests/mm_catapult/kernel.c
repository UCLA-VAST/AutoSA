#include "kernel.h"

int main(int argc, char **argv) {
  data_t A[I_P][K_P], B[J_P][K_P], C[I_P][J_P], C_golden[I_P][J_P]; // gemm0,3

  for (int i = 0; i < I_P; i++) 
    for (int k = 0; k < K_P; k++) {
      //A[i][k] = (data_t)rand() / RAND_MAX;
      A[i][k] = (data_t)1;
    }

  for (int j = 0; j < J_P; j++)
    for (int k = 0; k < K_P; k++) {
      //B[j][k] = (data_t)rand() / RAND_MAX;
      B[j][k] = (data_t)1;
    }

#pragma scop
  for (int i = 0; i < I_P; i++)
    for (int j = 0; j < J_P; j++) {
      C[i][j] = 0;
      for (int k = 0; k < K_P; k++) {
        C[i][j] = C[i][j] + A[i][k] * B[j][k];
      }
    }
#pragma endscop

  for (int i = 0; i < I_P; i++)
    for (int j = 0; j < J_P; j++) {
      C_golden[i][j] = 0;
      for (int k = 0; k < K_P; k++) {
        C_golden[i][j] = C_golden[i][j] + A[i][k] * B[j][k];
      }
    }

  int err = 0;
  for (int i = 0; i < I_P; i++)
    for (int j = 0; j < J_P; j++) {
      if (fabs((float)C_golden[i][j] - (float)C[i][j]) > 0.001)
        err++;
    }

  if (err)
    printf("Failed with %d errors!\n", err);
  else
    printf("Passed!\n");

  return 0;
}
