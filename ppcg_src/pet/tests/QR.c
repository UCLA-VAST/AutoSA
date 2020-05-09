int Zero();
int ReadMatrix();
void Vectorize(int, int, int *, int *, int *);
void Rotate(int, int, int, int *, int *, int *);
void WriteMatrix(int);

int N, K = 256;
#pragma parameter N 8 16
#pragma parameter K 100 1000

int main(void)
{
    int j, i, k;
    int R[N][N], X[K][N], t;

#pragma scop
    for (j = 0; j < N; ++j)
        for (i = j; i < N; ++i)
            R[j][i] = Zero();

    for (k = 0; k < K; ++k)
        for (j = 0; j < N; ++j)
            X[k][j] = ReadMatrix();

    for (k = 0; k < K; ++k)
        for (j = 0; j < N; ++j) {
            Vectorize(R[j][j], X[k][j], &R[j][j], &X[k][j], &t);
            for (i = j+1; i < N; ++i)
               Rotate(R[j][i], X[k][i], t, &R[j][i], &X[k][i], &t);
        }

    for (j = 0; j < N; ++j)
        for (i = j; i < N; ++i)
            WriteMatrix(R[j][i]);
#pragma endscop
}
