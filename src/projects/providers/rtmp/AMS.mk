LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_STATIC_LIBRARIES := \
	application \
	ovlibrary \
	flv_container \
	flv_container_v2 \
	provider \
	rtmp_module \
	rtmp_v2_module
	
LOCAL_PREBUILT_LIBRARIES := \

LOCAL_LDFLAGS := \
	-lpthread \
	-ldl \
	-lz
 
LOCAL_TARGET := rtmp_provider

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
	$(call get_sub_source_list,tracks) \
	$(call get_sub_source_list,handlers)

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
	$(call get_sub_header_list,tracks) \
	$(call get_sub_header_list,handlers)

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
# include $(BUILD_EXECUTABLE)
