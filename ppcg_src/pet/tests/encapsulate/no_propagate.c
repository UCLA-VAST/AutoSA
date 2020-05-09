void f(int n, int A[n * n])
{
#pragma scop
	int i = 0;

	for (i = 0; i < n * n; ++i)
		A[i] = 0;
#pragma endscop
}
