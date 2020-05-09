void foo(int n, int A[n], int B[n], int C[n])
{
#pragma scop
	#pragma pencil independent
	for (int i = 0; i < n; ++i)
		for (int j = C[i]; j < n; ++j)
			B[A[i]] += j;
#pragma endscop
}
