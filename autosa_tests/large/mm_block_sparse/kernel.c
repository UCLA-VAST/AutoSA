/* This example uses the block sparsity to compute a matrix multiplication.
 * C = A * B
 * The matrix A is with block sparsity and the matrix B is dense.
 * For matrix A, every VEC_LEN elements are grouped into a vector.
 * Inside each vector, there are NUM_NZERO non-zero elements.
 * The sparsity of the matrix A is computed as 1 - NUM_NZERO / VEC_LEN.
 * To store the sparse matrix A, we use two data structs,
 * A_d for storing the non-zero elements and A_i for storing the offset of non-zero elements in each vector.
 * As an example, for matrix A of size I * K, where I = K = 8,
 * suppose that we have VEC_LEN = 4 and NUM_NZERO = 2, we denote the compression ratio
 * COMPRESS_RATIO = VEC_LEN / NUM_NZERO
 * then, we will have A_d[I][K / COMPRESS_RATIO],
 * for A_i, we use a char to store the mask of non-zero elements.
 * For example, if the vector is 0 1 0 2, we will have a mask 0101_0000 to store the 
 * offsets of non-zero elements.
 * Currently, we assume the vector length is a power of two and is no greater than 8.
 * If it is grater than 8, we could use a larger-width data type to store the offset accordingly.
 * Based on the analysis above, we will have the index matrix A_i as
 * char A_i[I][K / VEC_LEN].
 * In summary, we use A_d[I][K / COMPRESS_RATIO] and A_i[I][K / VEC_LEN] to represent the sparse matrix.
 */
#include "kernel.h"

int main(int argc, char **argv) {
  static data_t A[I][K], B[J][K], C[I][J], C_golden[I][J];
  static data_t A_d[I][K / COMPRESS_RATIO];
  static unsigned char A_i[I][K / VEC_LEN];
  static data_t A_s[I][K / EFF_COMPRESS_RATIO];

  for (int i = 0; i < I; i++) 
    for (int k = 0; k < K; k++) {
      A[i][k] = (data_t)rand() / RAND_MAX;
    }

  for (int j = 0; j < J; j++)
    for (int k = 0; k < K; k++) {
      B[j][k] = (data_t)rand() / RAND_MAX;
    }

  for (int i = 0; i < I; i++)
    for (int k = 0; k < K / VEC_LEN; k++) {
      unsigned char offset = 0;
      int n = 0;
      while (n < NON_ZERO_NUM) {      
        int pos = rand() % VEC_LEN;
        /* Check if this position is already inserted */        
        unsigned char cur_mask = offset & (1 << pos);
        if (cur_mask) {
          continue;
        }
        offset = offset | (1 << pos);
        n++;
      }
      A_i[i][k] = offset;

      int pos = 0;
      int non_zero_pos = 0;
      while (pos < VEC_LEN) {
        unsigned char cur_mask = offset & (1 << pos);
        if (cur_mask) {
          A_d[i][k * NON_ZERO_NUM + non_zero_pos] = A[i][k * VEC_LEN + pos];
          non_zero_pos++;
        }
        pos++;
      }      
    }

  for (int i = 0; i < I; i++)
    for (int k = 0; k < K / VEC_LEN; k++) {
      int n;
      for (n = 0; n < NON_ZERO_NUM; n++) {
        A_s[i][k * (NON_ZERO_NUM + META_DATA_NUM) + n] = A_d[i][k * NON_ZERO_NUM + n];
      }
      unsigned char offset = A_i[i][k];
      union {data_t d; unsigned char c;} u;
      u.c = offset;
      A_s[i][k * (NON_ZERO_NUM + META_DATA_NUM) + n] = u.d;
    }

  /* For polyheral analysis */
#pragma scop
  for (int i = 0; i < I; i++)
    for (int j = 0; j < J; j++) {
      C[i][j] = 0;
      for (int k = 0; k < K; k++) {
        C[i][j] = C[i][j] + A[i][k] * B[j][k];
      }
    }
#pragma endscop

//  /* The actual computation */
//  for (int i = 0; i < I; i++)  
//    for (int j = 0; j < J; j++) {
//      C[i][j] = 0;
//      for (int k = 0; k < K / VEC_LEN; k++) {
//        /* Extract the non zero offset */
//        int offset[NON_ZERO_NUM];
//        unsigned char mask = A_i[i][k];
//        int pos = 0;
//        int non_zero_pos = 0;
//        while (pos < VEC_LEN) {
//          unsigned char cur_mask = mask & (1 << pos);
//          if (cur_mask) {
//            offset[non_zero_pos] = pos;
//            non_zero_pos++;
//          }
//          pos++;
//        }
//        for (int n = 0; n < NON_ZERO_NUM; n++) {
//          C[i][j] += A_d[i][k * NON_ZERO_NUM + n] * B[j][k * VEC_LEN + offset[n]];
//        }
//      }
//    }

  for (int i = 0; i < I; i++)  
    for (int j = 0; j < J; j++) {
      C_golden[i][j] = 0;
      for (int k = 0; k < K / VEC_LEN; k++) {
        /* Extract the non zero offset */
        int offset[NON_ZERO_NUM];
        unsigned char mask = A_i[i][k];
        int pos = 0;
        int non_zero_pos = 0;
        while (pos < VEC_LEN) {
          unsigned char cur_mask = mask & (1 << pos);
          if (cur_mask) {
            offset[non_zero_pos] = pos;
            non_zero_pos++;
          }
          pos++;
        }
        for (int n = 0; n < NON_ZERO_NUM; n++) {
          C_golden[i][j] += A_d[i][k * NON_ZERO_NUM + n] * B[j][k * VEC_LEN + offset[n]];
        }
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
