LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_TARGET := ovlibrary

LOCAL_HEADER_FILES := $(LOCAL_HEADER_FILES) \
    $(call get_sub_header_list,logger) \
	$(call get_sub_header_list,logger/formatters) \
	$(call get_sub_header_list,logger/pattern_flags)

LOCAL_SOURCE_FILES := $(LOCAL_SOURCE_FILES) \
	$(call get_sub_source_list,logger) \
	$(call get_sub_source_list,logger/formatters) \
	$(call get_sub_source_list,logger/pattern_flags)

$(call add_pkg_config,libpcre2-8)
$(call add_pkg_config,spdlog)

include $(BUILD_STATIC_LIBRARY)
