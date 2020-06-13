#!/bin/bash

# - A script to compile and run the host program and bitstream on Intel OpenCL platform

if [ $# != 1 ];
then
  echo "Usage: compile_design.sh [hw|emu|sim]"
  exit
fi  
mode=$1
echo $mode

echo "Compiling the bitstream..."
if [ "$mode" == "hw" ]
then 
  # Compile the bitstream
  # Change the board to your target board if necessary
  aoc src/kernel_kernel.cl -o bin/kernel_kernel.aocx -fp-relaxed -board=s10mx_hbm_es
elif [ "$mode" == "emu" ]
then
  # Compiling for emulator
  aoc -march=emulator src/kernel_kernel.cl -o bin/kernel_kernel.aocx -fp-relaxed -DEMULATE -legacy-emulator
elif [ "$mode" == "sim" ]
then
  # Compiling for simulator
  aoc -march=simulator src/kernel_kernel.cl -o bin/kernel_kernel.aocx -fp-relaxed
else
  echo "Error: Unsupported mode"
  exit
fi

#echo "Compiling the host program..."
## Compile the host program
#make

#echo "Running the program..."
#case "$mode" in
#    "hw")
#      # Run the host program
#      bin/host
#      ;;
#    "emu")
#      # Run the host program with the emulator
#      bin/host -emulator
#      ;;
#    "sim")
#      # Run the host program with the simulator
#      CL_CONTEXT_MPSIM_DEVICE_INTELFPGA=1 bin/host
#      ;;
#esac
