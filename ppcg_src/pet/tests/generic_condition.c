void foo(int N)
{
	int i;
	int a;

#pragma scop
	for (i = 0; min(i,N); ++i)
		a = 5;
#pragma endscop
}
