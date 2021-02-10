/* This example uses the block sparsity to compute the matrix multiplication C = A * B.
 * The matrix A is with block sparsity and the matrix B is dense.
 * For matrix A, every VEC_LEN elements are grouped into a vector.
 * Inside each vector, there are NON_ZERO_NUM non-zero elements.
 * The sparsity of the matrix A is computed as 1 - NON_ZERO_NUM / VEC_LEN.
 * We use the matrix A_s to store both the data and index of the sparse matrix A.
 * 
 * For each vector group, we use an unsigned char to record the relative position
 * of the non-zero element in the group.
 * At present, we assume the vector group size to be a power of two and is no greater than 8.
 * Then every NON_ZERO_NUM non-zero elements and their index are grouped together and 
 * store in the A_s. 
 * However, to make the data structure aligned, we will also pad this group if necessary.
 * For example, if the group size VEC_LEN is 8, and NON_ZERO_NUM is 4, we will concatenate the 
 * index right after the first 4 data elements, resulting in 5 elements. 
 * Furthermore, we will pad this group and extend it to 8 elements. 
 * In this case, the effective storage for matrix A is the same with the unsparsified one.
 * If the group size VEC_LEN is 8, and NON_ZERO_NUM is 3, we will concatenate the 
 * index after the first 3 elements, resulting in 4 elements. No further padding is needed.
 * The effective storage compression ratio for matrix A is 8/4 = 2x for this example.
 * In summary, we denote the number of elements other than the data elements as META_DATA_NUM.
 * And it can be computed as:
 * META_DATA_NUM = 2^{ceil(log2(NON_ZERO_NUM + 1))} - NON_ZERO_NUM
 */
#include "kernel.h"

int main(int argc, char **argv) {
  data_t A[I][K], B[J][K], C[I][J], C_golden[I][J];

  data_t A_d[I][K / COMPRESS_RATIO];
  unsigned char A_i[I][K / VEC_LEN];

  data_t A_s[I][K / EFF_COMPRESS_RATIO];

  /* Initialize the matrix */
  for (int i = 0; i < I; i++) 
    for (int k = 0; k < K; k++) {
      A[i][k] = (data_t)rand() / RAND_MAX;
    }

  for (int j = 0; j < J; j++)
    for (int k = 0; k < K; k++) {
      B[j][k] = (data_t)rand() / RAND_MAX;
    }

  /* Generate the random sparse matrix */
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

  /* Generate the matrix to store both the sparse data and index */
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

  /* The actual computation */
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

  /* Compute the golden reference */
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

  /* Compare the results */
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
