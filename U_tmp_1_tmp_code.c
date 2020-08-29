// kernel
// array
for (int c0 = 0; c0 <= 1; c0 += 1) {
  if (c0 == 1) {
    // io_L1
    // pe
    // hls_pipeline
    prev_V[1][1] = V[1][1];
  }
  for (int c1 = c0; c1 <= 1; c1 += 1) {
    // io_L1
    // pe
    for (int c2 = c0; c2 <= 1; c2 += 1) {
      // hls_pipeline
      {
        if (c0 == 0)
          prev_V[c2][c1] = A[c2][c1];
        if (c1 == c0) {
          U_tmp[c2][c0] = prev_V[c2][c0];
          U[c0][c2] = U_tmp[c2][c0];
        } else {
          U_tmp[c2][1] = U_tmp[c2][0];
          if (c2 == 0) {
            L_tmp[0][1] = (prev_V[0][1] / U_tmp[0][1]);
            L[0][1] = L_tmp[0][1];
          }
        }
      }
    }
  }
  if (c0 == 0) {
    // io_L1
    // pe
    // hls_pipeline
    L_tmp[1][1] = L_tmp[0][1];
    // io_L1
    // pe
    // hls_pipeline
    V[1][1] = (prev_V[1][1] - (L_tmp[1][1] * U_tmp[1][1]));
  }
}
