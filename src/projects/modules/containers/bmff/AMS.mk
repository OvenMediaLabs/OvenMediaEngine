LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_STATIC_LIBRARIES := \
	marker

LOCAL_TARGET := bmff_container

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
    $(call get_sub_source_list,fmp4_packager)

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list,fmp4_packager)

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
