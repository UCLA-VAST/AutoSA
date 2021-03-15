// In this example, we compile three different operators that are found often in 
// DNNs, including: point-wise conv, depth-wise conv, and FC.

#include "kernel.h"

int main(int argc, char **argv){
#ifdef PC	
  // Point-wise CONV
  data_t pc_cin[PC_R + PC_K - 1][PC_C + PC_K - 1][PC_I];
  data_t pc_w[PC_O][PC_K][PC_K][PC_I];
  data_t pc_cout[PC_R][PC_C][PC_O];
  data_t pc_cout_golden[PC_R][PC_C][PC_O];

  for (int i = 0; i < PC_I; i++)
    for (int r = 0; r < PC_R + PC_K - 1; r++)
      for (int c = 0; c < PC_C + PC_K - 1; c++) {
        pc_cin[r][c][i] = i;
      }

	for (int o = 0; o < PC_O; o++)
		for (int i = 0; i < PC_I; i++)
			for (int p = 0; p < PC_K; p++)
				for (int q = 0; q < PC_K; q++) {
					pc_w[o][p][q][i] = o;
				}

#pragma scop
  for (int o = 0; o < PC_O; o++)
    for (int r = 0; r < PC_R; r++)
      for (int c = 0; c < PC_C; c++) {
        pc_cout[r][c][o] = 0;
        for (int i = 0; i < PC_I; i++)
          for (int p = 0; p < PC_K; p++)
            for (int q = 0; q < PC_K; q++) {
              pc_cout[r][c][o] = pc_cout[r][c][o] + pc_cin[r + p][c + q][i] * pc_w[o][p][q][i];
            }
      }	
#pragma endscop

  for (int o = 0; o < PC_O; o++)
    for (int r = 0; r < PC_R; r++)
      for (int c = 0; c < PC_C; c++) {
        pc_cout_golden[r][c][o] = 0;
        for (int i = 0; i < PC_I; i++)
          for (int p = 0; p < PC_K; p++)
            for (int q = 0; q < PC_K; q++) {
              pc_cout_golden[r][c][o] = pc_cout_golden[r][c][o] + pc_cin[r + p][c + q][i] * pc_w[o][p][q][i];
            }
      }

  int err = 0;
  float thres = 0.001;
  for (int o = 0; o < PC_O; o++)
    for (int r = 0; r < PC_R; r++)
      for (int c = 0; c < PC_C; c++) {
        if (fabs((float)pc_cout_golden[r][c][o] - (float)pc_cout[r][c][o]) > thres) {
          err++;
        }
      }

  if (err) {
    printf("Test failed with %d errors!\n", err);
    return -1;
  } else {
    printf("Test passed!\n");
    return 0;
  }
#endif

#ifdef DC
  // Depth-wise CONV
  data_t dc_cin[DC_R + DC_K - 1][DC_C + DC_K - 1][DC_I];
  data_t dc_w[DC_K][DC_K][DC_I];
  data_t dc_cout[DC_R][DC_C][DC_O];
  data_t dc_cout_golden[DC_R][DC_C][DC_O];

  for (int i = 0; i < DC_I; i++)
    for (int r = 0; r < DC_R + DC_K - 1; r++)
      for (int c = 0; c < DC_C + DC_K - 1; c++) {
        dc_cin[r][c][i] = i;
      }
	
	for (int i = 0; i < DC_I; i++)
		for (int p = 0; p < DC_K; p++)
			for (int q = 0; q < DC_K; q++) {
				dc_w[p][q][i] = i;
			}

#pragma scop
  for (int o = 0; o < DC_O; o++)
    for (int r = 0; r < DC_R; r++)
      for (int c = 0; c < DC_C; c++) {
        dc_cout[r][c][o] = 0;        
        for (int p = 0; p < DC_K; p++)
          for (int q = 0; q < DC_K; q++) {
            dc_cout[r][c][o] = dc_cout[r][c][o] + dc_cin[r + p][c + q][o] * dc_w[p][q][o];
          }
      }	
#pragma endscop

  for (int o = 0; o < DC_O; o++)
    for (int r = 0; r < DC_R; r++)
      for (int c = 0; c < DC_C; c++) {
        dc_cout_golden[r][c][o] = 0;        
        for (int p = 0; p < DC_K; p++)
          for (int q = 0; q < DC_K; q++) {
            dc_cout_golden[r][c][o] = dc_cout_golden[r][c][o] + dc_cin[r + p][c + q][o] * dc_w[p][q][o];
          }
      }	

  int err = 0;
  float thres = 0.001;
  for (int o = 0; o < DC_O; o++)
    for (int r = 0; r < DC_R; r++)
      for (int c = 0; c < DC_C; c++) {
        if (fabs((float)dc_cout_golden[r][c][o] - (float)dc_cout[r][c][o]) > thres) {
          err++;
					printf("(golden, hw)@(%d, %d, %d): (%f, %f)\n", o, r, c, (float)dc_cout_golden[r][c][o], (float)dc_cout[r][c][o]);
        }
      }

  if (err) {
    printf("Test failed with %d errors!\n", err);
    return -1;
  } else {
    printf("Test passed!\n");
    return 0;
  }
#endif

#ifdef FC
  // Fully-connected Layers
  data_t fc_cin[FC_I][FC_J];
  data_t fc_w[FC_J];
  data_t fc_cout[FC_I];
  data_t fc_cout_golden[FC_I];

  for (int i = 0; i < FC_I; i++)
    for (int j = 0; j < FC_J; j++) {
      fc_cin[i][j] = i;
    }
	
	for (int j = 0; j < FC_J; j++) {
		fc_w[j] = j;
	}

#pragma scop
  for (int i = 0; i < FC_I; i++) {
		fc_cout[i] = 0;       
    for (int j = 0; j < FC_J; j++) {
      fc_cout[i] = fc_cout[i] + fc_cin[i][j] * fc_w[j];
    }
  }
#pragma endscop

  for (int i = 0; i < FC_I; i++) {
		fc_cout_golden[i] = 0;       
    for (int j = 0; j < FC_J; j++) {
      fc_cout_golden[i] = fc_cout_golden[i] + fc_cin[i][j] * fc_w[j];
    }
  }	

  int err = 0;
  float thres = 0.001;
  for (int i = 0; i < FC_I; i++)    
    if (fabs((float)fc_cout_golden[i] - (float)fc_cout[i]) > thres) {
      err++;
			printf("(golden, hw)@(%d): (%f, %f)\n", i, (float)fc_cout_golden[i], (float)fc_cout[i]);
    }    

  if (err) {
    printf("Test failed with %d errors!\n", err);
    return -1;
  } else {
    printf("Test passed!\n");
    return 0;
  }
#endif
}