int f(int);

void foo(int a[static 1])
{
	int t = a[0];

	if (f(t))
		a[0] = t + 1;
}

void bar()
{
#pragma scop
	int s;
	foo(&s);
#pragma endscop
}
