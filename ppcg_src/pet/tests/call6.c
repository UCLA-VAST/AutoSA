void foo(int pos, int C[const static 1 + pos + 4])
{
	for (int i = 0; i < 4; ++i)
		C[1 + pos + i] += i;
}

void bar(int n, int A[const static n])
{
#pragma scop
	__pencil_assume(n >= 4);
	foo(-1, A);
#pragma endscop
}
