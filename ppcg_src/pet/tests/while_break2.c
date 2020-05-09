int f();

void foo(int N)
{
	int i;
	int a;

#pragma scop
	for (i = 0; i < 10; ++i) {
		while (f())
			a = 5;
		if (N)
			break;
		a = 6;
	}
#pragma endscop
}
