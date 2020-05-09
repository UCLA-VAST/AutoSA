void foo(int n, int A[n], int C[n])
{
	int t;
	int B[n];
	int D[n][n];
#pragma scop
	__pencil_kill(C);
	for (int i = 0; i < n; ++i) {
		t = A[i];
		B[i] = t;
		C[i] = B[i];
		__pencil_kill(t, B[i], D[i]);
	}
	__pencil_kill(A);
#pragma endscop
}
