int f();

void foo()
{
	int t;
#pragma scop
	if (f())
		t = 0;
	else
		t = 1;
#pragma endscop
}
