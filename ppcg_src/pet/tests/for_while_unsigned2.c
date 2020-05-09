int f(void);
int P(int, int);
int g(int);
void h(int);

void foo(int n, int a[256][256])
{
	int s;

#pragma scop
	for (int x1 = 0; x1 < n; ++x1) {
S1:		s = f();
		for (unsigned char x2 = 9; P(x1, x2); x2 -= 3) {
			for (int x3 = 0; x3 <= x2; ++x3)
S2:				s = g(s + a[x2][255 - x2]);
		}
R:		h(s);
	}
#pragma endscop
}
