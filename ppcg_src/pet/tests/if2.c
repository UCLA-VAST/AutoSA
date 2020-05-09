void foo()
{
	int a;

#pragma scop
	if ((1 < 2) < 3)
		a = 1;
#pragma endscop
}
