inline void g(int n, int a[n])
{
	for (int i = 0; i < n; ++i)
		a[i] = 0;
}

void f(int n, int m, int a[n][m])
{
#pragma scop
	for (int i = 0; i < n; ++i)
		g(m - 2, &a[i][1]);
#pragma endscop
}
