int main()
{
	unsigned char k;
	int a;

#pragma scop
	for (k = 252; (k % 9) <= 5; ++k)
		a = 5;
#pragma endscop
}
