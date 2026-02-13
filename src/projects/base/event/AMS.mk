LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := event

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
    $(call get_sub_source_list,command)

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list,command)

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
