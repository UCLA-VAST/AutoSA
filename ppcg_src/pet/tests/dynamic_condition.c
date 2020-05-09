int f(void);

void foo()
{
    int i, j, a[100];

#pragma scop
    for (i = 0; i < 100; ++i) {
	j = f();
	if (j <= 1) {
	    if (j >= 0)
		a[i] = i;
	}
    }
#pragma endscop
}
