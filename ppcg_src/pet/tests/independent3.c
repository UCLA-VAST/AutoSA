void foo(int n, int A[n][n], int B[n][n])
{
#pragma scop
	for (int i = 0; i < n; ++i)
		#pragma pencil independent
		for (int j = 0; j < n; ++j) {
			float t = i + j;
			B[i][A[i][j]] = t;
		}
#pragma endscop
}
