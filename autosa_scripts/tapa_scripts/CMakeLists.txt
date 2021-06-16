cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0076 NEW)

project(kernel)

add_executable(kernel)
target_sources(kernel PRIVATE kernel_host.cpp kernel_kernel.cpp)
target_link_libraries(kernel PUBLIC tapa::tapa)
target_compile_features(kernel PUBLIC cxx_std_11)
include_directories(/opt/tools/xilinx/Vitis_HLS/2020.2/include)

add_test(NAME kernel COMMAND kernel)

find_package(gflags REQUIRED)
find_package(TAPA REQUIRED)
find_package(FRT REQUIRED)
set(TAPA tapa::tapa)

find_package(SDx)
if(SDx_FOUND)
  add_tapa_target(
    kernel-hw-xo
    INPUT kernel_kernel.cpp
    FRT_INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/kernel.frt.cpp
    TOP kernel0
    PLATFORM xilinx_u250_xdma_201830_2)

  add_xocc_hw_link_targets(
    ${CMAKE_CURRENT_BINARY_DIR}
    INPUT kernel-hw-xo
    HW_EMU_XCLBIN
    hw_emu_xclbin
    HW_XCLBIN
    hw_xclbin)

  add_executable(kernel-frt)
  target_include_directories(kernel-frt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_sources(kernel-frt PRIVATE kernel_host.cpp
                                  ${CMAKE_CURRENT_BINARY_DIR}/kernel.frt.cpp)
  target_link_libraries(kernel-frt PRIVATE ${TAPA} frt::frt)

  add_custom_target(
    kernel-cosim
    COMMAND TAPAB=$<TARGET_PROPERTY:${hw_emu_xclbin},FILE_NAME>
            $<TARGET_FILE:kernel-frt>
    DEPENDS kernel-frt ${hw_emu_xclbin}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_target(
    kernel-hw
    COMMAND TAPAB=$<TARGET_PROPERTY:${hw_xclbin},FILE_NAME>
            $<TARGET_FILE:kernel-frt>
    DEPENDS kernel-frt ${hw_xclbin}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  add_test(NAME kernel-cosim COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                                   --target kernel-cosim)
endif()
