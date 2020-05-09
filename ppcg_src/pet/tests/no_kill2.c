int foo()
{
	int a;
	int *p;

	p = &a;
#pragma scop
	a = 5;
#pragma endscop
	return *p;
}
