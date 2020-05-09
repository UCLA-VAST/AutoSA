void f()
{
	int done = 0;

#pragma scop
While:	while (!done)
		done = 1;
#pragma endscop
}
