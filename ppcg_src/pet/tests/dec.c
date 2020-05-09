void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = N - 1; i >= 0; --i)
		a[i] = i;
#pragma endscop
}
