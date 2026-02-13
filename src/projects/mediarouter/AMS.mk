LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
    $(call get_sub_source_list) \

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list) \

LOCAL_TARGET := mediarouter

LOCAL_STATIC_LIBRARIES := \
	application \
	ovlibrary \
	bitstream \
	managed_queue

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
# include $(BUILD_EXECUTABLE)
