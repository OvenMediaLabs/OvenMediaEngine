###########################################################################
# REVISION HISTORY
###########################################################################
#
# 0.1: First version
# 0.2: Support ansi color
# 0.3: Support Sub-make system
# 0.4: Support DEBUG/RELEASE build
# 0.41: Make "-g" is a default option while DEBUG build
# 0.42: Change build tool to ColorGCC from native gcc
# 2012-11-28 0.5: Fix dependency bug
# 2012-11-29 0.51: Fix double slash bug while using get_sub_*_list functions (// => /)
# 2012-11-29 0.52: Shows source file name while building
# 2012-11-29 0.53: Fix CFLAG/CXXFLAG bugs
# 2012-11-29 0.54: Supports prebuilt type
# 2017-10-19 0.6: Add PROJECT_*FLAGS variable
# 2017-10-19 0.61: Add dependency to C/C++ object files for AMS.mk
# 2017-10-19 0.62: Add progress indicator
# 2019-07-27 0.63: Add some features to support pkg-config
#
###########################################################################

CONFIG_AMS_VERSION := 0.63

CONFIG_OUTPUT_DIRECTORY := bin
CONFIG_OBJECT_DIRECTORY := intermediates

CONFIG_STATIC_LIBRARY_EXTENSION := .a
CONFIG_SHARED_LIBRARY_EXTENSION := .so
CONFIG_C_EXTENSION := .c
CONFIG_C_HEADER_EXTENSION := .h
CONFIG_CXX_EXTENSION := .cpp
CONFIG_CXX_HEADER_EXTENSION := .h

ifneq ($(DISABLE_CUSTOM_LIBRARY_PATHS),true)
    CONFIG_LIBRARY_PATHS := /opt/ovenmediaengine/lib:/opt/ovenmediaengine/lib64
    CONFIG_PKG_PATHS := /opt/ovenmediaengine/lib/pkgconfig:/opt/ovenmediaengine/lib64/pkgconfig
else
    CONFIG_LIBRARY_PATHS :=
    CONFIG_PKG_PATHS :=
endif

ifeq (${OS_VERSION},darwin)
    CONFIG_CORE_COUNT := $(shell sysctl -n hw.ncpu)
else
    CONFIG_CORE_COUNT := $(shell nproc)
endif

CONFIG_TARGET_COLOR := $(ANSI_GREEN)
CONFIG_TARGET_FILE_COLOR := $(ANSI_BLUE)
CONFIG_SOURCE_FILE_COLOR := $(ANSI_PURPLE)
CONFIG_COMPILING_COLOR := $(ANSI_YELLOW)
CONFIG_BUILDING_COLOR := $(ANSI_GREEN)
CONFIG_CREATE_COLOR := $(ANSI_GREEN)
CONFIG_CLEAN_COLOR := $(ANSI_RED)
CONFIG_COMPLETE_COLOR := $(ANSI_GREEN)
CONFIG_BUILD_TYPE_COLOR := $(ANSI_CYAN)
