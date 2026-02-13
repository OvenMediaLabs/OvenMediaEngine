LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_STATIC_LIBRARIES := \
	bmff_container \
	cpix_client \
	marker \
	webvtt_packager

LOCAL_TARGET := llhls_publisher

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
