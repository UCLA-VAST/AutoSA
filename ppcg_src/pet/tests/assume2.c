void foo(int n, int m)
{
#pragma scop
	n = 5;
        __pencil_assume(m > n);
#pragma endscop
}
