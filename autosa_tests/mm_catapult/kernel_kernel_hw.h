#include "kernel_kernel.h"

struct A_IO_L2_in_local_A {
  A_t8 data[8][2];
};

struct B_IO_L2_in_local_B {
  B_t8 data[8][2];
};

struct C_drain_IO_L1_out_local_C {
  C_t2 data[8][4];
};

#include <mc_scverify.h>

/* Module Definition */
class A_IO_L3_in {
  public:
    A_IO_L3_in() {}
    #pragma hls_design interface
    #pragma hls_pipeline_init_interval 1
    void CCS_BLOCK(run)(ac_channel<A_t8> &fifo_A_serialize, ac_channel<A_t8> &fifo_A_local_out) {
      /* Variable Declaration */
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
            for (ac_int<2, false> c3 = 0; c3 <= 1; c3 += 1)
              for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1)
#endif
                {
                  // hls_pipeline
                {
                  A_t8 fifo_data;
                  fifo_data = fifo_A_serialize.read();
                  fifo_A_local_out.write(fifo_data);
                }
                }
    }
};
/* Module Definition */

/* Module Definition */
class A_IO_L3_in_serialize {
  public:
    A_IO_L3_in_serialize() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(A_t16 A[1024], ac_channel<A_t8> &fifo_A_local_out) {
      /* Variable Declaration */
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
#endif
      A_t8 fifo_data;
      A_t16 mem_data;
      #pragma hls_pipeline_init_interval 1
      for (ac_int<11, false> i = 0; i < 1024; i++) {
        mem_data = A[i];
        for (ac_int<2, false> p = 0; p < 2; p++) {
          fifo_data = mem_data.slc<256>(0);
          mem_data = mem_data >> 256;
          fifo_A_local_out.write(fifo_data);
        }
      }
    }
};
/* Module Definition */

/* Module Definition */
class A_IO_L2_in_intra_trans {
  public:
    A_IO_L2_in_intra_trans() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<A_IO_L2_in_local_A> &local_A, ac_channel<A_t2> &fifo_A_local_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */


#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
#endif
          {
            A_IO_L2_in_local_A local_A_tmp;
            local_A_tmp = local_A.read();
            // synth
            #pragma hls_pipeline_init_interval 1
            for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
              for (ac_int<4, false> c6 = 0; c6 <= 7; c6 += 1)
                for (ac_int<4, false> c7 = 0; c7 <= 7; c7 += 1) {
                  // hls_pipeline
                  A_t2 fifo_data;
                  A_t8 buf_data;
                  A_t2 buf_data_split[4];
                  buf_data = local_A_tmp.data[c7][2 * c5 / 8];
                  buf_data_split[0] = buf_data.slc<64>(0);
                  buf_data_split[1] = buf_data.slc<64>(64);
                  buf_data_split[2] = buf_data.slc<64>(128);
                  buf_data_split[3] = buf_data.slc<64>(192);
                  int split_i = (c5) % 4;
                  fifo_data = buf_data_split[split_i];
                  fifo_A_local_out.write(fifo_data);
                }
          }
    }
};
/* Module Definition */

/* Module Definition */
class A_IO_L2_in_inter_trans {
  public:
    A_IO_L2_in_inter_trans() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<A_IO_L2_in_local_A> &local_A, ac_channel<A_t8> &fifo_A_in, ac_channel<A_t8> &fifo_A_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
#endif
          {
            A_IO_L2_in_local_A local_A_tmp;
            // synth
            #pragma hls_pipeline_init_interval 1
            for (ac_int<2, false> c3 = p0; c3 <= 1; c3 += 1) {
              if (c3 == p0) {
                for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                  for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1) {
                    // hls_pipeline
                    A_t8 fifo_data;
                    fifo_data = fifo_A_in.read();
                    local_A_tmp.data[c4][c5] = fifo_data;
                  }
              } else {
                for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                  for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1) {
                    // hls_pipeline
                    A_t8 fifo_data;
                    fifo_data = fifo_A_in.read();
                    fifo_A_out.write(fifo_data);
                  }
              }
            }
            local_A.write(local_A_tmp);
          }
    }
};
/* Module Definition */

/* Module Definition */
class A_IO_L2_in_inter_trans_boundary {
  public:
    A_IO_L2_in_inter_trans_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<A_IO_L2_in_local_A> &local_A, ac_channel<A_t8> &fifo_A_in) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
