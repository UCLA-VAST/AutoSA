#include <stdlib.h>

struct s {
	int a;
	int b;
};

int main()
{
	int a[10];

	for (int i = 0; i < 10; ++i)
		a[i] = 0;
#pragma scop
	for (int i = 0; i < 10; ++i) {
		struct s b[1];
		b[0].a = 1;
		b[0].b = i;
		a[i] = b[0].a + b[0].b;
	}
#pragma endscop
	for (int i = 0; i < 10; ++i)
		if (a[i] != 1 + i)
			return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
