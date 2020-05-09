typedef int	field;
typedef struct s {
	field a;
} a;

void foo()
{
	a s;

#pragma scop
	s.a = 5;
#pragma endscop
}
