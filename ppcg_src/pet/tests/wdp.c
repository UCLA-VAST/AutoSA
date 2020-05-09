int N;
#pragma parameter N 8 16

int _Source_x(void);
void _Source_yt(int *y, int *t);
int _Source_z(void);
int F1(int);
int F2(int);
int F3(int);
void F4(int, int, int *, int *);

void wdp(void)
{
	int i;
	int x[N], y[N + 1], t[N + 1], z[N + 2];

#pragma scop
	for (i = 0; i < N; ++i)
		x[i] = _Source_x();

	for (i = 0; i < N + 1; ++i)
		_Source_yt(&y[i], &t[i]);

	for (i = 0; i < N+2; ++i)
		z[i] = _Source_z();

	for (i = 0; i < N; ++i) {
		if (z[i] == 0)
			x[i] = F1(x[i]);
		if (x[i] * x[i] > 100) {
			y[i + 1] = F2(y[i]);
			t[i] = F3(t[i]);
		}
		F4(y[i + 1], z[i], &y[i + 1], &z[i + 2]);
	}
#pragma endscop
}
