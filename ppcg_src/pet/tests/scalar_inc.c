void foo()
{
	int a;

#pragma scop
	a = 5;
	a++;
#pragma endscop
}
