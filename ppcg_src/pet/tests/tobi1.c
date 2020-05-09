void foo()
{
        unsigned char i;
	int a;

#pragma scop
	for (i = 0; i != 16 ; i += 65)
                a = 5;
#pragma endscop
}

