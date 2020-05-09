int f();

void foo(int A[100])
{
	int x;
#pragma scop
	for (int i = 0; i < 100; ++i) {
		for (int j = 0; j < 100; ++j) {
			if (A[j]) {
				x += 0;
				continue;
			}
			x = 1;
		}
		for (int j = 0; j < 100; ++j) {
			if (j % 2 == 0) {
				x += 0;
				continue;
			}
			x = 1;
		}
		for (int j = 0; j < 100; ++j) {
			if (A[j])
				x += 0;
			else
				x = 1;
		}
	}
#pragma endscop
}
