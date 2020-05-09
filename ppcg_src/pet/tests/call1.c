void foo(int pos, int C[const static pos + 4])
{
	for (int i = 0; i < 4; ++i)
		C[pos + i] += i;
}

void bar(int n, int A[const static n])
{
#pragma scop
	__pencil_assume(n % 4 == 0);
	for (int i = 0; i < n; i += 4)
		foo(i, A);
#pragma endscop
}
