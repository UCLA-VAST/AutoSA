void foo()
{
	int b;

	{
		int a;
		a = 5;
		{
			b = 6;
			goto next;
next:
			b = 7;
		}
		b = a;
	}
}
