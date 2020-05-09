void foo(int N)
{
	int a[10], b, c;
#pragma scop
	c = N;
	N = 2;
	a[c] = 5;
#pragma endscop
}
