solution new -state initial
solution options defaults
solution options set /Input/CppStandard c++11
solution options set /Output/GenerateCycleNetlist false
solution options set /Flows/SCVerify/USE_CCS_BLOCK true
solution file add ../../research/autosa/AutoSA/autosa.tmp/output/src/kernel_kernel_hw.h -type CHEADER
solution file add ../../research/autosa/AutoSA/autosa.tmp/output/src/kernel_kernel.h -type CHEADER
solution file add ../../research/autosa/AutoSA/autosa.tmp/output/src/kernel.h -type CHEADER
solution file add ../../research/autosa/AutoSA/autosa.tmp/output/src/kernel_host.cpp -type C++
directive set -PIPELINE_RAMP_UP true
directive set -PROTOTYPING_ENGINE oasys
directive set -CLUSTER_TYPE combinational
directive set -CLUSTER_FAST_MODE false
directive set -CLUSTER_RTL_SYN false
directive set -CLUSTER_OPT_CONSTANT_INPUTS true
directive set -CLUSTER_ADDTREE_IN_COUNT_THRESHOLD 0
directive set -CLUSTER_ADDTREE_IN_WIDTH_THRESHOLD 0
directive set -ROM_THRESHOLD 64
directive set -PROTOTYPE_ROM true
directive set -CHARACTERIZE_ROM false
directive set -OPT_CONST_MULTS use_library
directive set -CLOCK_OVERHEAD 20.000000
directive set -RESET_CLEARS_ALL_REGS use_library
directive set -START_FLAG {}
directive set -READY_FLAG {}
directive set -DONE_FLAG {}
directive set -TRANSACTION_DONE_SIGNAL true
directive set -STALL_FLAG false
directive set -IDLE_SIGNAL {}
directive set -REGISTER_IDLE_SIGNAL false
directive set -ARRAY_SIZE 1024
directive set -CHAN_IO_PROTOCOL use_library
directive set -IO_MODE super
directive set -UNROLL no
directive set -REALLOC true
directive set -MUXPATH true
directive set -TIMING_CHECKS true
directive set -ASSIGN_OVERHEAD 0
directive set -REGISTER_SHARING_LIMIT 0
directive set -REGISTER_SHARING_MAX_WIDTH_DIFFERENCE 8
directive set -SAFE_FSM false
directive set -NO_X_ASSIGNMENTS true
directive set -REG_MAX_FANOUT 0
directive set -FSM_BINARY_ENCODING_THRESHOLD 64
directive set -FSM_ENCODING none
directive set -LOGIC_OPT false
directive set -MEM_MAP_THRESHOLD 32
directive set -REGISTER_THRESHOLD 256
directive set -MERGEABLE true
directive set -SPECULATE true
directive set -DESIGN_GOAL area
go new
solution library add mgc_Xilinx-VIRTEX-uplus-2LV_beh -- -rtlsyntool Vivado -manufacturer Xilinx -family VIRTEX-uplus -speed -2LV -part xcvu11p-flga2577-2LV-e
solution library add Xilinx_RAMS
solution library add Xilinx_ROMS
solution library add amba
solution library add ccs_fpga_hic
solution library add Xilinx_FIFO
go libraries
directive set -CLOCKS {clk {-CLOCK_PERIOD 5.0 -CLOCK_EDGE rising -CLOCK_UNCERTAINTY 0.0 -CLOCK_HIGH_TIME 2.5 -RESET_SYNC_NAME rst -RESET_ASYNC_NAME arst_n -RESET_KIND sync -RESET_SYNC_ACTIVE high -RESET_ASYNC_ACTIVE low -ENABLE_ACTIVE high}}
go assembly
directive set -FIFO_DEPTH 1
directive set /kernel0/A_IO_L2_in_intra_trans/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/A_IO_L2_in_inter_trans/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/A_IO_L2_in_inter_trans_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/A_IO_L2_in/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/A_IO_L2_in_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/B_IO_L2_in_intra_trans/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/B_IO_L2_in_inter_trans/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/B_IO_L2_in_inter_trans_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/B_IO_L2_in/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/B_IO_L2_in_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/PE/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/PE/idy:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_intra_trans/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_intra_trans/idy:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_inter_trans/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_inter_trans/idy:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_inter_trans_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_inter_trans_boundary/idy:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out/idy:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L1_out_boundary/idy:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L2_out/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/C_drain_IO_L2_out_boundary/idx:rsc -MAP_TO_MODULE {[DirectInput]}
directive set /kernel0/A_IO_L2_in/A_IO_L2_in_local_A_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL
directive set /kernel0/A_IO_L2_in/A_IO_L2_in_local_A_inst:cns -STAGE_REPLICATION 2
directive set /kernel0/A_IO_L2_in/A_IO_L2_in_local_A_inst -WORD_WIDTH 256
directive set /kernel0/A_IO_L2_in_boundary/A_IO_L2_in_local_A_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL
directive set /kernel0/A_IO_L2_in_boundary/A_IO_L2_in_local_A_inst:cns -STAGE_REPLICATION 2
directive set /kernel0/A_IO_L2_in_boundary/A_IO_L2_in_local_A_inst -WORD_WIDTH 256
directive set /kernel0/B_IO_L2_in/B_IO_L2_in_local_B_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL
directive set /kernel0/B_IO_L2_in/B_IO_L2_in_local_B_inst:cns -STAGE_REPLICATION 2
directive set /kernel0/B_IO_L2_in/B_IO_L2_in_local_B_inst -WORD_WIDTH 256
directive set /kernel0/B_IO_L2_in_boundary/B_IO_L2_in_local_B_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL
directive set /kernel0/B_IO_L2_in_boundary/B_IO_L2_in_local_B_inst:cns -STAGE_REPLICATION 2
directive set /kernel0/B_IO_L2_in_boundary/B_IO_L2_in_local_B_inst -WORD_WIDTH 256
directive set /kernel0/C_drain_IO_L1_out/C_drain_IO_L1_out_local_C_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL
directive set /kernel0/C_drain_IO_L1_out/C_drain_IO_L1_out_local_C_inst:cns -STAGE_REPLICATION 1
directive set /kernel0/C_drain_IO_L1_out/C_drain_IO_L1_out_local_C_inst -WORD_WIDTH 64
directive set /kernel0/C_drain_IO_L1_out_boundary/C_drain_IO_L1_out_local_C_inst:cns -MAP_TO_MODULE Xilinx_RAMS.BLOCK_1R1W_RBW_DUAL
directive set /kernel0/C_drain_IO_L1_out_boundary/C_drain_IO_L1_out_local_C_inst:cns -STAGE_REPLICATION 1
directive set /kernel0/C_drain_IO_L1_out_boundary/C_drain_IO_L1_out_local_C_inst -WORD_WIDTH 64
go architect
// Insert directives for dependence if necessary
// Example: directive set /kernel0/PE/run/for:read_mem(local_C:rsc.@) -IGNORE_DEPENDENCY_FROM {for:write_mem(local_C:rsc.@) for:write_mem(local_C:rsc.@)}
directive set /kernel0/PE/run/for#1:for:for:for:for#2:read_mem(local_C:rsc.@) -IGNORE_DEPENDENCY_FROM {for#1:for:for:for:for#2:write_mem(local_C:rsc.@)}
go allocate
go extract
