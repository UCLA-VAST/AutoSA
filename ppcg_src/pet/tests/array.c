void f(const int A[10], int B[10]);

void foo(const int A[10], int B[10])
{
#pragma scop
	f(A, B);
#pragma endscop
}
