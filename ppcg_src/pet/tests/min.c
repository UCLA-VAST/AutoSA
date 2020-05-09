void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = 0; i < min(N, 2 * N - 10); ++i)
		a[i] = i;
#pragma endscop
}
