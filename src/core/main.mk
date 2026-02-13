#===============================================================================
# Configure variables
#===============================================================================
SHELL := /bin/bash
BUILD_ROOT := .
BUILD_SYSTEM_DIRECTORY := core
OME := OvenMediaEngine
OME_SERVICE := ovenmediaengine.service
INSTALL_DIRECTORY := /usr/share/ovenmediaengine
INSTALL_CONF_DIRECTORY := $(INSTALL_DIRECTORY)/conf
LINK_BIN_DIRECTORY := /usr/bin
INSTALL_SERVICE_DIRECTORY := /lib/systemd/system
LINK_SERVICE_DIRECTORY := /etc/systemd/system

#===============================================================================
# Include makefiles
#===============================================================================
include $(BUILD_SYSTEM_DIRECTORY)/pre_processing.mk
include $(BUILD_SYSTEM_DIRECTORY)/os_version.mk
include $(BUILD_SYSTEM_DIRECTORY)/colors.mk
include $(BUILD_SYSTEM_DIRECTORY)/config.mk
include $(BUILD_SYSTEM_DIRECTORY)/global_config.mk
include $(BUILD_SYSTEM_DIRECTORY)/environments.mk
include $(BUILD_SYSTEM_DIRECTORY)/utilities.mk
include $(BUILD_SYSTEM_DIRECTORY)/progress.mk

#===============================================================================
# OS detection for use utilties
#===============================================================================
ifeq ($(OS_VERSION),)
    $(error Unable to determine OS_VERSION from uname -sm: $(UNAME)!)
endif

#===============================================================================
# Rules
#===============================================================================
BUILD_TARGET_LIST :=
# File list to delete
BUILD_FILES_TO_CLEAN :=

.PHONY: all help release
all: directories_to_prepare build_target_list
release: all
help:
	@echo ""
	@echo " $(ANSI_GREEN)* AMS Help Page$(ANSI_RESET)"
	@echo ""
	@echo "   Usage: make $(ANSI_YELLOW)[COMMANDS]$(ANSI_RESET)"
	@echo ""
	@echo "   Commands:"
	@echo "       $(ANSI_YELLOW)help$(ANSI_RESET): show this page"
	@echo "       $(ANSI_YELLOW)release$(ANSI_RESET): make project to release"
	@echo ""

# clean할 때 target이 삭제될 수 있도록 함
# ifneq ($(MAKECMDGOALS),clean)
include $(BUILD_PROJECTS_DIRECTORY)/AMS.mk
# endif

BUILD_TARGET_LIST := $(strip $(BUILD_TARGET_LIST))
BUILD_BUILT_COUNT := 1
BUILD_TOTAL_PROJECTS_COUNT := $(words $(BUILD_TARGET_LIST))

include $(BUILD_SYSTEM_DIRECTORY)/informations.mk

.PHONY: build_target_list
build_target_list: $(BUILD_TARGET_LIST)
	@$(TARGET_COUNTER)
	@echo $(CURRENT_PROGRESS)"$(CONFIG_COMPLETE_COLOR)Completed.$(ANSI_RESET)"$(INCREASE_COUNT)

.PHONY: directories_to_prepare
directories_to_prepare:
	@$(TARGET_COUNTER)
	@echo $(CURRENT_PROGRESS)"$(CONFIG_CREATE_COLOR)Preparing directories$(ANSI_RESET)..."$(INCREASE_COUNT)
	@for dir in $(BUILD_DIRECTORIES_TO_PREPARE); \
	do \
		if [ ! -d $$dir ]; \
		then \
			echo "    $(CONFIG_CREATE_COLOR)Preparing directory$(ANSI_RESET) $$dir..."; \
			mkdir -p $$dir; \
		fi \
	done

.PHONY: clean clean_internal clean_release
clean_internal:
	@$(TARGET_COUNTER)
	@echo $(CURRENT_PROGRESS)"$(CONFIG_CLEAN_COLOR)Deleting directories$(ANSI_RESET)..."$(INCREASE_COUNT)
	@for dir in $(BUILD_DIRECTORIES_TO_CLEAN); \
	do \
		echo "    $(CONFIG_CLEAN_COLOR)Deleting directory$(ANSI_RESET) $$dir..."; \
		rm -rf $$dir; \
	done
	@for target in $(BUILD_TARGET_LIST); \
	do \
		echo "    $(CONFIG_CLEAN_COLOR)Deleting target$(ANSI_RESET) $$target..."; \
		rm -rf $$target; \
	done
	@for target in $(BUILD_FILES_TO_CLEAN); \
	do \
		echo "    $(CONFIG_CLEAN_COLOR)Deleting file$(ANSI_RESET) $$target..."; \
		rm -rf $$target; \
	done

