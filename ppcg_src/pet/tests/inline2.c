inline void g(int *a)
{
	a[0] += 1;
}

void f()
{
#pragma scop
	int a = 1;
	g(&a);
#pragma endscop
}
