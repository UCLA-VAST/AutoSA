void foo(int T[100])
{
	int i;
#pragma scop
	i = 0;
	while (i < 100) {
		T[i] = i;
		i++;
	}
#pragma endscop
}
