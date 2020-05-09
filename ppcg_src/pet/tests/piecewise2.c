void bar(int N, int a[N][N], int b[5][5])
{
	int i, j;

#pragma scop
	for (i = 0; i < N; ++i)
		for (j = i; j < N; ++j)
			a[i][j] = a[i - (i > 0)][0];
#pragma endscop
}
