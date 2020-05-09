void foo()
{
	int a;

#pragma scop
	a = 5;
#pragma endscop
}
