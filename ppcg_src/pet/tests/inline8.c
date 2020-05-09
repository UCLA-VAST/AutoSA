struct s {
	int a[30][40];
	int b[50];
};

void g(struct s *u);

inline void h(struct s t[20])
{
	for (int i = 0; i < 20; ++i)
		g(&t[i]);
}

void f()
{
	struct s s[10][20];

#pragma scop
	for (int i = 0; i < 10; ++i)
		h(s[i]);
#pragma endscop
}
