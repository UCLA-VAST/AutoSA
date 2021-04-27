#include "kernel.h"

#define A_B 0
#define AT_B 1
#define A_BT 2
#define AT_BT 3
#define TRANS A_BT

int main(int argc, char **argv) {
#if TRANS == A_B 
  data_t A[I][K], B[K][J], C[I][J], C_golden[I][J]; 
#elif TRANS == AT_B
  data_t A[K][I], B[K][J], C[I][J], C_golden[I][J];
#elif TRANS == A_BT
  data_t A[I][K], B[J][K], C[I][J], C_golden[I][J];
#elif TRANS == AT_BT
  data_t A[K][I], B[J][K], C[I][J], C_golden[I][J];
#endif

  for (int i = 0; i < I; i++) 
    for (int k = 0; k < K; k++) {
#if TRANS == A_B 
      A[i][k] = (data_t)rand() / RAND_MAX;
#elif TRANS == A_BT
      A[i][k] = (data_t)rand() / RAND_MAX;
#elif TRANS == AT_B
      A[k][i] = (data_t)rand() / RAND_MAX;
#elif TRANS == AT_BT
      A[k][i] = (data_t)rand() / RAND_MAX;
#endif
    }

  for (int j = 0; j < J; j++)
    for (int k = 0; k < K; k++) {
#if TRANS == A_B
      B[k][j] = (data_t)rand() / RAND_MAX;
#elif TRANS == A_BT
      B[j][k] = (data_t)rand() / RAND_MAX;
#elif TRANS == AT_B
      B[k][j] = (data_t)rand() / RAND_MAX;
#elif TRANS == AT_BT
      B[j][k] = (data_t)rand() / RAND_MAX;
#endif     
    }

#pragma scop
  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C[i][j] = 0;
      for (int k = 0; k < K; k++) {
#if TRANS == A_B
        C[i][j] = C[i][j] + A[i][k] * B[k][j];
#elif TRANS == A_BT
        C[i][j] = C[i][j] + A[i][k] * B[j][k];
#elif TRANS == AT_B
        C[i][j] = C[i][j] + A[k][i] * B[k][j];
#elif TRANS == AT_BT
        C[i][j] = C[i][j] + A[k][i] * B[j][k];
#endif          
      }
    }
#pragma endscop

  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C_golden[i][j] = 0;
      for (int k = 0; k < K; k++) {
#if TRANS == A_B
        C_golden[i][j] = C_golden[i][j] + A[i][k] * B[k][j];
#elif TRANS == A_BT
        C_golden[i][j] = C_golden[i][j] + A[i][k] * B[j][k];
#elif TRANS == AT_B
        C_golden[i][j] = C_golden[i][j] + A[k][i] * B[k][j];
#elif TRANS == AT_BT
        C_golden[i][j] = C_golden[i][j] + A[k][i] * B[j][k];
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

//#include <stdio.h>
//int main(int argc, char **argv) {
//
//      float L2[1][10];
//      float FL[1][64];
//      float w2[64][10];
//#pragma scop
//      for (int j1 = 0; j1 < 10; ++j1) {
//        L2[0][j1] = 0.000000e+00f;
//        for (int k1 = 0; k1 < 64; ++k1) {
//          L2[0][j1] = (L2[0][j1] + (FL[0][k1] * w2[k1][j1]));
//        }
//      }
//#pragma endscop
//      printf("%f", L2[0][0]);
//      printf("%f", FL[0][0]);
//      printf("%f", w2[0][0]);
//}