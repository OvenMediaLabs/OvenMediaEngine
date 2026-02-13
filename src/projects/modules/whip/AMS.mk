LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := whip

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES)
LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES)

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
