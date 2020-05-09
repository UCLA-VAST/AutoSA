void foo(int *a)
{
#pragma scop
	int b = 5;
	a[0] = b;
	__pencil_kill(b);
#pragma endscop
}
