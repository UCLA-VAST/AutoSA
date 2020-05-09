void foo(int N)
{
	int i, j;
	int a;

#pragma scop
	for (i = 0; i <= 4; ++i)
		for (j = i; j <= 5 * ((j + 2)/5); ++j)
			a = 5;
#pragma endscop
}
