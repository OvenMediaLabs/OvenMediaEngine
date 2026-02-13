LOCAL_PATH := $(call get_local_path)

PROJECT_C_INCLUDES := \
	projects \
	projects/third_party

PROJECT_CXX_INCLUDES := \
	$(PROJECT_C_INCLUDES)

# -Wfatal-errors: Stop at the first error encountered during build
PROJECT_CFLAGS := \
	-D__STDC_CONSTANT_MACROS \
	-Wfatal-errors \
	-Wno-unused-function

PROJECT_CXXFLAGS := \
	$(PROJECT_CFLAGS) \
	-DSPDLOG_COMPILED_LIB \
	-Wliteral-suffix \
	-std=c++17

PROJECT_LDFLAGS := \
	-ldl -lz \
	-lstdc++fs				# Compatibility with C++17 and later versions

include $(CLEAR_VARIABLES)
include $(BUILD_SUB_AMS)
