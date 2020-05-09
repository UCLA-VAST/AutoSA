void f(int n)
{
#pragma scop
	for (int i = 0; i < n; i++) {
		float a[n], b[n];

		a[i] = b[i];
	}
#pragma endscop
}
