void foo(int N)
{
	int a;
#pragma scop
	if (N < 100 && 5 * N > 0)
		a = 1;
#pragma endscop
}
