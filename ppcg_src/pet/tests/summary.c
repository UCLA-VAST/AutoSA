int f(int i);
int maybe();

void set_odd_summary(int n, int A[static n])
{
	for (int i = 1; i < n; i += 2)
		if (maybe())
			A[i] = 0;
}

__attribute__((pencil_access(set_odd_summary)))
void set_odd(int n, int A[static n]);

void set_odd(int n, int A[static n])
{
	for (int i = 0; i < n; ++i)
		A[2 * f(i) + 1] = i;
}

void foo(int n, int A[static n][n])
{
#pragma scop
	for (int i = 0; i < n; ++i)
		set_odd(n, A[i]);
#pragma endscop
}
