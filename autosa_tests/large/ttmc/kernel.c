/*
 * This code implements the Chain of Tensor-matrix multiplications (TTMc), which performs:
 * D(i,j,k) += A(i,l,m) * B(l,j) * C(m,k)
 * Input: A[I][L][M], B[L][J], C[M][K]
 * Output: D[I][J][K]
 */

#include "kernel.h"

int main(int argc, char **argv){
  // declarations
  static data_t A[I][L][M];
  static data_t B[L][J];
//  static data_t C[M][K];
  static data_t C[K][M];
  static data_t D[I][J][K];
  static data_t D_golden[I][J][K];

  // data initialization
  for (int i = 0; i < I; i++)
    for (int l = 0; l < L; l++) 
      for (int m = 0; m < M; m++) {
        A[i][l][m] = 2.5;
      }
  for (int l = 0; l < L; l++)
    for (int j = 0; j < J; j++) {
      B[l][j] = 2.5;
    }
  for (int m = 0; m < M; m++)
    for (int k = 0; k < K; k++) {
//      C[m][k] = 2.5;
      C[k][m] = 2.5;
    }
  
  // computation
#pragma scop
  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) 
      for (int k = 0; k < K; k++) {
        D[i][j][k] = 0;        
        for (int l = 0; l < L; l++) 
          for (int m = 0; m < M; m++) {
//            D[i][j][k] = D[i][j][k] + A[i][l][m] * B[l][j] * C[m][k];
            D[i][j][k] = D[i][j][k] + A[i][l][m] * B[l][j] * C[k][m];
          }
      }    
#pragma endscop

  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) 
      for (int k = 0; k < K; k++) {
        D_golden[i][j][k] = 0;        
        for (int l = 0; l < L; l++) 
          for (int m = 0; m < M; m++) {
//            D_golden[i][j][k] += A[i][l][m] * B[l][j] * C[m][k];
            D_golden[i][j][k] += A[i][l][m] * B[l][j] * C[k][m];
          }
      }    

  // comparison
  int err = 0;
  float thres = 0.001;
  for (int i = 0; i < I; i++) 
    for (int j = 0; j < J; j++) 
      for (int k = 0; k < K; k++) {
        if (fabs(D_golden[i][j][k] - D[i][j][k]) > thres) {
          err++;
        }
      }

  if (err) {
    printf("Test failed with %d errors!\n", err);
    return -1;
  } else {
    printf("Test passed!\n");
    return 0;
  }
}
