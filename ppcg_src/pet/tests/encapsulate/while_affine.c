void foo(int N)
{
	int i, j, a[N], b[N];

#pragma scop
	for (i = 0; i < 100; ++i)
		while (i > 98) {
			for (j = 0; j < N; ++j)
				a[j] = 0;
			for (j = 0; j < N; ++j)
				b[j] = a[j];
		}
#pragma endscop
}
