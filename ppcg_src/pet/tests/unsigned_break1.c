void foo()
{
	unsigned char k;
	int a;

#pragma scop
	for (k = 252; (k % 9) <= 5; ++k) {
		a = 5;
		if (k == 1)
			break;
		a = 6;
	}
#pragma endscop
}
