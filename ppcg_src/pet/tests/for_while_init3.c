void foo(int n, int A[n])
{
	int s;

#pragma scop
	s = 0;
	for (int i = 0; i < n; ++i)
		for (int j = A[i]; j < 10; ++j)
			s++;
#pragma endscop
}
