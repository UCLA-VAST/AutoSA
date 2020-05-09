struct s0 {
	struct {
		int a[10];
	} f[10];
	int b;
};

struct s {
	struct s0 c[10];
};

void bar(struct s0 t[static 5])
{
	for (int i = 0; i < 10; ++i)
		for (int j = 0; j < 10; ++j)
			t[2].f[i].a[j] = i * j;
	t[3].b = 1;
}

void quux(int a[1])
{
	a[0] = 5;
}

void foo()
{
	struct s s[10];

#pragma scop
	bar(s[0].c);
	for (int i = 1; i < 4; ++i)
		bar(&s[1].c[i]);
	quux(&s[2].c[9].b);
#pragma endscop
}
