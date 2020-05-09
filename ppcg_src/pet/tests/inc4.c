void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = 0; i < N; i += (i + 100) - (i + 99))
		a[i] = i;
#pragma endscop
}
