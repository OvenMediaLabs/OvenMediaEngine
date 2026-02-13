ALIGNED_VERSION := $(shell printf "%-16s" $(CONFIG_AMS_VERSION))

$(info $(ANSI_CYAN)####################################################$(ANSI_RESET))
$(info $(ANSI_CYAN)#                                                  #$(ANSI_RESET))
$(info $(ANSI_CYAN)#           AirenSoft Make System v$(ALIGNED_VERSION)#$(ANSI_RESET))
$(info $(ANSI_CYAN)#                                                  #$(ANSI_RESET))
$(info $(ANSI_CYAN)####################################################$(ANSI_RESET))

ifneq ($(MAKECMDGOALS),help)
$(info $()   - $(ANSI_YELLOW)Operating system$(ANSI_RESET)        : $(OS_VERSION))
$(info $()   - $(ANSI_YELLOW)Core count$(ANSI_RESET)              : $(CONFIG_CORE_COUNT))
$(info $()   - $(ANSI_YELLOW)GCC version$(ANSI_RESET)             : $(GCC_VERSION_MAJOR).$(GCC_VERSION_MINOR).$(GCC_VERSION_PATCH))
$(info $()   - $(ANSI_YELLOW)Build option$(ANSI_RESET)            : $(BUILD_METHOD))
$(info $()   - $(ANSI_YELLOW)Paths for linker$(ANSI_RESET)        : $(CONFIG_LIBRARY_PATHS))
$(info $()   - $(ANSI_YELLOW)Paths for pkg-config$(ANSI_RESET)    : $(CONFIG_PKG_PATHS))
ifneq ($(MAKECMDGOALS),clean)
$(info $()   - $(ANSI_YELLOW)Total Projects count$(ANSI_RESET)    : $(words $(BUILD_TARGET_LIST)))
endif
ifeq ($(HWACCELS_NVIDIA_ENABLED),true)
$(info $()   - $(ANSI_BLUE)NVIDIA GPU HWAccels$(ANSI_RESET)     : Support)
endif
ifeq ($(HWACCELS_XMA_ENABLED),true)
$(info $()   - $(ANSI_BLUE)Xilinx Alveo HWAccels$(ANSI_RESET)   : Support)
endif
ifeq ($(HWACCELS_NILOGAN_ENABLED),true)
$(info $()   - $(ANSI_BLUE)Netint Logan HWAccels$(ANSI_RESET)   : Support)
endif
ifeq ($(THIRDP_LIBX264_ENABLED),true)
$(info $()   - $(ANSI_BLUE)Libx264 Software Codec$(ANSI_RESET)  : Support)
endif
$(info $())
endif
