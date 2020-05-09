int f();

void foo(int N)
{
	int i;
	int a;

#pragma scop
	while (1) {
		a = 5;
		if (N)
			break;
		a = 6;
	}
	while (1) {
		a = 5;
		if (f())
			break;
		a = 6;
	}
	while (f()) {
		a = 5;
		if (N)
			break;
		a = 6;
	}
	while (f()) {
		a = 5;
		if (f())
			break;
		a = 6;
	}
#pragma endscop
}
