int N;

#pragma parameter N 8 16

int main()
{
    int i, j, a[N], b[N];

#pragma scop
    while (1) {
	for (j = 0; j < N; ++j)
	    a[j] = 0;
	for (j = 0; j < N; ++j)
	    b[j] = a[j];
    }
#pragma endscop
}
