LOCAL_PATH := $(call get_local_path)

# Call AMS-*.mk in the current directory
AMS_FILE_LIST := $(call get_local_file_list,AMS-*.mk)
$(foreach file,$(AMS_FILE_LIST),$(eval include $(LOCAL_PATH)/$(file)))

