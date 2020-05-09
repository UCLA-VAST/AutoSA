struct s {
	int a;
};

void foo(int A[10], struct s s)
{
#pragma scop
	A[s.a] = 5;
#pragma endscop
}
