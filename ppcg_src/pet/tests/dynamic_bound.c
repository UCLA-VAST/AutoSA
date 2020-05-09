int f();
int g();
void h(int, int, int);

void foo()
{
	int N, M;
	int a[100][100];
#pragma value_bounds N 0 100
#pragma value_bounds M 0 100

#pragma scop
	N = f();
	M = g();
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < M; ++j)
			a[i][j] = i + j;
	for (int i = 0; i < N; ++i)
		for (int j = 0; j < M; ++j)
			h(i, j, a[i][j]);
#pragma endscop
}
