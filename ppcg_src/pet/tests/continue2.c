int f(void);

void foo()
{
	int i, j, a[100];

#pragma scop
	for (i = 0; i < 100; ++i) {
		if (i < 60) {
			if (a[i] > 5) {
				j = f();
				if (j == 0)
					continue;
				a[i] = i;
			} else
				a[i] = 0;
			j = f();
		} else
			a[i] = i;
		j = f();
		a[i] = a[i] + 1;
	}
#pragma endscop
}
