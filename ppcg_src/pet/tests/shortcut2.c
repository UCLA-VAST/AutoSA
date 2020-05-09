void foo(int N)
{
	int a;
#pragma scop
	if (5 * N > 0 && N < 100)
		a = 1;
#pragma endscop
}