clean: clean_internal
	@$(TARGET_COUNTER)
	@echo $(CURRENT_PROGRESS)"$(CONFIG_CLEAN_COLOR)Cleaned.$(ANSI_RESET)"$(INCREASE_COUNT)

clean_release: clean_internal
	@$(TARGET_COUNTER)
	@echo $(CURRENT_PROGRESS)"$(CONFIG_CLEAN_COLOR)Cleaned.$(ANSI_RESET)"$(INCREASE_COUNT)

.PHONY: install
install:
	@if test ! -f bin/$(BUILD_METHOD)/$(OME); then \
		echo $(CONFIG_CLEAN_COLOR)Create a release version using the \'make release\' command.$(ANSI_RESET); \
		exit 1; \
	fi

	@echo "$(ANSI_GREEN)Installing directory$(ANSI_RESET) $(INSTALL_DIRECTORY)"
	@mkdir -p $(INSTALL_CONF_DIRECTORY)
	@install -m 755 -s bin/$(BUILD_METHOD)/$(OME) $(INSTALL_DIRECTORY)
	@install -m 755 ../misc/ome_launcher.sh $(INSTALL_DIRECTORY)

	@if test ! -f $(INSTALL_CONF_DIRECTORY)/Server.xml; then \
		echo "$(ANSI_GREEN)Installing example Server config$(ANSI_RESET) $(INSTALL_CONF_DIRECTORY)/Server.xml"; \
		install -m 644 ../misc/conf_examples/Server.xml $(INSTALL_CONF_DIRECTORY); \
	else \
		echo "$(ANSI_YELLOW)Skipping overwriting existing Server config$(ANSI_RESET) \
	$(INSTALL_CONF_DIRECTORY)/Server.xml"; \
	fi

	@if test ! -f $(INSTALL_CONF_DIRECTORY)/Logger.xml; then \
		echo "$(ANSI_GREEN)Installing example Logger config$(ANSI_RESET) $(INSTALL_CONF_DIRECTORY)/Logger.xml"; \
		install -m 644 ../misc/conf_examples/Logger.xml $(INSTALL_CONF_DIRECTORY); \
	else \
		echo "$(ANSI_YELLOW)Skipping overwriting existing Logger config$(ANSI_RESET) \
	$(INSTALL_CONF_DIRECTORY)/Logger.xml"; \
	fi

	@echo "$(ANSI_GREEN)Creating link file$(ANSI_RESET) $(LINK_BIN_DIRECTORY)/$(OME) => \
	$(ANSI_BLUE)$(INSTALL_DIRECTORY)/$(OME)$(ANSI_RESET)"
	@ln -sf $(INSTALL_DIRECTORY)/$(OME) $(LINK_BIN_DIRECTORY)/$(OME)

	@if test ! -f $(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE); then \
		echo "$(ANSI_GREEN)Installing service$(ANSI_RESET) $(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE)"; \
		install -m 644 ../misc/$(OME_SERVICE) $(INSTALL_SERVICE_DIRECTORY); \
	else \
		echo "$(ANSI_YELLOW)Skipping overwriting existing service$(ANSI_RESET) \
	$(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE)"; \
	fi

	@echo "$(ANSI_GREEN)Creating link file$(ANSI_RESET) $(LINK_SERVICE_DIRECTORY)/$(OME_SERVICE) => \
	$(ANSI_BLUE)$(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE)$(ANSI_RESET)"
	@ln -sf $(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE) $(LINK_SERVICE_DIRECTORY)/$(OME_SERVICE)

.PHONY: uninstall
uninstall:
	@echo "$(CONFIG_CLEAN_COLOR)Deleting directory$(ANSI_RESET) $(INSTALL_DIRECTORY)"
	@rm -rf $(INSTALL_DIRECTORY)
	@echo "$(CONFIG_CLEAN_COLOR)Deleting link file$(ANSI_RESET) $(LINK_BIN_DIRECTORY)/$(OME)"
	@rm -rf $(LINK_BIN_DIRECTORY)/$(OME)
	@echo "$(CONFIG_CLEAN_COLOR)Deleting service file$(ANSI_RESET) $(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE)"
	@rm -rf $(INSTALL_SERVICE_DIRECTORY)/$(OME_SERVICE)
	@echo "$(CONFIG_CLEAN_COLOR)Deleting link file$(ANSI_RESET) $(LINK_SERVICE_DIRECTORY)/$(OME_SERVICE)"
	@rm -rf $(LINK_SERVICE_DIRECTORY)/$(OME_SERVICE)