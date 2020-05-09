void foo()
{
	int a;

#pragma scop
	a = 5;
	a &= 4;
	a |= 8;
	a ^= 2;
#pragma endscop
}
