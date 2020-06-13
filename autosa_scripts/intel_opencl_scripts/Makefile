APP ?= kernel
AOCL_BOARD ?= s10mx_hbm_es
SW_EMU_AOCX ?= $(APP)_kernel.aocx
HW_AOCX ?= $(APP)_kernel.aocx

# Compiler
AOC ?= aoc
CXX ?= g++
AOC_FLAGS ?= -board=s10mx_hbm_es -fp-relaxed -report

TARGET ?= host
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
	CL_CONTEXT_EMULATOR_DEVICE_INTELFPGA=1 $(TARGET_DIR)/$(TARGET)

# Host executable target.
$(TARGET_DIR)/$(TARGET) : Makefile $(HOST_SRCS) $(INCS) $(TARGET_DIR)
	$(ECHO)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(EXTRACXXFLAGS) -fPIC $(foreach D,$(INC_DIRS),-I$D) \
			$(AOCL_COMPILE_CONFIG) $(HOST_SRCS) $(AOCL_LINK_CONFIG) \
			$(foreach D,$(LIB_DIRS),-L$D) \
			$(foreach L,$(LIBS),-l$L) \
			-o $(TARGET_DIR)/$(TARGET)

$(TARGET_DIR) :
	$(ECHO)mkdir $(TARGET_DIR)

$(TARGET_DIR)/$(SW_EMU_AOCX) : $(KERNEL_SRCS)
	$(AOC) $(AOC_FLAGS) -march=emulator -legacy-emulator -o $@ $^

# Standard make targets
clean :
	$(ECHO)rm -rf $(TARGET_DIR)/*

.PHONY : all clean
