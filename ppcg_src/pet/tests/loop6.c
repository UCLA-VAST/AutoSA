void foo(int n)
{
	int a[n];

#pragma scop
	for (int i = 0; i <= n; ++i)
		if (i <= 2147483547)
			a[i + 100 - 100] = 0;
#pragma endscop
}
