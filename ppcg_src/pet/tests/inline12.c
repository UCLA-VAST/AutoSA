inline int select(int n, int a[n], int i)
{
	return a[i];
}

void foo(int n, int a[n])
{
#pragma scop
	for (int i = 0; i + 1 < n; ++i) {
		a[i] = select(n, a, i) + select(n, a, i + 1);
	}
#pragma endscop
}
