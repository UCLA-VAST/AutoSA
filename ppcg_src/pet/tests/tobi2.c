#include <limits.h>

int main() {
        unsigned int N = UINT_MAX;
        unsigned int i;
	int a;

#pragma scop
        for (i = 0; i < 20 && i < N + 10; ++i)
                a = 5;
#pragma endscop
}

