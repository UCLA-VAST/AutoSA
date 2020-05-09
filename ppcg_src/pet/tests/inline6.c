inline void add2(int *a)
{
	int t = 2;
	a[0] += t;
}

inline void add3(int *a)
{
	int t = 3;
	a[0] += t;
}

void foo()
{
	int i;
	int a[10];

#pragma scop
	for (i = 0; i < 10; ++i) {
		a[i] = 0;
		add2(&a[i]);
		add3(&a[i]);
	}
#pragma endscop
}
