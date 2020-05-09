void foo()
{
	int i;
	unsigned int j;
	int m;
	unsigned int n;
	int a[20];
	int b[20];

#pragma scop
	for (i = 0; i < imax(m, 10); ++i)
		a[i] = i;

	for (j = 0; j < umax(n, 10); ++j)
		b[j] = j;
#pragma endscop
}
