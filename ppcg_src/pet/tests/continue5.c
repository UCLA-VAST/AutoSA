int f(void);

void foo()
{
	int i, j, a[100];

#pragma scop
	for (i = 0; i < 100; ++i) {
		j = f();
		if (i == 57)
			continue;
		a[i] = i;
		j = f();
		if (j == 0)
			continue;
		a[i] = a[i] + 1;
	}
#pragma endscop
}
