inline void f1(int n)
{
}

inline void f2(int n)
{
	float A[n];
}

void f(int s)
{
#pragma scop
	f1(s);
	f2(s);
#pragma endscop
}
