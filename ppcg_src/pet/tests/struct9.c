void f1(int a);
void f2(int a[40]);
void f3(int a[30][40]);

struct s {
	int a[30][40];
};

void foo()
{
	struct s s[10][20];

#pragma scop
	for (int i = 0; i < 10; ++i)
		for (int j = 0; j < 20; ++j)
			for (int k = 0; k < 30; ++k)
				for (int l = 0; l < 40; ++l) {
					f1(s[i][j].a[k][l]);
					f2(s[i][j].a[k]);
					f3(s[i][j].a);
				}
#pragma endscop
}
