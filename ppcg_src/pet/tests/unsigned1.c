void foo()
{
        unsigned char i, j, k;
        int a;

#pragma scop
        for (i = 0; i < 200; ++i)
                for (j = 0; j < 200; ++j)
                        for (k = 0; k < i + j; ++k)
                                a = 5;
#pragma endscop
}
