int f();

void foo()
{
	int i;
	int a;

#pragma scop
	i = f();
	for (i = i; i < 10; ++i)
		a = 5;
#pragma endscop
}
