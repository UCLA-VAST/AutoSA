void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	i = 0;
	for (i = 0; i < N; i += i)
		a[i] = i;
#pragma endscop
}
