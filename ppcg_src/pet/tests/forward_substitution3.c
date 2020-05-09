void foo()
{
	int a[10];
#pragma scop
	int b = 1;
	int c = b;
	b = 2;
	a[c] = 5;
#pragma endscop
}
