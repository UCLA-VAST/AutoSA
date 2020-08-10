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

void lu_cpu(data_t A[N][N], data_t L[N][N], data_t U[N][N]) {
  data_t prev_V[N][N][N];
  data_t V_tmp[N][N][N];
  data_t U_tmp[N][N][N];
  data_t L_tmp[N][N][N];

  for (int k = 0; k < N; k++)
    for (int j = k; j < N; j++)
      for (int i = k; i < N; i++) {
        if (k == 0)
          prev_V[i][j][k] = A[i][j];
        else
          prev_V[i][j][k] = V_tmp[i][j][k - 1];
        
        if (j == k) {
          U_tmp[i][j][k] = prev_V[i][j][k];
          U[j][i] = U_tmp[i][j][k];
        } else {
          U_tmp[i][j][k] = U_tmp[i][j - 1][k];

          if (i == k) {            
            L_tmp[i][j][k] = prev_V[i][j][k] / U_tmp[i][j - 1][k]; // final
            L[i][j] = L_tmp[i][j][k];
          } else {
            L_tmp[i][j][k] = L_tmp[i - 1][j][k];
          }
          V_tmp[i][j][k] = prev_V[i][j][k] - L_tmp[i][j][k] * U_tmp[i][j - 1][k];
        }
      }  
}

void lu_device(data_t A[N][N], data_t L[N][N], data_t U[N][N])
{
#pragma scop
  {
    data_t prev_V[N][N];  
    data_t V[N][N];
    data_t U_tmp[N][N];
    data_t L_tmp[N][N];

    for (int k = 0; k < N; k++) {    
      for (int j = k; j < N; j++)
        for (int i = k; i < N; i++) {
          if (k == 0)
            prev_V[i][j] = A[i][j];
          else
            prev_V[i][j] = V[i][j];          

          if (j == k) {          
            U_tmp[i][j] = prev_V[i][j]; 
            U[j][i] = U_tmp[i][j]; // final
          } else {          
            U_tmp[i][j] = U_tmp[i][j - 1];        

            if (i == k) {
              L_tmp[i][j] = prev_V[i][j] / U_tmp[i][j]; 
              L[i][j] = L_tmp[i][j]; // final
            } else {            
              L_tmp[i][j] = L_tmp[i - 1][j];
            }          
          
            V[i][j] = prev_V[i][j] - L_tmp[i][j] * U_tmp[i][j];
          }

        }
    }
  }
#pragma endscop
}

int main(int argc, char **argv) {
  data_t A[N][N], L[N][N], U[N][N], L_golden[N][N], U_golden[N][N];

  init_array(A);
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      L[i][j] = 0;
      U[i][j] = 0;
      L_golden[i][j] = 0;
      U_golden[i][j] = 0;
    }
    
  lu_device(A, L, U);
  lu_cpu(A, L_golden, U_golden);

  int err = 0;
  for (int i = 0; i < N; i++)
    for (int j = 0; j <= i; j++) {
      if (fabs((float)L_golden[i][j] - (float)L[i][j]) > 0.001)
        err++;
    }
  for (int i = 0; i < N; i++)
    for (int j = i; j < N; j++) {
      if (fabs((float)U_golden[i][j] - (float)U[i][j]) > 0.001)
        err++;
    }

  if (err)
    printf("Failed with %d errors!\n", err);
  else
    printf("Passed!\n");

  printf("A:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) 
      printf("%f ", A[i][j]);
    printf("\n");
  }

  printf("L_golden:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {      
      printf("%f ", (i == j)? 1.0 : L_golden[j][i]);      
    }
    printf("\n");
  }

  printf("U_golden:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      printf("%f ", U_golden[i][j]);
    }
    printf("\n");
  }

  printf("L:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {      
      printf("%f ", (i == j)? 1.0 : (j < i)? L[j][i] : 0.0);      
    }
    printf("\n");
  }

  printf("U:\n");
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
      printf("%f ", (j < i)? 0.0 : U[i][j]);
    }
    printf("\n");
  }

  return 0;    
}
