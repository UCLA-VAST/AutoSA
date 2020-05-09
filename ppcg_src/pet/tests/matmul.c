void matmul(int M, int N, int K, float A[M][K], float B[K][N], float C[M][N])
{
	int i, j, k;

#pragma scop
#pragma live-out C
	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++) {
			C[i][j] = 0;
			for (k = 0; k < K; k++)
				C[i][j] += A[i][k] * B[k][j];
		}
#pragma endscop
}
