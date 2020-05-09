void foo()
{
	int a;

#pragma scop
A:	a = 5;
B:	a = 7;
#pragma endscop
}
