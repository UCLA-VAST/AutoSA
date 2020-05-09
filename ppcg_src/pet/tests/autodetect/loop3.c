void foo()
{
	int a;

	for (;;) {
		a = 5;
loop:		goto loop;
	}
}
