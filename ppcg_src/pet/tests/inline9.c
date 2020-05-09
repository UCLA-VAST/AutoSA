struct s {
	int a[30][40];
	int b[50];
};

void g(struct s *u)
{
	for (int i = 0; i < 10; ++i)
		for (int j = 0; j < 10; ++j)
			u->a[10 + i][20 + j] = i + j;
	u->b[5] = 1;
}

inline void h(struct s t[20])
{
	int a = 0;
	for (int i = 0; i < 20; ++i)
		g(&t[i]);
}

void f()
{
	struct s s[10][20];

#pragma scop
	int a = 1;
	for (int i = 0; i < 10; ++i)
		h(s[i]);
#pragma endscop
}
