void f(const int *a, int *b);

int foo()
{
	int b[11];
	int a;
	int i;

#pragma scop
	for (i = 0; i < 10; i++) {
		f(&a, &a);
		f(&b[i], &b[i+1]);
	}
#pragma endscop

	return a;
}
