void bar(int N, int a[N][N], int b[5][5])
{
	int i, j;
	int ind;

#pragma scop
	for (i = 0; i < N; ++i)
		for (j = i; j < N; ++j) {
			ind = i > 0 ? i - 1 : i;
			a[i][j] = a[ind][0];
		}
#pragma endscop
}
