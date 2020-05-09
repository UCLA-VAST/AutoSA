void foo(int test[100], int index)
{
	int a;

#pragma scop
	if (test[index])
		a = 5;
#pragma endscop
}