#endif
          {
            A_IO_L2_in_local_A local_A_tmp;
            // synth
            #pragma hls_pipeline_init_interval 1
            for (ac_int<2, false> c3 = p0; c3 <= 1; c3 += 1)
              if (c3 == p0)
                for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                  for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1) {
                    // hls_pipeline
                    A_t8 fifo_data;
                    fifo_data = fifo_A_in.read();
                    local_A_tmp.data[c4][c5] = fifo_data;
                  }
            local_A.write(local_A_tmp);
          }
    }
};
/* Module Definition */

/* Module Definition */
class A_IO_L2_in {
  public:
    A_IO_L2_in() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<A_t8> &fifo_A_in, ac_channel<A_t8> &fifo_A_out, ac_channel<A_t2> &fifo_A_local_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

      A_IO_L2_in_inter_trans_inst.run(
        /* module id */ idx, 
        /* array */ A_IO_L2_in_local_A_inst, 
        /* fifo */ fifo_A_in, 
        /* fifo */ fifo_A_out
      );
      A_IO_L2_in_intra_trans_inst.run(
        /* module id */ idx, 
        /* array */ A_IO_L2_in_local_A_inst, 
        /* fifo */ fifo_A_local_out
      );
    }

  private:
    A_IO_L2_in_inter_trans A_IO_L2_in_inter_trans_inst;
    A_IO_L2_in_intra_trans A_IO_L2_in_intra_trans_inst;
    ac_channel<A_IO_L2_in_local_A> A_IO_L2_in_local_A_inst;
};
/* Module Definition */

/* Module Definition */
class A_IO_L2_in_boundary {
  public:
    A_IO_L2_in_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<A_t8> &fifo_A_in, ac_channel<A_t2> &fifo_A_local_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

      A_IO_L2_in_inter_trans_boundary_inst.run(
        /* module id */ idx, 
        /* array */ A_IO_L2_in_local_A_inst, 
        /* fifo */ fifo_A_in
      );
      A_IO_L2_in_intra_trans_inst.run(
        /* module id */ idx, 
        /* array */ A_IO_L2_in_local_A_inst, 
        /* fifo */ fifo_A_local_out
      );
    }

  private:
    A_IO_L2_in_inter_trans_boundary A_IO_L2_in_inter_trans_boundary_inst;
    A_IO_L2_in_intra_trans A_IO_L2_in_intra_trans_inst;
    ac_channel<A_IO_L2_in_local_A> A_IO_L2_in_local_A_inst;
};
/* Module Definition */

/* Module Definition */
class B_IO_L3_in {
  public:
    B_IO_L3_in() {}
    #pragma hls_design interface
    #pragma hls_pipeline_init_interval 1
    void CCS_BLOCK(run)(ac_channel<B_t8> &fifo_B_serialize, ac_channel<B_t8> &fifo_B_local_out) {
      /* Variable Declaration */
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
            for (ac_int<2, false> c3 = 0; c3 <= 1; c3 += 1)
              for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1)
#endif
                {
                  // hls_pipeline
                {
                  B_t8 fifo_data;
                  fifo_data = fifo_B_serialize.read();
                  fifo_B_local_out.write(fifo_data);
                }
                }
    }
};
/* Module Definition */

/* Module Definition */
class B_IO_L3_in_serialize {
  public:
    B_IO_L3_in_serialize() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(B_t16 B[1024], ac_channel<B_t8> &fifo_B_local_out) {
      /* Variable Declaration */
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
#endif
      B_t8 fifo_data;
      B_t16 mem_data;
      #pragma hls_pipeline_init_interval 1
      for (ac_int<11, false> i = 0; i < 1024; i++) {
        mem_data = B[i];
        for (ac_int<2, false> p = 0; p < 2; p++) {
          fifo_data = mem_data.slc<256>(0);
          mem_data = mem_data >> 256;
          fifo_B_local_out.write(fifo_data);
        }
      }
    }
};
/* Module Definition */

/* Module Definition */
class B_IO_L2_in_intra_trans {
  public:
    B_IO_L2_in_intra_trans() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<B_IO_L2_in_local_B> &local_B, ac_channel<B_t2> &fifo_B_local_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */


#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
#endif
          {
            B_IO_L2_in_local_B local_B_tmp;
            local_B_tmp = local_B.read();
            // synth
            #pragma hls_pipeline_init_interval 1
            for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
              for (ac_int<4, false> c6 = 0; c6 <= 7; c6 += 1)
                for (ac_int<4, false> c7 = 0; c7 <= 7; c7 += 1) {
                  // hls_pipeline
                  B_t2 fifo_data;
                  B_t8 buf_data;
                  B_t2 buf_data_split[4];
                  buf_data = local_B_tmp.data[c6][2 * c5 / 8];
                  buf_data_split[0] = buf_data.slc<64>(0);
                  buf_data_split[1] = buf_data.slc<64>(64);
                  buf_data_split[2] = buf_data.slc<64>(128);
                  buf_data_split[3] = buf_data.slc<64>(192);
                  int split_i = (c5) % 4;
                  fifo_data = buf_data_split[split_i];
                  fifo_B_local_out.write(fifo_data);
                }
          }
    }
};
/* Module Definition */

