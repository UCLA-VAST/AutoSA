void foo(int n, int A[static n][n])
{
#pragma scop
	for (int i = 0; i < n; ++i)
		for (int j = 0; j < n; ++j)
			A[i][j] = 0;
#pragma endscop
}
