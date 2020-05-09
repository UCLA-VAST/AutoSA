inline void g(int *a)
{
	a[0] += 1;
}

void f()
{
#pragma scop
	int a[10];
	a[5] = 1;
	g(&a[5]);
#pragma endscop
}
