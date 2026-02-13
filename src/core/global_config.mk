GCC_VERSION := $(shell gcc -v 2>&1 | tail -1 | awk ' { print $3 } ')
GCC_VERSION := $(word 3, $(GCC_VERSION))
GCC_VERSION := $(subst ., ,$(GCC_VERSION))

GCC_VERSION_MAJOR := $(word 1, $(GCC_VERSION))
GCC_VERSION_MINOR := $(word 2, $(GCC_VERSION))
GCC_VERSION_PATCH := $(word 3, $(GCC_VERSION))

# <major> + <00 + minor> + <00 + patch>
GCC_VERSION := $(shell expr $(GCC_VERSION_MAJOR) "*" 1000000 "+" $(GCC_VERSION_MINOR) "*" 1000 "+" $(GCC_VERSION_PATCH))

# gcc 4.9 이상부터는 -fdiagnostics-color 옵션을 지원함
# 4.9 = 4009000
ifeq ($(shell test $(GCC_VERSION) -gt 4009000; echo $$?),0)
    # GCC >= 4.9
    GLOBAL_CC := gcc
    GLOBAL_CXX := g++
    GLOBAL_LD := g++
    GCC_COLOR_OPTION := -fdiagnostics-color
else
    # GCC < 4.9
    GLOBAL_CC := core/colorgcc
    GLOBAL_CXX := core/colorg++
    GLOBAL_LD := core/colorg++
    GCC_COLOR_OPTION :=
endif

__COMMA := ,
ifneq ($(CONFIG_LIBRARY_PATHS),)
__RPATH := -Wl,-rpath,$(subst :,$(__COMMA)-rpath$(__COMMA),$(CONFIG_LIBRARY_PATHS)) -Wl,--disable-new-dtags
endif

ifneq ($(CONFIG_PKG_PATHS),)
__PKG_CONFIG_PATH := PKG_CONFIG_PATH="$(CONFIG_PKG_PATHS)"
endif

__EXTRA_CPPFLAGS :=
__EXTRA_LDFLAGS :=
# Increase stack size if the OS is Alpine Linux)
ifneq ($(shell cat /etc/*release 2>/dev/null | grep "^ID=" | grep "alpine"),)
    # 1048576 = 1MB
    __EXTRA_CPPFLAGS := $(__EXTRA_CPPFLAGS) -Wl,-z,stack-size=1048576
    __EXTRA_LDFLAGS := $(__EXTRA_LDFLAGS) -Wl,-z,stack-size=1048576
endif

# Suppress "warning: attribute ignored" for SRT
GLOBAL_CFLAGS_COMMON := $(GCC_COLOR_OPTION) -Wall -Wformat-security -Wno-attributes $(__EXTRA_CPPFLAGS) $(CPPFLAGS) $(CFLAGS)
GLOBAL_CXXFLAGS_COMMON := $(GCC_COLOR_OPTION) -Wall -Wformat-security -Wno-attributes $(__EXTRA_CPPFLAGS) $(CPPFLAGS) $(CXXFLAGS)
GLOBAL_LDFLAGS_COMMON := $(GCC_COLOR_OPTION) $(__EXTRA_LDFLAGS) $(LDFLAGS)

GLOBAL_CFLAGS_DEBUG := -g -DDEBUG -D_DEBUG $(GLOBAL_CFLAGS_COMMON)
GLOBAL_CXXFLAGS_DEBUG := -g -DDEBUG -D_DEBUG $(GLOBAL_CXXFLAGS_COMMON)
GLOBAL_LDFLAGS_DEBUG := $(__RPATH) $(GLOBAL_LDFLAGS_COMMON)

GLOBAL_CFLAGS_RELEASE := -g -O3 $(GLOBAL_CFLAGS_COMMON)
GLOBAL_CXXFLAGS_RELEASE := -g -O3 $(GLOBAL_CXXFLAGS_COMMON)
GLOBAL_LDFLAGS_RELEASE := $(GLOBAL_LDFLAGS_DEBUG) -O3

# Enable dynamic symbols to use backtrace APIs if the OS isn't macOS
ifneq ($(OS_VERSION), darwin)
    # -Wl,--export-dynamic: backtrace 출력가능
    GLOBAL_LDFLAGS_DEBUG := $(GLOBAL_LDFLAGS_DEBUG) -Wl,--export-dynamic
endif

__GLOBAL_CONFIG_PATH := $(lastword $(MAKEFILE_LIST))
__GLOBAL_CONFIG_PATH := $(dir $(__GLOBAL_CONFIG_PATH))
-include $(__GLOBAL_CONFIG_PATH)local_config.mk
