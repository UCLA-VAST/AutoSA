void foo()
{
	int i;
	int a[10];

#pragma scop
	for (i = 0; i < 10; ++i)
		if (i < 5 || i > 6)
			a[i] = i;
#pragma endscop
}
