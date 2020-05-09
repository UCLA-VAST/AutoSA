int f(int k)
{
	int a[10];
#pragma scop
	for (int i = 0; i < 10; ++i) {
		__pencil_assume(k >= 0);
		a[i] = k % 16;
	}
#pragma endscop
	return a[0];
}
