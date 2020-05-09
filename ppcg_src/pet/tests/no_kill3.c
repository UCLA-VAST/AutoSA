int foo()
{
	int a[1];

#pragma scop
	a[0] = 5;
#pragma endscop
	return a[0];
}
