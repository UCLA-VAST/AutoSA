int f(int);
int g(int);
int N;

void foo()
{
	int i;
	int in;
	int A[N + 1];
	int out;

#pragma scop
	if (N >= 0) {
		A[0] = in;
		for (i = 1; i <= N; ++i) {
			A[i] = f(g(A[i/2]));
		}
		out = g(A[N]);
	}
#pragma endscop
}
