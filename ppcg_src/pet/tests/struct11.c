struct s {
	int a;
	int b;
};

void foo(int A[10], struct s s)
{
#pragma scop
	s.a = 0;
	s.b = 1;
	A[s.a] = 5;
#pragma endscop
}
