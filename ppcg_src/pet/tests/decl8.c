void f(int n, float b[n])
{
#pragma scop
	float a[n];

	for (int i = 0; i < n; i++)
		a[i] = b[i];

	for (int i = 0; i < n; i++) {
		float b[n][n];
		b[i][i] = a[i] + 1;
		a[i] = b[i][i];
	}

	for (int i = 0; i < n; i++)
		b[i] = a[i];
#pragma endscop
}
