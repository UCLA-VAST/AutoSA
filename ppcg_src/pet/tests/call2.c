int f(int, int);

void foo(int i, int n, int A[const static n])
{
	A[i - 1] = 0;
	if (i < n)
		if (f(i, n))
			A[i] += 1;
}

void bar(int n, int B[const static n][n])
{
#pragma scop
	for (int i = 0; i < n; ++i)
		foo(i + 1, n, B[i]);
#pragma endscop
}
