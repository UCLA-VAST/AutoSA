int f(int k)
{
	int a;
#pragma scop
	__pencil_assume(k >= 0);
	a = k % 16;
#pragma endscop
	return a;
}
