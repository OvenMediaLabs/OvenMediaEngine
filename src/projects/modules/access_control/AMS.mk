LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := access_controller

LOCAL_STATIC_LIBRARIES := \
	http

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
    $(call get_sub_source_list,admission_webhooks) \
    $(call get_sub_source_list,signed_policy) 

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list,admission_webhooks) \
    $(call get_sub_header_list,signed_policy) 

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
