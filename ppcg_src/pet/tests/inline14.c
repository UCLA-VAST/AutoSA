inline void g(int a)
{
	a += 1;
}

inline int h(int a)
{
	return a + 1;
}

void f()
{
#pragma scop
	int a = 1;
	g(h(a));
#pragma endscop
}
