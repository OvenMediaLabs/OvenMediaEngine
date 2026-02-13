LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := cpix_client

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)