void foo()
{
	int a;

	for (;;) {
loop:		goto loop;
		a = 5;
	}
	a = 2;
}
