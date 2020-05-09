int f();

void foo()
{
	unsigned char k;
	int a;

#pragma scop
	for (k = 252; (k % 9) <= 5; ++k) {
		if (k != 1)
			a = 5;
		if (f())
			break;
		a = k;
	}
#pragma endscop
}
