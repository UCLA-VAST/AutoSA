int f(void);
int P(int, int);
int g(int);
void h(int);

void foo(int n)
{
	int s;

#pragma scop
	for (int x = 0; x < n; ++x) {
S1:		s = f();
		while (P(x, s)) {
S2:			s = g(s);
		}
R:		h(s);
	}
#pragma endscop
}
