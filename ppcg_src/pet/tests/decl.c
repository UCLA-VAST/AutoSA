void matmul(int M, int N, int K, float A[M][K], float B[K][N], float C[M][N])
{
	int i, j, k;

#pragma scop
	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++) {
			C[i][j] = 0;
			for (k = 0; k < K; k++) {
				float t = A[i][k] * B[k][j];
				C[i][j] += t;
			}
		}
#pragma endscop
}
