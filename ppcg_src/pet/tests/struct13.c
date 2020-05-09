typedef int	field;
typedef struct {
	field a;
} a;

void foo(int n, a s[const restrict static n])
{
#pragma scop
	if (n > 0)
		s[0].a = 5;
#pragma endscop
}
