LOCAL_PATH := $(call get_local_path)
include $(DEFAULT_VARIABLES)

LOCAL_STATIC_LIBRARIES := \
	webrtc_publisher \
	llhls_publisher \
	hls_publisher \
	ovt_publisher \
	file_publisher \
	push_publisher \
	thumbnail_publisher \
	srt_publisher \
	ovt_provider \
	rtmp_provider \
	srt_provider \
	mpegts_provider \
	rtspc_provider \
	webrtc_provider \
	scheduled_provider \
	multiplex_provider \
	transcoder \
	rtc_signalling \
	whip \
	address_utilities \
	ice \
	api_server \
	json_serdes \
	bitstream \
	http \
	dtls_srtp \
	rtp_rtcp \
	sdp \
	id3v2 \
	cue_event \
	amf_event \
	scte35_event \
	webvtt_format \
	segment_writer \
	web_console \
	mediarouter \
	rtsp_module \
	jitter_buffer \
	ovt_packetizer \
	orchestrator \
	origin_map_client \
	publisher \
	application \
	access_controller \
	physical_port \
	socket \
	ovcrypto \
	config \
	ovlibrary \
	monitoring \
	json_serdes \
	jsoncpp \
	dump \
	srt \
	file_provider \
	managed_queue \
	ffmpeg_wrapper \
	event \
	

# rtsp_provider 

LOCAL_PREBUILT_LIBRARIES := \
	libpugixml.a

LOCAL_LDFLAGS := -lpthread -luuid



$(call add_pkg_config,srt)
$(call add_pkg_config,libavformat)
$(call add_pkg_config,libavfilter)
$(call add_pkg_config,libavcodec)
$(call add_pkg_config,libswresample)
$(call add_pkg_config,libswscale)
$(call add_pkg_config,libavutil)
$(call add_pkg_config,openssl)
$(call add_pkg_config,vpx)
$(call add_pkg_config,opus)
$(call add_pkg_config,libsrtp2)
$(call add_pkg_config,libpcre2-8)
$(call add_pkg_config,hiredis)
$(call add_pkg_config,spdlog)
$(call add_pkg_config,whisper)

ifeq ($(call chk_pkg_exist,ffnvcodec),0)
$(call add_pkg_config,ffnvcodec)
endif

# Enable Xilinx Media SDK
# If libavcodec references libxrm.so, then the XMA library is supported
ifeq ($(and \
  $(filter 0,$(call chk_pkg_exist,libxrm)), \
  $(filter 0,$(call chk_dd_exist,$(CONFIG_LIBRARY_PATHS),libavcodec.so,libxrm.so)) \
), 0)
$(call add_pkg_config,libxma2api)
$(call add_pkg_config,xvbm)
$(call add_pkg_config,libxrm)
HWACCELS_XMA_ENABLED := true
PROJECT_CXXFLAGS += -DHWACCELS_XMA_ENABLED
endif

# Enable NVidia Accelerator
ifeq ($(and \
  $(filter 0,$(call chk_lib_exist,libcuda.so)), \
  $(filter 0,$(call chk_lib_exist,libnvidia-ml.so)), \
  $(filter 0,$(call chk_exe_exist,nvcc)) \
), 0)
HWACCELS_NVIDIA_ENABLED := true
PROJECT_CXXFLAGS += -I/usr/local/cuda/include -DHWACCELS_NVIDIA_ENABLED
LOCAL_LDFLAGS += -L/usr/local/cuda/lib64 -L/usr/local/cuda/lib64/stubs -lcuda -lnvidia-ml
endif

# Enable Netint Accelerator
ifeq ($(call chk_lib_exist,libxcoder_logan.so), 0)
HWACCELS_NILOGAN_ENABLED := true
PROJECT_CXXFLAGS += -DHWACCELS_NILOGAN_ENABLED
endif

# Enable libx264 
ifeq ($(call chk_dd_exist,$(CONFIG_LIBRARY_PATHS),libavcodec.so,libx264.so), 0)
THIRDP_LIBX264_ENABLED := true
PROJECT_CXXFLAGS += -DTHIRDP_LIBX264_ENABLED
endif

ifeq ($(shell echo $${OSTYPE}),linux-musl) 
# For alpine linux
LOCAL_LDFLAGS += -lexecinfo
endif


# Enable jemalloc 
ifeq ($(MAKECMDGOALS),release)
$(call add_pkg_config,jemalloc)
LOCAL_CFLAGS += -DOME_USE_JEMALLOC
LOCAL_CXXFLAGS += -DOME_USE_JEMALLOC
endif

# Setup flags for spdlog
LOCAL_CFLAGS += -DSPDLOG_COMPILED_LIB -Iprojects/third_party/spdlog-1.15.1/include
LOCAL_CXXFLAGS += -DSPDLOG_COMPILED_LIB -Iprojects/third_party/spdlog-1.15.1/include

LOCAL_TARGET := OvenMediaEngine

# Update git information
PRIVATE_MAIN_PATH := $(LOCAL_PATH)
$(shell "$(PRIVATE_MAIN_PATH)/update_git_info.sh" >/dev/null 2>&1)

BUILD_FILES_TO_CLEAN += "$(PRIVATE_MAIN_PATH)/git_info.h"

include $(BUILD_EXECUTABLE)
