void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = 0; i < N; ++i)
		if (0)
			a[i] = i;
#pragma endscop
}
