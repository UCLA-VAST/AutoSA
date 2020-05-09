int t(void);
int f(void);
int g(int);

void foo(int N)
{
	int s;
	int v;

#pragma scop
	s = 0;
	while (t()) {
		for (int i = 0; i < 2 * N; ++i)
			s = s + 1;
	}
#pragma endscop
}
