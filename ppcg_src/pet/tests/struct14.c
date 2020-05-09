typedef struct scomplex {
	float re;
	float im;
} scomplex;

struct pair {
	scomplex c[2];
};

void foo()
{
	struct pair p;

#pragma scop
	p.c[0].re = p.c[1].im;
#pragma endscop
}
