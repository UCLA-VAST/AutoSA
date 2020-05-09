void cuervo(int n, int m, int *A, int *B, int *C)
{
#pragma scop
	for (int i = 0; i <= n; ++i)
		for (int j = 0; j <= m; ++j)
			C[i+j] = C[i+j] + A[i] * B[j];
#pragma endscop
}
