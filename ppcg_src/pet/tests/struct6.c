struct s {
	int a;
};

void foo()
{
	struct s *s;

#pragma scop
	s->a = 5;
#pragma endscop
}
