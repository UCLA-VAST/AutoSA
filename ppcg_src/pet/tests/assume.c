void foo(int n, int m, int S, int D[const restrict static S])
{
#pragma scop
        __pencil_assume(m > n);
        for (int i = 0; i < n; i++) {
                D[i] = D[i + m];
        }
#pragma endscop
}
