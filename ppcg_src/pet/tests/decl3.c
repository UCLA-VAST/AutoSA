void foo(int N)
{
#pragma scop
	N = 5;
	int A[N];
	A[0] = 1;
#pragma endscop
}
