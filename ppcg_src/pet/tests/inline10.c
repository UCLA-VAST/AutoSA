inline void g(int b)
{
	b += 1;
}

void f()
{
#pragma scop
	int a = 1;
	g(a);
	g(a);
#pragma endscop
}
