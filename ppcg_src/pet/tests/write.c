void foo(int n, float A[static const restrict n][n]);

void bar(int n, float A[static const restrict n][n])
{
#pragma scop
	foo(n, A);
#pragma endscop
}
