float foo(float a, float b)
{
	float c;

#pragma scop
	c = a / b;
#pragma endscop
	return c;
}
