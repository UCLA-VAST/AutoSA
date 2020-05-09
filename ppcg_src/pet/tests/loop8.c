void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = 0; i < 10; ++i)
		a[N - 1] = N;
#pragma endscop
}
