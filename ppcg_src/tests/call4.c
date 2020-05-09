#include <stdlib.h>

int inline get(int a[1000], int pos)
{
	int tmp = a[pos];
	return tmp;
}

int main()
{
	int a[1000], b[1000];

	for (int i = 0; i < 1000; ++i)
		a[i] = i;
#pragma scop
	for (int i = 0; i < 999; ++i)
		b[i] = get(a, i) + get(a, i + 1);
#pragma endscop
	for (int i = 0; i < 999; ++i)
		if (b[i] != a[i] + a[i + 1])
			return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
