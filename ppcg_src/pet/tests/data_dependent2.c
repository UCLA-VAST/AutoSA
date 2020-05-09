int f(void);

void foo()
{
	int i, j;
	int a[10];
	int N;

#pragma scop
	N = 5;
	for (i = 0; i < 10; ++i)
		if (N <= 4)
			for (j = 0; j < 10; ++j) {
				a[j] = 5 + j + N;
				N = f();
			}
#pragma endscop
}
