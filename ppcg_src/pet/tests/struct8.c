struct s {
	struct {
		struct {
			int a[10];
		} f[10];
		int b;
	};
};

void foo()
{
	struct s s;

#pragma scop
	for (int i = 0; i < 10; ++i)
		for (int j = 0; j < 10; ++j)
			s.f[i].a[j] = i * j;
	s.b = 1;
#pragma endscop
}
