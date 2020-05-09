void compute_row(int, int *);
int f(const int *);
int g(int, int);
int h(int);

int M;
int N;

void foo()
{
	int i, j;
	int in1[N][M];
	int in2[N];
	int A[N][10];
	#pragma value_bounds in2 "-1" "1"
	int C[N];
	int m;

#pragma scop
	for (i = 0; i < N; ++i) {
		m = i+1;
		for (j = 0; j < M; ++j)
			m = g(h(m), in1[i][j]);
		compute_row(h(m), A[i]);
	}
	A[5][6] = 0;
	for (i = 0; i < N; ++i)
		if (i + in2[i] >= 0 && i + in2[i] < N)
			C[i] = f(A[i + in2[i]]);
		else
			C[i] = 0;
#pragma endscop
}
