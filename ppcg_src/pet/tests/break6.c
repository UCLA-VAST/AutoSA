int f();

void foo()
{
	int i, a[100];

#pragma scop
	for (i = f(); i < 100; ++i) {
		a[i] = 0;
		if (f())
			break;
		a[i] = 1;
		if (f())
			continue;
		a[i] = 2;
	}
#pragma endscop
}
