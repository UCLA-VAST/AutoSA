inline int add_one(int i)
{
	return i + 1;
}

void foo()
{
	int a;

#pragma scop
	a = add_one(add_one(5));
#pragma endscop
}
