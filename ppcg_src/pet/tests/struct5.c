struct s {
	struct f {
		int a[10];
	} f[10];
};

void foo()
{
	struct s s;

#pragma scop
	for (int i = 0; i < 10; ++i)
		for (int j = 0; j < 10; ++j)
			s.f[i].a[j] = i * j;
#pragma endscop
}
