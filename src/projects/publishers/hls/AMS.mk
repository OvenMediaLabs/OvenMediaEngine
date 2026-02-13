LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_STATIC_LIBRARIES := \
	mpegts_container \
	cpix_client

LOCAL_TARGET := hls_publisher

$(call add_pkg_config,srt)

include $(BUILD_STATIC_LIBRARY)
