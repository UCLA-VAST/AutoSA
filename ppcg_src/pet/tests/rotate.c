void rotate(int N, int A[N], int B[N])
{
#pragma scop
	for (int i = 0; i < N; ++i)
		A[i] = i == 0 ? B[N - 1] : B[i - 1];
#pragma endscop
}
