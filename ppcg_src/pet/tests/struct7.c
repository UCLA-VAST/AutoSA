struct scomplex {
	float re;
	float im;
};

struct pair {
	struct scomplex a;
	struct scomplex b;
};

void foo()
{
	struct pair p;

#pragma scop
	p.a.re = p.b.im;
#pragma endscop
}
