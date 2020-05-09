int N;

#pragma parameter N 8 16

int main()
{
    unsigned i;
    int j, a[N], b[N];

#pragma scop
    for (i = 0;; ++i) {
	for (j = 0; j < N; ++j)
	    a[j] = 0;
	for (j = 0; j < N; ++j)
	    b[j] = a[j];
    }
#pragma endscop
}
