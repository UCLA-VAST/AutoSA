void foo(int N)
{
#pragma scop
	int A[N];
	A[0] = 1;
#pragma endscop
}
