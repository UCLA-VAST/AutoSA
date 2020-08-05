// This kernel is copied from Polybench 4.2.1-beta
#include "kernel.h"

void init_array(data_t A[N][N])
{
  int i, j;

  for (i = 0; i < N; i++)
  {
    for (j = 0; j <= i; j++)
      A[i][j] = (data_t)(-j % N) / N + 1;
    for (j = i + 1; j < N; j++) {
      A[i][j] = 0;
    }
    A[i][i] = 1;
  }

  /* Make the matrix positive semi-definite. */
  /* not necessary for LU, but using same code as cholesky */
  int r, s, t;
  data_t B[N][N];
  for (r = 0; r < N; r++)
    for (s = 0; s < N; s++) 
      B[r][s] = 0;
  for (t = 0; t < N; t++)
    for (r = 0; r < N; r++)
      for (s = 0; s < N; s++)
        B[r][s] += A[r][t] * A[s][t];
  for (r = 0; r < N; r++)        
    for (s = 0; s < N; s++)
      A[r][s] = B[r][s];
}

int main(int argc, char **argv) {
  data_t A[N][N], A_golden[N][N];

  init_array(A);
  init_array(A_golden);

#pragma scop
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < i; j++) {
      for (int k = 0; k < j; k++) {
        A[i][j] -= A[i][k] * A[k][j];
      }
      A[i][j] /= A[j][j];
    }
//    for (int j = i; j < N; j++) {
//      for (int k = 0; k < i; k++) {
//        A[i][j] -= A[i][k] * A[k][j];
//      }
//    }
  }
#pragma endscop

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < i; j++) {
      for (int k = 0; k < j; k++) {
        A_golden[i][j] -= A_golden[i][k] * A_golden[k][j];
      }
      A_golden[i][j] /= A_golden[j][j];
    }
    for (int j = i; j < N; j++) {
      for (int k = 0; k < i; k++) {
        A_golden[i][j] -= A_golden[i][k] * A_golden[k][j];
      }
    }
  }  

  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      printf("%f ", A[i][j]);
    }
    printf("\n");
  }

  int err = 0;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      if (fabs((float)A_golden[i][j] - (float)A[i][j]) > 0.001)
        err++;
    }
  
  if (err)
    printf("Failed with %d errors!\n", err);
  else
    printf("Passed!\n");

  return 0;
}