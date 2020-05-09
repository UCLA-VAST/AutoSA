int foo()
{
	int a;
#pragma scop
	a = 5;
#pragma endscop
	return a;
}