/* Module Definition */
class B_IO_L2_in_inter_trans {
  public:
    B_IO_L2_in_inter_trans() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<B_IO_L2_in_local_B> &local_B, ac_channel<B_t8> &fifo_B_in, ac_channel<B_t8> &fifo_B_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
#endif
          {
            B_IO_L2_in_local_B local_B_tmp;
            // synth
            #pragma hls_pipeline_init_interval 1
            for (ac_int<2, false> c3 = p0; c3 <= 1; c3 += 1) {
              if (c3 == p0) {
                for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                  for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1) {
                    // hls_pipeline
                    B_t8 fifo_data;
                    fifo_data = fifo_B_in.read();
                    local_B_tmp.data[c4][c5] = fifo_data;
                  }
              } else {
                for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                  for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1) {
                    // hls_pipeline
                    B_t8 fifo_data;
                    fifo_data = fifo_B_in.read();
                    fifo_B_out.write(fifo_data);
                  }
              }
            }
            local_B.write(local_B_tmp);
          }
    }
};
/* Module Definition */

/* Module Definition */
class B_IO_L2_in_inter_trans_boundary {
  public:
    B_IO_L2_in_inter_trans_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<B_IO_L2_in_local_B> &local_B, ac_channel<B_t8> &fifo_B_in) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
#endif
          {
            B_IO_L2_in_local_B local_B_tmp;
            // synth
            #pragma hls_pipeline_init_interval 1
            for (ac_int<2, false> c3 = p0; c3 <= 1; c3 += 1)
              if (c3 == p0)
                for (ac_int<4, false> c4 = 0; c4 <= 7; c4 += 1)
                  for (ac_int<2, false> c5 = 0; c5 <= 1; c5 += 1) {
                    // hls_pipeline
                    B_t8 fifo_data;
                    fifo_data = fifo_B_in.read();
                    local_B_tmp.data[c4][c5] = fifo_data;
                  }
            local_B.write(local_B_tmp);
          }
    }
};
/* Module Definition */

/* Module Definition */
class B_IO_L2_in {
  public:
    B_IO_L2_in() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<B_t8> &fifo_B_in, ac_channel<B_t8> &fifo_B_out, ac_channel<B_t2> &fifo_B_local_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

      B_IO_L2_in_inter_trans_inst.run(
        /* module id */ idx, 
        /* array */ B_IO_L2_in_local_B_inst, 
        /* fifo */ fifo_B_in, 
        /* fifo */ fifo_B_out
      );
      B_IO_L2_in_intra_trans_inst.run(
        /* module id */ idx, 
        /* array */ B_IO_L2_in_local_B_inst, 
        /* fifo */ fifo_B_local_out
      );
    }

  private:
    B_IO_L2_in_inter_trans B_IO_L2_in_inter_trans_inst;
    B_IO_L2_in_intra_trans B_IO_L2_in_intra_trans_inst;
    ac_channel<B_IO_L2_in_local_B> B_IO_L2_in_local_B_inst;
};
/* Module Definition */

/* Module Definition */
class B_IO_L2_in_boundary {
  public:
    B_IO_L2_in_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<B_t8> &fifo_B_in, ac_channel<B_t2> &fifo_B_local_out) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

      B_IO_L2_in_inter_trans_boundary_inst.run(
        /* module id */ idx, 
        /* array */ B_IO_L2_in_local_B_inst, 
        /* fifo */ fifo_B_in
      );
      B_IO_L2_in_intra_trans_inst.run(
        /* module id */ idx, 
        /* array */ B_IO_L2_in_local_B_inst, 
        /* fifo */ fifo_B_local_out
      );
    }

  private:
    B_IO_L2_in_inter_trans_boundary B_IO_L2_in_inter_trans_boundary_inst;
    B_IO_L2_in_intra_trans B_IO_L2_in_intra_trans_inst;
    ac_channel<B_IO_L2_in_local_B> B_IO_L2_in_local_B_inst;
};
/* Module Definition */

