int f(void);

void foo()
{
	int i, j, a[100];

#pragma scop
	for (i = 0; i < 100; ++i) {
		j = f();
		a[i] = j ? 40 : 90;
	}
#pragma endscop
}
