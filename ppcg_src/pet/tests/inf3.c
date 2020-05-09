void foo(int A[100])
{
	int i;
#pragma scop
	i = 5;
	for (;;) {
		A[i] = 6;
		i = 7;
	}
#pragma endscop
}