/* Module Definition */
class PE {
  public:
    PE() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, int idy, ac_channel<A_t2> &fifo_A_in, ac_channel<A_t2> &fifo_A_out, ac_channel<B_t2> &fifo_B_in, ac_channel<B_t2> &fifo_B_out, ac_channel<C_t1> &fifo_C_drain_out) {
      /* Variable Declaration */
      int p0 = idx, p1 = idy; // module id
      A_t1 local_A[1][2];
      B_t1 local_B[1][2];
      C_t1 local_C[8][8];
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
#endif
        {
          {
            #pragma hls_pipeline_init_interval 1
            for (ac_int<4, false> c6 = 0; c6 <= 7; c6 += 1)
              for (ac_int<4, false> c7 = 0; c7 <= 7; c7 += 1) {
                // hls_unroll
                local_C[c7][c6] = 0;
              }
            #pragma hls_pipeline_init_interval 1
            for (ac_int<3, false> c2 = 0; c2 <= 3; c2 += 1)
              for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                for (ac_int<4, false> c6 = 0; c6 <= 7; c6 += 1)
                  for (ac_int<4, false> c7 = 0; c7 <= 7; c7 += 1) {
                    {
                      A_t2 fifo_data;
                      fifo_data = fifo_A_in.read();
                      #pragma unroll yes
                      for (ac_int<2, false> n = 0; n < 2; n++) {
                        local_A[0][n] = (A_t1)fifo_data.slc<32>(0);
                        fifo_data = fifo_data >> 32;
                      }
                    }
                    {
                      B_t2 fifo_data;
                      fifo_data = fifo_B_in.read();
                      #pragma unroll yes
                      for (ac_int<2, false> n = 0; n < 2; n++) {
                        local_B[0][n] = (B_t1)fifo_data.slc<32>(0);
                        fifo_data = fifo_data >> 32;
                      }
                    }
                    #pragma unroll yes
                    for (ac_int<2, false> c8 = 0; c8 <= 1; c8 += 1)
                      local_C[c7][c6] = (local_C[c7][c6] + (local_A[0][c8] * local_B[0][c8]));
                    if (c2 == 3 && c5 == 7)
                      fifo_C_drain_out.write(local_C[c7][c6]);
                    {
                      B_t2 fifo_data;
                      fifo_data.set_slc(32, local_B[0][1]);
                      fifo_data.set_slc(0, local_B[0][0]);
                      fifo_B_out.write(fifo_data);
                    }
                    {
                      A_t2 fifo_data;
                      fifo_data.set_slc(32, local_A[0][1]);
                      fifo_data.set_slc(0, local_A[0][0]);
                      fifo_A_out.write(fifo_data);
                    }
                  }
          }
        }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L1_out_intra_trans {
  public:
    C_drain_IO_L1_out_intra_trans() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, int idy, ac_channel<C_drain_IO_L1_out_local_C> &local_C, ac_channel<C_t1> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      int p0 = idx, p1 = idy; // module id
      /* Variable Declaration */


#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
#endif
        {
          C_drain_IO_L1_out_local_C local_C_tmp;
          // synth
          #pragma hls_pipeline_init_interval 1
          for (ac_int<4, false> c6 = 0; c6 <= 7; c6 += 1)
            for (ac_int<4, false> c7 = 0; c7 <= 7; c7 += 1) {
              // hls_pipeline
              C_t1 fifo_data;
              C_t2 buf_data;
              C_t1 buf_data_split[2];
              buf_data = local_C_tmp.data[c7][c6 / 2];
              buf_data_split[0] = buf_data.slc<32>(0);
              buf_data_split[1] = buf_data.slc<32>(32);
              int split_i = (c6) % 2;
              fifo_data = fifo_C_drain_local_in.read();
              buf_data_split[split_i] = fifo_data;
                            buf_data.set_slc(0, buf_data_split[0]);
              buf_data.set_slc(32, buf_data_split[1]);

              local_C_tmp.data[c7][c6 / 2] = buf_data;
            }
          local_C.write(local_C_tmp);
        }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L1_out_inter_trans {
  public:
    C_drain_IO_L1_out_inter_trans() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, int idy, ac_channel<C_drain_IO_L1_out_local_C> &local_C, ac_channel<C_t2> &fifo_C_drain_in, ac_channel<C_t2> &fifo_C_drain_out) {
      /* Variable Declaration */
      int p0 = idx, p1 = idy; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
#endif
        {
          C_drain_IO_L1_out_local_C local_C_tmp;
          local_C_tmp = local_C.read();
          // synth
          #pragma hls_pipeline_init_interval 1
          for (ac_int<2, false> c4 = p1; c4 <= 1; c4 += 1) {
            if (c4 == p1) {
              for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1) {
                  // hls_pipeline
                  C_t2 fifo_data;
                  fifo_data = local_C_tmp.data[c5][c6];
                  fifo_C_drain_out.write(fifo_data);
                }
            } else {
              for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1) {
                  // hls_pipeline
                  C_t2 fifo_data;
                  fifo_data = fifo_C_drain_in.read();
                  fifo_C_drain_out.write(fifo_data);
                }
            }
          }
        }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L1_out_inter_trans_boundary {
  public:
    C_drain_IO_L1_out_inter_trans_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, int idy, ac_channel<C_drain_IO_L1_out_local_C> &local_C, ac_channel<C_t2> &fifo_C_drain_out) {
      /* Variable Declaration */
      int p0 = idx, p1 = idy; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
#endif
        {
          C_drain_IO_L1_out_local_C local_C_tmp;
          local_C_tmp = local_C.read();
          // synth
          #pragma hls_pipeline_init_interval 1
          for (ac_int<2, false> c4 = p1; c4 <= 1; c4 += 1)
            if (c4 == p1)
              for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1) {
                  // hls_pipeline
                  C_t2 fifo_data;
                  fifo_data = local_C_tmp.data[c5][c6];
                  fifo_C_drain_out.write(fifo_data);
                }
        }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L1_out {
  public:
    C_drain_IO_L1_out() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, int idy, ac_channel<C_t2> &fifo_C_drain_in, ac_channel<C_t2> &fifo_C_drain_out, ac_channel<C_t1> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      int p0 = idx, p1 = idy; // module id
      /* Variable Declaration */

      C_drain_IO_L1_out_intra_trans_inst.run(
        /* module id */ idx, 
        /* module id */ idy, 
        /* array */ C_drain_IO_L1_out_local_C_inst, 
        /* fifo */ fifo_C_drain_local_in
      );
      C_drain_IO_L1_out_inter_trans_inst.run(
        /* module id */ idx, 
        /* module id */ idy, 
        /* array */ C_drain_IO_L1_out_local_C_inst, 
        /* fifo */ fifo_C_drain_in, 
        /* fifo */ fifo_C_drain_out
      );
    }

  private:
    C_drain_IO_L1_out_inter_trans C_drain_IO_L1_out_inter_trans_inst;
    C_drain_IO_L1_out_intra_trans C_drain_IO_L1_out_intra_trans_inst;
    ac_channel<C_drain_IO_L1_out_local_C> C_drain_IO_L1_out_local_C_inst;
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L1_out_boundary {
  public:
    C_drain_IO_L1_out_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, int idy, ac_channel<C_t2> &fifo_C_drain_out, ac_channel<C_t1> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      int p0 = idx, p1 = idy; // module id
      /* Variable Declaration */

      C_drain_IO_L1_out_intra_trans_inst.run(
        /* module id */ idx, 
        /* module id */ idy, 
        /* array */ C_drain_IO_L1_out_local_C_inst, 
        /* fifo */ fifo_C_drain_local_in
      );
      C_drain_IO_L1_out_inter_trans_boundary_inst.run(
        /* module id */ idx, 
        /* module id */ idy, 
        /* array */ C_drain_IO_L1_out_local_C_inst, 
        /* fifo */ fifo_C_drain_out
      );
    }

  private:
    C_drain_IO_L1_out_inter_trans_boundary C_drain_IO_L1_out_inter_trans_boundary_inst;
    C_drain_IO_L1_out_intra_trans C_drain_IO_L1_out_intra_trans_inst;
    ac_channel<C_drain_IO_L1_out_local_C> C_drain_IO_L1_out_local_C_inst;
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L2_out {
  public:
    C_drain_IO_L2_out() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<C_t2> &fifo_C_drain_in, ac_channel<C_t2> &fifo_C_drain_out, ac_channel<C_t2> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
#endif
        {
          #pragma hls_pipeline_init_interval 1
          for (ac_int<2, false> c3 = p0; c3 <= 1; c3 += 1) {
            if (c3 == p0) {
              for (ac_int<2, false> c4 = 0; c4 <= 1; c4 += 1)
                for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                  for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1) {
                    // hls_pipeline
                    C_t2 fifo_data;
                    fifo_data = fifo_C_drain_local_in.read();
                    fifo_C_drain_out.write(fifo_data);
                  }
            } else {
              for (ac_int<2, false> c4 = 0; c4 <= 1; c4 += 1)
                for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                  for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1) {
                    // hls_pipeline
                    C_t2 fifo_data;
                    fifo_data = fifo_C_drain_in.read();
                    fifo_C_drain_out.write(fifo_data);
                  }
            }
          }
        }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L2_out_boundary {
  public:
    C_drain_IO_L2_out_boundary() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(int idx, ac_channel<C_t2> &fifo_C_drain_out, ac_channel<C_t2> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      int p0 = idx; // module id
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
#endif
        {
          #pragma hls_pipeline_init_interval 1
          for (ac_int<2, false> c3 = p0; c3 <= 1; c3 += 1)
            if (c3 == p0)
              for (ac_int<2, false> c4 = 0; c4 <= 1; c4 += 1)
                for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                  for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1) {
                    // hls_pipeline
                    C_t2 fifo_data;
                    fifo_data = fifo_C_drain_local_in.read();
                    fifo_C_drain_out.write(fifo_data);
                  }
        }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L3_out {
  public:
    C_drain_IO_L3_out() {}
    #pragma hls_design interface
    #pragma hls_pipeline_init_interval 1
    void CCS_BLOCK(run)(ac_channel<C_t2> &fifo_C_drain_serialize, ac_channel<C_t2> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
      for (ac_int<3, false> c0 = 0; c0 <= 3; c0 += 1)
        for (ac_int<3, false> c1 = 0; c1 <= 3; c1 += 1)
          for (ac_int<2, false> c3 = 0; c3 <= 1; c3 += 1)
            for (ac_int<2, false> c4 = 0; c4 <= 1; c4 += 1)
              for (ac_int<4, false> c5 = 0; c5 <= 7; c5 += 1)
                for (ac_int<3, false> c6 = 0; c6 <= 3; c6 += 1)
#endif
                {
                  // hls_pipeline
                {
                  C_t2 fifo_data;
                  fifo_data = fifo_C_drain_local_in.read();
                  fifo_C_drain_serialize.write(fifo_data);
                }
                }
    }
};
/* Module Definition */

/* Module Definition */
class C_drain_IO_L3_out_serialize {
  public:
    C_drain_IO_L3_out_serialize() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(C_t16 C[256], ac_channel<C_t2> &fifo_C_drain_local_in) {
      /* Variable Declaration */
      /* Variable Declaration */

#ifndef __SYNTHESIS__
      // while () // Please add the fifo check for C sim.
#endif
      #pragma hls_pipeline_init_interval 1
      for (ac_int<9, false> i = 0; i < 256; i++) {
        C_t2 fifo_data;
        C_t16 mem_data;
        C_t2 mem_data_split[8];
        for (ac_int<4, false> p = 0; p < 8; p++) {
          fifo_data = fifo_C_drain_local_in.read();
          mem_data_split[p] = fifo_data;
        }
        mem_data.set_slc(0, mem_data_split[0]);
        mem_data.set_slc(64, mem_data_split[1]);
        mem_data.set_slc(128, mem_data_split[2]);
        mem_data.set_slc(192, mem_data_split[3]);
        mem_data.set_slc(256, mem_data_split[4]);
        mem_data.set_slc(320, mem_data_split[5]);
        mem_data.set_slc(384, mem_data_split[6]);
        mem_data.set_slc(448, mem_data_split[7]);
        C[i] = mem_data;
      }
    }
};
/* Module Definition */

#pragma hls_design top
class kernel0 {
  public:
    kernel0() {}
    #pragma hls_design interface
    void CCS_BLOCK(run)(A_t16 A[16384 / 16], B_t16 B[16384 / 16], C_t16 C[4096 / 16])
    {
      /* Module Call */
      A_IO_L3_in_serialize_inst.run(
        /* array */ A,
        /* fifo */ fifo_A_A_IO_L3_in_serialize
      );
      /* Module Call */

      /* Module Call */
      A_IO_L3_in_inst.run(
        /* fifo */ fifo_A_A_IO_L3_in_serialize,
        /* fifo */ fifo_A_A_IO_L2_in_0
      );
      /* Module Call */

      /* Module Call */
      A_IO_L2_in_inst_0.run(
        /* module id */ 0,
        /* fifo */ fifo_A_A_IO_L2_in_0,
        /* fifo */ fifo_A_A_IO_L2_in_1,
        /* fifo */ fifo_A_PE_0_0
      );
      /* Module Call */

      /* Module Call */
      A_IO_L2_in_boundary_inst_1.run(
        /* module id */ 1,
        /* fifo */ fifo_A_A_IO_L2_in_1,
        /* fifo */ fifo_A_PE_1_0
      );
      /* Module Call */

      /* Module Call */
      B_IO_L3_in_serialize_inst.run(
        /* array */ B,
        /* fifo */ fifo_B_B_IO_L3_in_serialize
      );
      /* Module Call */

      /* Module Call */
      B_IO_L3_in_inst.run(
        /* fifo */ fifo_B_B_IO_L3_in_serialize,
        /* fifo */ fifo_B_B_IO_L2_in_0
      );
      /* Module Call */

      /* Module Call */
      B_IO_L2_in_inst_0.run(
        /* module id */ 0,
        /* fifo */ fifo_B_B_IO_L2_in_0,
        /* fifo */ fifo_B_B_IO_L2_in_1,
        /* fifo */ fifo_B_PE_0_0
      );
      /* Module Call */

      /* Module Call */
      B_IO_L2_in_boundary_inst_1.run(
        /* module id */ 1,
        /* fifo */ fifo_B_B_IO_L2_in_1,
        /* fifo */ fifo_B_PE_0_1
      );
      /* Module Call */

      /* Module Call */
      PE_inst_0_0.run(
        /* module id */ 0,
        /* module id */ 0,
        /* fifo */ fifo_A_PE_0_0,
        /* fifo */ fifo_A_PE_0_1,
        /* fifo */ fifo_B_PE_0_0,
        /* fifo */ fifo_B_PE_1_0,
        /* fifo */ fifo_C_drain_PE_0_0
      );
      /* Module Call */

      /* Module Call */
      PE_inst_0_1.run(
        /* module id */ 0,
        /* module id */ 1,
        /* fifo */ fifo_A_PE_0_1,
        /* fifo */ fifo_A_PE_0_2,
        /* fifo */ fifo_B_PE_0_1,
        /* fifo */ fifo_B_PE_1_1,
        /* fifo */ fifo_C_drain_PE_0_1
      );
      /* Module Call */

      /* Module Call */
      PE_inst_1_0.run(
        /* module id */ 1,
        /* module id */ 0,
        /* fifo */ fifo_A_PE_1_0,
        /* fifo */ fifo_A_PE_1_1,
        /* fifo */ fifo_B_PE_1_0,
        /* fifo */ fifo_B_PE_2_0,
        /* fifo */ fifo_C_drain_PE_1_0
      );
      /* Module Call */

      /* Module Call */
      PE_inst_1_1.run(
        /* module id */ 1,
        /* module id */ 1,
        /* fifo */ fifo_A_PE_1_1,
        /* fifo */ fifo_A_PE_1_2,
        /* fifo */ fifo_B_PE_1_1,
        /* fifo */ fifo_B_PE_2_1,
        /* fifo */ fifo_C_drain_PE_1_1
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L1_out_boundary_inst_0_1.run(
        /* module id */ 0,
        /* module id */ 1,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_0_1,
        /* fifo */ fifo_C_drain_PE_1_0
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L1_out_inst_0_0.run(
        /* module id */ 0,
        /* module id */ 0,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_0_1,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_0_0,
        /* fifo */ fifo_C_drain_PE_0_0
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L1_out_boundary_inst_1_1.run(
        /* module id */ 1,
        /* module id */ 1,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_1_1,
        /* fifo */ fifo_C_drain_PE_1_1
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L1_out_inst_1_0.run(
        /* module id */ 1,
        /* module id */ 0,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_1_1,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_1_0,
        /* fifo */ fifo_C_drain_PE_0_1
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L2_out_boundary_inst_1.run(
        /* module id */ 1,
        /* fifo */ fifo_C_drain_C_drain_IO_L2_out_1,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_1_0
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L2_out_inst_0.run(
        /* module id */ 0,
        /* fifo */ fifo_C_drain_C_drain_IO_L2_out_1,
        /* fifo */ fifo_C_drain_C_drain_IO_L2_out_0,
        /* fifo */ fifo_C_drain_C_drain_IO_L1_out_0_0
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L3_out_inst.run(
        /* fifo */ fifo_C_drain_C_drain_IO_L3_out_serialize,
        /* fifo */ fifo_C_drain_C_drain_IO_L2_out_0
      );
      /* Module Call */

      /* Module Call */
      C_drain_IO_L3_out_serialize_inst.run(
        /* array */ C,
        /* fifo */ fifo_C_drain_C_drain_IO_L3_out_serialize
      );
      /* Module Call */

    }

  private:
    /* Module Declaration */
    A_IO_L3_in_serialize A_IO_L3_in_serialize_inst;
    A_IO_L3_in A_IO_L3_in_inst;
    A_IO_L2_in A_IO_L2_in_inst_0;
    A_IO_L2_in_boundary A_IO_L2_in_boundary_inst_1;
    B_IO_L3_in_serialize B_IO_L3_in_serialize_inst;
    B_IO_L3_in B_IO_L3_in_inst;
    B_IO_L2_in B_IO_L2_in_inst_0;
    B_IO_L2_in_boundary B_IO_L2_in_boundary_inst_1;
    PE PE_inst_0_0;
    PE PE_inst_0_1;
    PE PE_inst_1_0;
    PE PE_inst_1_1;
    C_drain_IO_L1_out C_drain_IO_L1_out_inst_0_0;
    C_drain_IO_L1_out_boundary C_drain_IO_L1_out_boundary_inst_0_1;
    C_drain_IO_L1_out C_drain_IO_L1_out_inst_1_0;
    C_drain_IO_L1_out_boundary C_drain_IO_L1_out_boundary_inst_1_1;
    C_drain_IO_L2_out C_drain_IO_L2_out_inst_0;
    C_drain_IO_L2_out_boundary C_drain_IO_L2_out_boundary_inst_1;
    C_drain_IO_L3_out C_drain_IO_L3_out_inst;
    C_drain_IO_L3_out_serialize C_drain_IO_L3_out_serialize_inst;
    /* Module Declaration */

    /* FIFO Declaration */
    /* A_IO_L3_in_serialize fifo */ ac_channel<A_t8> fifo_A_A_IO_L3_in_serialize;
    /* B_IO_L3_in_serialize fifo */ ac_channel<B_t8> fifo_B_B_IO_L3_in_serialize;
    /* C_drain_IO_L3_out_serialize fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L3_out_serialize;
    /* A_IO_L2_in fifo */ ac_channel<A_t8> fifo_A_A_IO_L2_in_0;
    /* A_IO_L2_in fifo */ ac_channel<A_t8> fifo_A_A_IO_L2_in_1;
    /* A_IO_L2_in fifo */ ac_channel<A_t8> fifo_A_A_IO_L2_in_2;
    /* B_IO_L2_in fifo */ ac_channel<B_t8> fifo_B_B_IO_L2_in_0;
    /* B_IO_L2_in fifo */ ac_channel<B_t8> fifo_B_B_IO_L2_in_1;
    /* B_IO_L2_in fifo */ ac_channel<B_t8> fifo_B_B_IO_L2_in_2;
    /* PE fifo */ ac_channel<A_t2> fifo_A_PE_0_0;
    /* PE fifo */ ac_channel<A_t2> fifo_A_PE_0_1;
    /* PE fifo */ ac_channel<A_t2> fifo_A_PE_0_2;
    /* PE fifo */ ac_channel<A_t2> fifo_A_PE_1_0;
    /* PE fifo */ ac_channel<A_t2> fifo_A_PE_1_1;
    /* PE fifo */ ac_channel<A_t2> fifo_A_PE_1_2;
    /* PE fifo */ ac_channel<B_t2> fifo_B_PE_0_0;
    /* PE fifo */ ac_channel<B_t2> fifo_B_PE_1_0;
    /* PE fifo */ ac_channel<B_t2> fifo_B_PE_2_0;
    /* PE fifo */ ac_channel<B_t2> fifo_B_PE_0_1;
    /* PE fifo */ ac_channel<B_t2> fifo_B_PE_1_1;
    /* PE fifo */ ac_channel<B_t2> fifo_B_PE_2_1;
    /* PE fifo */ ac_channel<C_t1> fifo_C_drain_PE_0_0;
    /* PE fifo */ ac_channel<C_t1> fifo_C_drain_PE_1_0;
    /* PE fifo */ ac_channel<C_t1> fifo_C_drain_PE_0_1;
    /* PE fifo */ ac_channel<C_t1> fifo_C_drain_PE_1_1;
    /* C_drain_IO_L1_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L1_out_0_0;
    /* C_drain_IO_L1_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L1_out_0_1;
    /* C_drain_IO_L1_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L1_out_0_2;
    /* C_drain_IO_L1_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L1_out_1_0;
    /* C_drain_IO_L1_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L1_out_1_1;
    /* C_drain_IO_L1_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L1_out_1_2;
    /* C_drain_IO_L2_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L2_out_0;
    /* C_drain_IO_L2_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L2_out_1;
    /* C_drain_IO_L2_out fifo */ ac_channel<C_t2> fifo_C_drain_C_drain_IO_L2_out_2;
    /* FIFO Declaration */
};
