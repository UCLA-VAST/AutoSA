void foo(int N)
{
	int i;
	int a[N];

#pragma scop
	for (i = intCeil(N, 3); i < intFloor(N, 2); ++i)
		a[intMod(i, 5)] = i;
#pragma endscop
}
