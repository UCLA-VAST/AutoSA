/*
 * Copyright 2014      ARM Ltd.
 *
 * Use of this software is governed by the MIT license
 */

#include <stdio.h>
#include <stdlib.h>

struct ComplexFloat
{
	float Re;
	float Im;
};

/* chemv - complex hermitian matrix-vector multiplication
 * The function body was taken from a VOBLA-generated BLAS library.
 */
void chemv(int n, float alpha_re, float alpha_im,
	int ldAT, struct ComplexFloat AT[restrict const static n][ldAT],
	int incX, struct ComplexFloat X[restrict const static n][incX],
	float beta_re, float beta_im,
	int incY, struct ComplexFloat Y[restrict const static n][incY])
{
#pragma scop
	for (int i0 = 0; i0 <= (n-1); i0 += 1) {
		float var5_Re;
		float var5_Im;
		var5_Re = ((Y[i0][0].Re*beta_re)-(Y[i0][0].Im*beta_im));
		var5_Im = ((Y[i0][0].Im*beta_re)+(Y[i0][0].Re*beta_im));
		Y[i0][0].Re = var5_Re;
		Y[i0][0].Im = var5_Im;
	}
	for (int i1 = 0; i1 <= ((n-1)+1)-1; i1 += 1) {
		float var2_Re;
		float var3_Im;
		float var2_Im;
		float var4_Im;
		float var4_Re;
		float var3_Re;
		var2_Re = (alpha_re*AT[i1][i1].Re);
		var2_Im = (alpha_im*AT[i1][i1].Re);
		var3_Re = ((var2_Re*X[i1][0].Re)-(var2_Im*X[i1][0].Im));
		var3_Im = ((var2_Im*X[i1][0].Re)+(var2_Re*X[i1][0].Im));
		var4_Re = (Y[i1][0].Re+var3_Re);
		var4_Im = (Y[i1][0].Im+var3_Im);
		Y[i1][0].Re = var4_Re;
		Y[i1][0].Im = var4_Im;
	}
	for (int i2 = 0; i2 <= ((n-1)-1); i2 += 1) {
		for (int i3 = 0; i3 <= (n-1)-(1+i2); i3 += 1) {
			float var99_Re;
			float var96_Re;
			float var98_Im;
			float var96_Im;
			float var94_Im;
			float var95_Im;
			float var94_Re;
			float var95_Re;
			float var97_Im;
			float var99_Im;
			float var97_Re;
			float var98_Re;
			var94_Re = ((alpha_re*AT[i2][((1+i2)+i3)].Re)-
				(alpha_im*(-AT[i2][((1+i2)+i3)].Im)));
			var94_Im = ((alpha_im*AT[i2][((1+i2)+i3)].Re)+
				(alpha_re*(-AT[i2][((1+i2)+i3)].Im)));
			var95_Re = ((var94_Re*X[((i3+i2)+1)][0].Re)-
				(var94_Im*X[((i3+i2)+1)][0].Im));
			var95_Im = ((var94_Im*X[((i3+i2)+1)][0].Re)+
				(var94_Re*X[((i3+i2)+1)][0].Im));
			var96_Re = (Y[i2][0].Re+var95_Re);
			var96_Im = (Y[i2][0].Im+var95_Im);
			Y[i2][0].Re = var96_Re;
			Y[i2][0].Im = var96_Im;
			var97_Re = ((alpha_re*AT[i2][((1+i2)+i3)].Re)-
				(alpha_im*AT[i2][((1+i2)+i3)].Im));
			var97_Im = ((alpha_im*AT[i2][((1+i2)+i3)].Re)+
				(alpha_re*AT[i2][((1+i2)+i3)].Im));
			var98_Re = ((var97_Re*X[i2][0].Re)-
				(var97_Im*X[i2][0].Im));
			var98_Im = ((var97_Im*X[i2][0].Re)+
				(var97_Re*X[i2][0].Im));
			var99_Re = (Y[((i3+i2)+1)][0].Re+var98_Re);
			var99_Im = (Y[((i3+i2)+1)][0].Im+var98_Im);
			Y[((i3+i2)+1)][0].Re = var99_Re;
			Y[((i3+i2)+1)][0].Im = var99_Im;
		}
	}
#pragma endscop
}

int main()
{
	const int n = 37;
	const int incX = 1;
	const int incY = 1;
	const int ldAT = n;
	struct ComplexFloat AT[n][ldAT];
	struct ComplexFloat X[n][incX];
	struct ComplexFloat Y[n][incY];

	for (int i = 0; i < n; i++) {
		X[i][0] = (struct ComplexFloat){i + 5, i * 2};
		Y[i][0] = (struct ComplexFloat){i * 3, i + 7};
		for (int j = 0; j < ldAT; j++) {
			AT[i][j] = (struct ComplexFloat){i + j, i + 3};
		}
	}

	chemv(n, 3.14f, 1.59f, ldAT, AT, incX, X, 2.71f, 8.28f, incY, Y);

	for (int i = 0; i < n; i++)
		printf("%0.2f %0.2f\n", Y[i][0].Re, Y[i][0].Im);

	return EXIT_SUCCESS;
}
