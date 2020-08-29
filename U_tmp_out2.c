// kernel
// array
{
  for (int c0 = 0; c0 <= 1; c0 += 1) {
    // io_L1
    for (int c4 = c0; c4 <= 1; c4 += 1)
      U_tmp[c4][c0] = prev_V[c4][c0];
    // io_L1
    for (int c4 = c0; c4 <= 1; c4 += 1)
      U[c0][c4] = U_tmp[c4][c0];
    if (c0 == 0)
      for (int c1 = 0; c1 <= 1; c1 += 1)
        if (c1 == 0) {
          // io_L1
          for (int c4 = 0; c4 <= 1; c4 += 1)
            prev_V[c4][0] = A[c4][0];
        }
  }
  // io_L1
  prev_V[1][1] = V[1][1];
}
