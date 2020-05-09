void foo(int n, int m)
{
	int a;

#pragma scop
	for (int i = n; i + 1 <= m + 1; ++i)
		a = 5;
#pragma endscop
}
