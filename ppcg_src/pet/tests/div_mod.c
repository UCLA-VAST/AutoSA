void foo()
{
	int i;
	int a;

#pragma scop
	for (i = -10; i < 10; ++i) {
		if (i == 5 / -2)
			a = 5 / -2;
		if (i == -5 / -2)
			a = -5 / -2;
		if (i == 5 % -2)
			a = 5 % -2;
		if (i == -5 % -2)
			a = -5 % -2;
	}
#pragma endscop
}
