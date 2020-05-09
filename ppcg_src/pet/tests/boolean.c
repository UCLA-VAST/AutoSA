void foo(int A[4])
{
#pragma scop
	A[0] = (A[1] && A[2]) || !A[3];
#pragma endscop
}
