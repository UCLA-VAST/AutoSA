void foo(int C[const static 4])
{
	for (int i = 0; i < 4; ++i)
		C[i] += i;
}

void bar(int n, int A[const static n])
{
#pragma scop
	__pencil_assume(n % 4 == 0);
	for (int i = 0; i < n; i += 4)
		foo(&A[i]);
#pragma endscop
}
