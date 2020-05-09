void compute_row(int, int *);
int f(const int *);
int g(int, int);
int h(int);

int M;
#pragma parameter M 10 1000
int N;
#pragma parameter N 10 1000

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
	for (i = 0; i < N - 2; ++i)
		C[i] = f(A[i + 1 + in2[i]]);
#pragma endscop
}
