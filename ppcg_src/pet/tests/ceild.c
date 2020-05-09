void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = ceild(N, 3); i < floord(N, 2); ++i)
		a[i] = i;
#pragma endscop
}
