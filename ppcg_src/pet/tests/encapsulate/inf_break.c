void foo(int A[1])
{
#pragma scop
	while (1) {
		int s = A[0];

		if (!s)
			break;
	}
#pragma endscop
}
