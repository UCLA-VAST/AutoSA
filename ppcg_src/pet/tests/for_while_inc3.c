int f(void);
int P(int, int);
int g(int);
void h(int);

void foo(int n)
{
	int s;

#pragma scop
	for (int x1 = 0; x1 < n; ++x1) {
S1:		s = f();
		for (int x2 = 0; P(x1, x2); x2 += n) {
S2:			s = g(s);
		}
R:		h(s);
	}
#pragma endscop
}
