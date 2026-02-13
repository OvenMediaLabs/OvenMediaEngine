LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := rtmp_module

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
    $(call get_sub_source_list,amf0) \
	$(call get_sub_source_list,chunk) \

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list,amf0) \
	$(call get_sub_header_list,opus) \

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)