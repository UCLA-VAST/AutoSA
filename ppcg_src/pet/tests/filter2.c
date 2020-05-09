void foo(int test[100], int index)
{
#pragma scop
	if (test[index])
		index = 5;
#pragma endscop
}
