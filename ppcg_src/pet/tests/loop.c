void foo()
{
	int i;
	int a;

#pragma scop
	for (i = 0; i < 10; ++i)
		a = 5;
#pragma endscop
}
