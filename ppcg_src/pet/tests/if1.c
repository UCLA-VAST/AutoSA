void foo()
{
	int a;

#pragma scop
	if (1 << 0)
		a = 1;
#pragma endscop
}
