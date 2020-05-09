int f();

void foo()
{
	int i, a[100];

#pragma scop
	for (i = f(); i < 100; ++i) {
		a[i] = 0;
		if (1)
			break;
		a[i] = 1;
	}
#pragma endscop
}
