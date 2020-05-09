int f(int k)
{
	int a;
#pragma scop
	__pencil_assume(k >= 0);
	k = -1;
	a = k % 16;
#pragma endscop
	return a;
}
