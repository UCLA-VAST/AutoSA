void foo()
{
	int a;

	for (int i = 0; i < 10; ++i) {
loop:		goto loop;
		a = 5;
	}
}
