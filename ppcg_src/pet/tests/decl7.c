void f(int n, float c[n])
{
#pragma scop
	float a[n];

	for (int i = 0; i < n; i++)
		a[i] = c[i];

	for (int i = 0; i < n; i++) {
		float b[n][n];
		b[i][i] = a[i] + 1;
		a[i] = b[i][i];
	}
	for (int i = 0; i < n; i++) {
		float b[n][n];
		b[i][i] = a[i] + 1;
		a[i] = b[i][i];
	}

	for (int i = 0; i < n; i++)
		c[i] = a[i];
#pragma endscop
}
