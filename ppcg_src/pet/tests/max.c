void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = max(0, N - 10); i < N; ++i)
		a[i] = i;
#pragma endscop
}
