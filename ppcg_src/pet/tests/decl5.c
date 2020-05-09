void f(int n)
{
#pragma scop
	float a[n];

	for (int i = 0; i < n; i++) {
		float b[n][n];
	}
#pragma endscop
}
