void foo(int N)
{
	int a[10], b, c;
#pragma scop
	c = 1;
	if (N == 2)
		c = 2;
	a[c] = 5;
#pragma endscop
}
