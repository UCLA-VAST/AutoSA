struct s {
	int a;
};

void foo()
{
	struct s s[10][20];

#pragma scop
	for (int i = 0; i < 10; ++i)
		for (int j = 0; j < 20; ++j)
			s[i][j].a = 5;
#pragma endscop
}
