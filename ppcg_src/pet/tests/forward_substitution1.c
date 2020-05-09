void foo()
{
	int a[10], b, c;
#pragma scop
	b = 1;
	c = b;
	b = 2;
	a[c] = 5;
#pragma endscop
}
