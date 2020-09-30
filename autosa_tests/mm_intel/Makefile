APP ?= kernel
AOCL_BOARD ?= s10mx_hbm_es
SW_EMU_AOCX ?= $(APP)_sw_emu.aocx
HW_EMU_AOCX ?= $(APP)_hw_emu.aocx
HW_AOCX ?= $(APP)_hw.aocx
AOCO ?= $(APP).aoco
AOCR ?= $(APP).aocr

# Compiler
AOC ?= aoc
CXX ?= g++
AOC_FLAGS ?= -board=$(AOCL_BOARD) -fp-relaxed -report -hyper-optimized-handshaking=off -I $(INTELFPGAOCLSDKROOT)/include/kernel_headers

TARGET ?= host
SW_EMU_TARGET ?= host_sw_emu
TARGET_DIR ?= bin
AOCL_UTILS ?= $(INTELFPGAOCLSDKROOT)/examples_aoc/common

# Directories
INC_DIRS := src $(AOCL_UTILS)/inc
LIB_DIRS := 

# Files
INCS := $(wildcard src/*.h)
HOST_SRCS := $(wildcard src/$(APP)_host.cpp $(AOCL_UTILS)/src/AOCLUtils/*.cpp)
KERNEL_SRCS := src/$(APP)_kernel.cl

ifeq ($(VERBOSE),1)
ECHO := 
else
ECHO := @
endif

# Where is the Intel(R) FPGA SDK for OpenCL(TM) software?
ifeq ($(wildcard $(INTELFPGAOCLSDKROOT)),)
$(error Set INTELFPGAOCLSDKROOT to the root directory of the Intel(R) FPGA SDK for OpenCL(TM) software installation)
endif
ifeq ($(wildcard $(INTELFPGAOCLSDKROOT)/host/include/CL/opencl.h),)
$(error Set INTELFPGAOCLSDKROOT to the root directory of the Intel(R) FPGA SDK for OpenCL(TM) software installation.)
endif

# OpenCL compile and link flags.
AOCL_COMPILE_CONFIG := $(shell aocl compile-config )
AOCL_LINK_LIBS := $(shell aocl ldlibs )
AOCL_LINK_FLAGS := $(shell aocl ldflags )
# Linking with defences enabled
AOCL_LINK_FLAGS += -z noexecstack
AOCL_LINK_FLAGS += -Wl,-z,relro,-z,now
AOCL_LINK_FLAGS += -Wl,-Bsymbolic
AOCL_LINK_FLAGS += -pie
AOCL_LINK_CONFIG := $(AOCL_LINK_FLAGS) $(AOCL_LINK_LIBS)

# Compilation flags
ifeq ($(DEBUG),1)
CXXFLAGS += -g
else
CXXFLAGS += -O2
endif
CXXFLAGS += -std=gnu++0x

# Compiling with defences enabled
CXXFLAGS += -fstack-protector
CXXFLAGS += -D_FORTIFY_SOURCE=2
CXXFLAGS += -Wformat -Wformat-security
CXXFLAGS += -fPIE

# We must force GCC to never assume that it can shove in its own
# sse2/sse3 versions of strlen and strcmp because they will CRASH.
# Very hard to debug!
CXXFLAGS += -fPIC

LIBS := rt pthread

## Make it all!
#all : $(TARGET_DIR)/$(TARGET)

sw_emu : $(TARGET_DIR)/$(TARGET) $(TARGET_DIR)/$(SW_EMU_AOCX)

hls: $(TARGET_DIR)/$(AOCR)

hw : $(TARGET_DIR)/$(TARGET) $(TARGET_DIR)/$(HW_AOCX)

hw_emu: $(TARGET_DIR)/$(TARGET) $(TARGET_DIR)/$(HW_EMU_AOCX)

hw_emu_check: $(TARGET_DIR)/$(TARGET) $(TARGET_DIR)/$(HW_EMU_AOCX)
	CL_CONTEXT_MPSIM_DEVICE_INTELFPGA=1 $(TARGET_DIR)/$(TARGET) $(HW_EMU_AOCX)

sw_emu_check : $(TARGET_DIR)/$(SW_EMU_TARGET) $(TARGET_DIR)/$(SW_EMU_AOCX)
	CL_CONTEXT_EMULATOR_DEVICE_INTELFPGA=1 $(TARGET_DIR)/$(TARGET) $(SW_EMU_AOCX)

hw_check : $(TARGET_DIR)/$(TARGET) $(TARGET_DIR)/$(HW_AOCX)
	$(TARGET_DIR)/$(TARGET) $(HW_AOCX)

# Host executable target.
$(TARGET_DIR)/$(TARGET) : Makefile $(HOST_SRCS) $(INCS) $(TARGET_DIR)
	$(ECHO)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(EXTRACXXFLAGS) -fPIC $(foreach D,$(INC_DIRS),-I$D) \
			$(AOCL_COMPILE_CONFIG) $(HOST_SRCS) $(AOCL_LINK_CONFIG) \
			$(foreach D,$(LIB_DIRS),-L$D) \
			$(foreach L,$(LIBS),-l$L) \
			-o $(TARGET_DIR)/$(TARGET)

$(TARGET_DIR)/$(SW_EMU_TARGET) : Makefile $(HOST_SRCS) $(INCS) $(TARGET_DIR)
	$(ECHO)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(EXTRACXXFLAGS) -fPIC $(foreach D,$(INC_DIRS),-I$D) \
			$(AOCL_COMPILE_CONFIG) $(HOST_SRCS) $(AOCL_LINK_CONFIG) \
			$(foreach D,$(LIB_DIRS),-L$D) \
			$(foreach L,$(LIBS),-l$L) \
			-o $(TARGET_DIR)/$(TARGET) -DEMULATE

$(TARGET_DIR) :
	$(ECHO)mkdir $(TARGET_DIR)

$(TARGET_DIR)/$(SW_EMU_AOCX) : $(KERNEL_SRCS)
	$(AOC) $(AOC_FLAGS) -march=emulator -legacy-emulator -o $@ $^

$(TARGET_DIR)/$(HW_EMU_AOCX) : $(KERNEL_SRCS)
	$(AOC) $(AOC_FLAGS) -march=simulator -ghdl -o $@ $^

$(TARGET_DIR)/$(HW_AOCX) : $(KERNEL_SRCS)
	$(AOC) $(AOC_FLAGS) -o $@ $^

$(TARGET_DIR)/$(AOCO) : $(KERNEL_SRCS)
	$(AOC) $(AOC_FLAGS) -c -o $@ $^

$(TARGET_DIR)/$(AOCR) : $(TARGET_DIR)/$(AOCO)
	$(AOC) $(AOC_FLAGS) -rtl -o $@ $^

# Standard make targets
clean :
	$(ECHO)rm -rf $(TARGET_DIR)/*

.PHONY : all clean
