LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_STATIC_LIBRARIES := \
	application \
	ovlibrary \
	managed_queue \
	transcode_webhook

$(call add_pkg_config,libavformat)
$(call add_pkg_config,libavfilter)
$(call add_pkg_config,libavcodec)
$(call add_pkg_config,libswresample)
$(call add_pkg_config,libswscale)
$(call add_pkg_config,libavutil)
$(call add_pkg_config,vpx)
$(call add_pkg_config,opus)

# Enable Xilinx Media Accelerator
ifeq ($(call chk_pkg_exist,libxma2api),0)
$(call add_pkg_config,libxma2api)
$(call add_pkg_config,libxrm)
endif


LOCAL_TARGET := transcoder

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) $(call get_sub_source_list,codec) $(call get_sub_source_list,codec/decoder) $(call get_sub_source_list,codec/encoder) $(call get_sub_source_list,filter)
LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) $(call get_sub_source_list,codec) $(call get_sub_source_list,codec/decoder) $(call get_sub_source_list,codec/encoder) $(call get_sub_source_list,filter)

include $(BUILD_STATIC_LIBRARY)

