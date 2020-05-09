int f(void);

void foo(int test[100], int index)
{
#pragma scop
	index = f();
	if (test[index])
		index = 5;
#pragma endscop
}
