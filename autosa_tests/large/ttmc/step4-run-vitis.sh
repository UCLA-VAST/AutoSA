OUTPUT_DIR="$(pwd)/vitis_run"

# name of the top function
TOP=kernel0

# choose the target device
PLATFORM=xilinx_u250_xdma_201830_2 
#PLATFORM=xilinx_u280_xdma_201920_3 

XO="$(pwd)/kernel0.xo"

# For different approaches see UG904-vivado-implementation
STRATEGY="Default" 
#STRATEGY="EarlyBlockPlacement" 

# remove the unused '--connectivity.sp' option for v++ if some DDRs are not used 
# Example: if we map p1 to DDR 3 and p2 to DDR 0
#
# void kernel0(ap_uint<512> *p1, ap_uint<512> *p2)
# {
#   #pragma HLS INTERFACE m_axi port=p1 offset=slave bundle=gmem_A
#   #pragma HLS INTERFACE m_axi port=p2 offset=slave bundle=gmem_B
# 
#   load_p1 (p1, ...);
#   load_p2 (p2, ...);
# }
#
# ARG_FOR_DDR_0=p2
# ARG_FOR_DDR_3=p1
# Should remove '--connectivity.sp' for DDR1 and DDR2

ARG_FOR_DDR_1=A
ARG_FOR_DDR_2=B
ARG_FOR_DDR_3=C
ARG_FOR_DDR_4=D

# the constraint file containing the floorplan results
# WARNING: must use absolute address
CONSTRAINT="$(pwd)/constraint.tcl"
if [ ! -f "$CONSTRAINT" ]; then
    echo "no constraint file found"
    exit
fi

v++ \
  --link \
  --output "${OUTPUT_DIR}/${TOP}_${PLATFORM}.xclbin" \
  --kernel ${TOP} \
  --platform ${PLATFORM} \
  --target hw \
  --report_level 2 \
  --temp_dir "${OUTPUT_DIR}/${TOP}_${PLATFORM}.temp" \
  --optimize 3 \
  --connectivity.nk ${TOP}:1:${TOP}_1 \
  --max_memory_ports ${TOP} \
  --save-temps \
  ${XO} \
  --connectivity.sp ${TOP}_1.${ARG_FOR_DDR_1}:DDR[0] \
  --connectivity.sp ${TOP}_1.${ARG_FOR_DDR_2}:DDR[1] \
  --connectivity.sp ${TOP}_1.${ARG_FOR_DDR_3}:DDR[2] \
  --connectivity.sp ${TOP}_1.${ARG_FOR_DDR_4}:DDR[3] \
  --kernel_frequency 300 \
  --vivado.prop run.impl_1.STEPS.PLACE_DESIGN.ARGS.DIRECTIVE=$STRATEGY \
  --vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE=$CONSTRAINT
