int t(void);
int f(void);
int g(int);

void foo(int N)
{
	int s;
	int v;

#pragma scop
	s = 0;
	for (int T = 0; t(); ++T) {
		for (int i = 0; i < 2 * N; ++i)
			s = s + 1;
	}
#pragma endscop
}
