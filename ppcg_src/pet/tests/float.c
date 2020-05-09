void foo()
{
	float a;

#pragma scop
	a = 0.333f;
	a = 0.;
#pragma endscop
}
