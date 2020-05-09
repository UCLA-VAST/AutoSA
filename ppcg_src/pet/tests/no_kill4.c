int foo()
{
	int a[1];
	int *p;

	p = a;
#pragma scop
	a[0] = 5;
#pragma endscop
	return *p;
}
