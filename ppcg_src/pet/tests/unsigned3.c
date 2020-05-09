int main()
{
	unsigned char k;
	int a;

#pragma scop
	for (k = 252; 1; ++k)
		a = k;
#pragma endscop
}
