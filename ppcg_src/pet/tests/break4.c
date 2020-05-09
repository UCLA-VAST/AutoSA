int f();

void foo()
{
	int i, j, a[100];

#pragma scop
	for (i = 0; i < 100; ++i)
		for (j = 0; j < 100; ++j) {
			a[i] = 0;
			if (j > 80) {
				if (f())
					break;
			}
			if (f()) {
				if (f())
					break;
				a[i] = 1;
				if (f())
					continue;
				else
					a[i] = 2;
				a[i] = 3;
			}
			if (f())
				break;
			a[i] = i + j;
		}
#pragma endscop
}
