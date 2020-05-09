int f(int i);
int maybe();

struct s {
	int a;
};

void set_odd_summary(int n, struct s A[static n])
{
	for (int i = 1; i < n; i += 2)
		if (maybe())
			A[i].a = 0;
}

__attribute__((pencil_access(set_odd_summary)))
void set_odd(int n, struct s A[static n]);

void set_odd(int n, struct s A[static n])
{
	for (int i = 0; i < n; ++i)
		A[2 * f(i) + 1].a = i;
}

void foo(int n, struct s A[static n])
{
#pragma scop
	set_odd(n, A);
#pragma endscop
}
