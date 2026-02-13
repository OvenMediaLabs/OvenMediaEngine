LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := scte35_event

LOCAL_STATIC_LIBRARIES := \
	mpegts_container

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
    $(call get_sub_source_list_r,frames)

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list_r,frames)

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)