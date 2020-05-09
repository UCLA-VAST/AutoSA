void foo(int n, int A[n], int B[n][n])
{
#pragma scop
	#pragma pencil independent
	for (int i = 0; i < n; ++i)
		for (int j = 0; j < n; ++j)
			B[A[i]][j] = i + j;
#pragma endscop
}
