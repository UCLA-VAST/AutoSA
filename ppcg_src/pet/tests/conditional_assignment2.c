void compute_row(int, int *);
int f(const int *);
int g(int, int);
int g2();
int h(int);

int M;
int N;

void foo()
{
	int i, j;
	int in1[N][M];
	int in2;
	int A[N][10];
	#pragma value_bounds in2 "-1" "1"
	int C[N];
	int m;

#pragma scop
	in2 = g2();
	for (i = 0; i < N; ++i) {
		m = i+1;
		for (j = 0; j < M; ++j)
			m = g(h(m), in1[i][j]);
		compute_row(h(m), A[i]);
	}
	A[5][6] = 0;
	for (i = 0; i < N; ++i)
		if (i + in2 >= 0 && i + in2 < N)
			C[i] = f(A[i + in2]);
		else
			C[i] = 0;
#pragma endscop
}
