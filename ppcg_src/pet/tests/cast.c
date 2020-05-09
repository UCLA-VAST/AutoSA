void foo()
{
	int a;
	signed char c = 'a';

#pragma scop
	a = (int) c;
#pragma endscop
}
