#include <stdlib.h>

int inline add_one(int i)
{
	return i + 1;
}

int main()
{
	int a[1000], b[1000];

	for (int i = 0; i < 1000; ++i)
		a[i] = i;
#pragma scop
	for (int i = 0; i < 999; ++i)
		b[i] = add_one(add_one(a[i]));
#pragma endscop
	for (int i = 0; i < 999; ++i)
		if (b[i] != a[i] + 2)
			return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
