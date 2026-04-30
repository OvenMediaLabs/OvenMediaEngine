#!/usr/bin/env cmake -P
#
# InstallPrerequisites.cmake
#
# Installs all external libraries required by OvenMediaEngine.
# Mirrors the logic of misc/prerequisites.sh, implemented as CMake script.
#
# Usage:
#   cmake -P cmake/InstallPrerequisites.cmake [options]
#
# Options (set via -D on the command line before -P):
#   -DOME_DEP_PREFIX=/opt/ovenmediaengine  Installation prefix (default)
#   -DOME_HWACCEL_NVIDIA=ON                Enable NVIDIA nv-codec-headers + CUDA FFmpeg/Whisper support
#   -DOME_HWACCEL_QSV=ON                   Enable Intel QSV (libmfx) FFmpeg support
#   -DOME_HWACCEL_XMA=ON                   Enable Xilinx XMA FFmpeg support
#   -DOME_HWACCEL_NILOGAN=ON               Enable Netint NiLogan FFmpeg support
#   -DOME_NILOGAN_PATCH_PATH=<path>        Path to NiLogan FFmpeg patch file (required with OME_HWACCEL_NILOGAN)
#   -DOME_NILOGAN_XCODER_COMPILE_PATH=<path>  Path to xcoder_logan source to compile (optional)
#   -DOME_ENABLE_X264=ON                   Enable libx264 (default ON)
#   -DOME_USE_CLANG=ON                     Install clang/lld and use as compiler (default ON)
#   -DTARGET=<name>                        Install only this target
#
# Example:
#   cmake -DOME_HWACCEL_NVIDIA=ON -P cmake/InstallPrerequisites.cmake
#   cmake -DOME_HWACCEL_XMA=ON -P cmake/InstallPrerequisites.cmake
#

cmake_minimum_required(VERSION 3.16)

# ==============================================================================
# Defaults
# ==============================================================================
if(NOT DEFINED OME_DEP_PREFIX)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(OME_DEP_PREFIX "$ENV{HOME}/opt/ovenmediaengine")
    else()
        set(OME_DEP_PREFIX /opt/ovenmediaengine)
    endif()
endif()
# Internal alias used throughout this file
set(PREFIX ${OME_DEP_PREFIX})
if(NOT DEFINED TEMP_PATH)
    set(TEMP_PATH "/tmp/ome_deps_$ENV{USER}")
endif()
if(NOT DEFINED OME_ENABLE_X264)
    set(OME_ENABLE_X264 ON)
endif()
# Internal alias used throughout this file
set(ENABLE_X264 ${OME_ENABLE_X264})
if(NOT DEFINED OME_HWACCEL_NVIDIA)
    set(OME_HWACCEL_NVIDIA OFF)
endif()
# Internal alias used throughout this file
set(ENABLE_NVIDIA ${OME_HWACCEL_NVIDIA})
if(NOT DEFINED OME_HWACCEL_QSV)
    set(OME_HWACCEL_QSV OFF)
endif()
if(NOT DEFINED OME_HWACCEL_XMA)
    set(OME_HWACCEL_XMA OFF)
endif()
if(NOT DEFINED OME_HWACCEL_NILOGAN)
    set(OME_HWACCEL_NILOGAN OFF)
endif()
if(NOT DEFINED OME_NILOGAN_PATCH_PATH)
    set(OME_NILOGAN_PATCH_PATH "")
endif()
if(NOT DEFINED OME_NILOGAN_XCODER_COMPILE_PATH)
    set(OME_NILOGAN_XCODER_COMPILE_PATH "")
endif()
# Internal aliases used throughout this file
set(ENABLE_QSV ${OME_HWACCEL_QSV})
set(ENABLE_XMA ${OME_HWACCEL_XMA})
set(ENABLE_NILOGAN ${OME_HWACCEL_NILOGAN})
set(NILOGAN_PATCH_PATH ${OME_NILOGAN_PATCH_PATH})
set(NILOGAN_XCODER_COMPILE_PATH ${OME_NILOGAN_XCODER_COMPILE_PATH})
if(NOT DEFINED OME_USE_CLANG)
    set(OME_USE_CLANG ON)
endif()
if(NOT DEFINED ENABLE_JEMALLOC_PROF)
    set(ENABLE_JEMALLOC_PROF OFF)
endif()

# Library versions - defined in a shared file so Dependencies.cmake can use the same values.
include("${CMAKE_CURRENT_LIST_DIR}/Versions.cmake")

# ------------------------------------------------------------------------------
# Parses an OME_VER_* definition declared as either:
#   set(OME_VER_FOO <verify-version>)
#   set(OME_VER_FOO <verify-version>@<install-ref>)
#
# Usage:
#   ome_parse_dep_version(OME_VER_SRT SRT_VERSION SRT_SOURCE_REF SRT_HAS_OVERRIDE)
#
# Outputs:
#   <verify-version>  -> version string used for pkg-config validation
#   <source-ref>      -> source ref used by the installer (version or override)
#   <has-override>    -> ON when "@<install-ref>" was provided
# ------------------------------------------------------------------------------
function(ome_parse_dep_version var_name out_verify_version out_source_ref out_has_override)
    set(_value "${${var_name}}")
    string(FIND "${_value}" "@" _sep_index)

    if(_sep_index EQUAL -1)
        set(_verify_version "${_value}")
        set(_source_ref "${_value}")
        set(_has_override OFF)
    else()
        string(REPLACE "@" ";" _parts "${_value}")
        list(LENGTH _parts _len)
        if(NOT _len EQUAL 2)
            message(FATAL_ERROR "${var_name} must be: <version> or <version>@<install-ref>")
        endif()
        list(GET _parts 0 _verify_version)
        list(GET _parts 1 _source_ref)
        set(_has_override ON)
    endif()

    set(${out_verify_version} "${_verify_version}" PARENT_SCOPE)
    set(${out_source_ref} "${_source_ref}" PARENT_SCOPE)
    set(${out_has_override} "${_has_override}" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# Selects the archive ref fragment that should be embedded into the download URL.
#
# Usage:
#   ome_select_archive_ref(SRT_ARCHIVE_REF
#       "${SRT_HAS_OVERRIDE}"
#       "${SRT_SOURCE_REF}"
#       "v${SRT_SOURCE_REF}")
#
# If an install-ref override exists, use it as-is. Otherwise, use the default
# archive ref pattern required by that dependency's upstream source archive.
# ------------------------------------------------------------------------------
function(ome_select_archive_ref out_var has_override source_ref default_archive_ref)
    if("${has_override}" STREQUAL "ON")
        set(${out_var} "${source_ref}" PARENT_SCOPE)
    else()
        set(${out_var} "${default_archive_ref}" PARENT_SCOPE)
    endif()
endfunction()

# Resolve all OME_VER_* declarations into verification versions and install refs
# before building per-dependency archive refs and source URLs below.
ome_parse_dep_version(OME_VER_OPENSSL OPENSSL_VERSION OPENSSL_SOURCE_REF OPENSSL_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_SRTP SRTP_VERSION SRTP_SOURCE_REF SRTP_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_SRT SRT_VERSION SRT_SOURCE_REF SRT_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_OPUS OPUS_VERSION OPUS_SOURCE_REF OPUS_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_VPX VPX_VERSION VPX_SOURCE_REF VPX_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_FDKAAC FDKAAC_VERSION FDKAAC_SOURCE_REF FDKAAC_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_NASM NASM_VERSION NASM_SOURCE_REF NASM_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_FFMPEG FFMPEG_VERSION FFMPEG_SOURCE_REF FFMPEG_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_JEMALLOC JEMALLOC_VERSION JEMALLOC_SOURCE_REF JEMALLOC_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_PCRE2 PCRE2_VERSION PCRE2_SOURCE_REF PCRE2_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_OPENH264 OPENH264_VERSION OPENH264_SOURCE_REF OPENH264_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_HIREDIS HIREDIS_VERSION HIREDIS_SOURCE_REF HIREDIS_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_NVCC_HDR NVCC_HDR_VERSION NVCC_HDR_SOURCE_REF NVCC_HDR_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_X264 X264_VERSION X264_SOURCE_REF X264_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_WEBP WEBP_VERSION WEBP_SOURCE_REF WEBP_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_SPDLOG SPDLOG_VERSION SPDLOG_SOURCE_REF SPDLOG_HAS_OVERRIDE)
ome_parse_dep_version(OME_VER_WHISPER WHISPER_VERSION WHISPER_SOURCE_REF WHISPER_HAS_OVERRIDE)

# Each dependency keeps its original URL pattern. The only thing that changes
# here is the archive ref fragment embedded into that pattern.
ome_select_archive_ref(OPENSSL_ARCHIVE_REF "${OPENSSL_HAS_OVERRIDE}" "${OPENSSL_SOURCE_REF}" "openssl-${OPENSSL_SOURCE_REF}")
ome_select_archive_ref(SRTP_ARCHIVE_REF "${SRTP_HAS_OVERRIDE}" "${SRTP_SOURCE_REF}" "v${SRTP_SOURCE_REF}")
ome_select_archive_ref(SRT_ARCHIVE_REF "${SRT_HAS_OVERRIDE}" "${SRT_SOURCE_REF}" "v${SRT_SOURCE_REF}")
ome_select_archive_ref(OPUS_ARCHIVE_REF "${OPUS_HAS_OVERRIDE}" "${OPUS_SOURCE_REF}" "refs/tags/v${OPUS_SOURCE_REF}")
ome_select_archive_ref(VPX_ARCHIVE_REF "${VPX_HAS_OVERRIDE}" "${VPX_SOURCE_REF}" "refs/tags/v${VPX_SOURCE_REF}")
ome_select_archive_ref(FDKAAC_ARCHIVE_REF "${FDKAAC_HAS_OVERRIDE}" "${FDKAAC_SOURCE_REF}" "v${FDKAAC_SOURCE_REF}")
ome_select_archive_ref(NASM_ARCHIVE_REF "${NASM_HAS_OVERRIDE}" "${NASM_SOURCE_REF}" "refs/tags/nasm-${NASM_SOURCE_REF}")
ome_select_archive_ref(FFMPEG_ARCHIVE_REF "${FFMPEG_HAS_OVERRIDE}" "${FFMPEG_SOURCE_REF}" "refs/tags/n${FFMPEG_SOURCE_REF}")
ome_select_archive_ref(JEMALLOC_ARCHIVE_REF "${JEMALLOC_HAS_OVERRIDE}" "${JEMALLOC_SOURCE_REF}" "refs/tags/${JEMALLOC_SOURCE_REF}")
ome_select_archive_ref(PCRE2_ARCHIVE_REF "${PCRE2_HAS_OVERRIDE}" "${PCRE2_SOURCE_REF}" "refs/tags/pcre2-${PCRE2_SOURCE_REF}")
ome_select_archive_ref(OPENH264_ARCHIVE_REF "${OPENH264_HAS_OVERRIDE}" "${OPENH264_SOURCE_REF}" "refs/tags/v${OPENH264_SOURCE_REF}")
ome_select_archive_ref(HIREDIS_ARCHIVE_REF "${HIREDIS_HAS_OVERRIDE}" "${HIREDIS_SOURCE_REF}" "refs/tags/v${HIREDIS_SOURCE_REF}")
ome_select_archive_ref(NVCC_HDR_ARCHIVE_REF "${NVCC_HDR_HAS_OVERRIDE}" "${NVCC_HDR_SOURCE_REF}" "refs/tags/n${NVCC_HDR_SOURCE_REF}")
ome_select_archive_ref(X264_ARCHIVE_REF "${X264_HAS_OVERRIDE}" "${X264_SOURCE_REF}/x264-${X264_SOURCE_REF}" "master/x264-${X264_SOURCE_REF}")
ome_select_archive_ref(WEBP_ARCHIVE_REF "${WEBP_HAS_OVERRIDE}" "${WEBP_SOURCE_REF}" "refs/tags/v${WEBP_SOURCE_REF}")
ome_select_archive_ref(SPDLOG_ARCHIVE_REF "${SPDLOG_HAS_OVERRIDE}" "${SPDLOG_SOURCE_REF}" "refs/tags/v${SPDLOG_SOURCE_REF}")
ome_select_archive_ref(WHISPER_ARCHIVE_REF "${WHISPER_HAS_OVERRIDE}" "${WHISPER_SOURCE_REF}" "refs/tags/v${WHISPER_SOURCE_REF}")

set(OPENSSL_SOURCE_URL "https://github.com/openssl/openssl/archive/${OPENSSL_ARCHIVE_REF}.tar.gz")
set(SRTP_SOURCE_URL "https://github.com/cisco/libsrtp/archive/${SRTP_ARCHIVE_REF}.tar.gz")
set(SRT_SOURCE_URL "https://github.com/Haivision/srt/archive/${SRT_ARCHIVE_REF}.tar.gz")
set(OPUS_SOURCE_URL "https://archive.mozilla.org/pub/opus/opus-${OPUS_SOURCE_REF}.tar.gz")
set(VPX_SOURCE_URL "https://codeload.github.com/webmproject/libvpx/tar.gz/${VPX_ARCHIVE_REF}")
set(FDKAAC_SOURCE_URL "https://github.com/mstorsjo/fdk-aac/archive/${FDKAAC_ARCHIVE_REF}.tar.gz")
set(NASM_SOURCE_URL "https://github.com/netwide-assembler/nasm/archive/${NASM_ARCHIVE_REF}.tar.gz")
set(FFMPEG_SOURCE_URL "https://github.com/FFmpeg/FFmpeg/archive/${FFMPEG_ARCHIVE_REF}.tar.gz")
set(JEMALLOC_SOURCE_URL "https://github.com/jemalloc/jemalloc/releases/download/${JEMALLOC_SOURCE_REF}/jemalloc-${JEMALLOC_SOURCE_REF}.tar.bz2")
set(PCRE2_SOURCE_URL "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${PCRE2_SOURCE_REF}/pcre2-${PCRE2_SOURCE_REF}.tar.gz")
set(OPENH264_SOURCE_URL "https://github.com/cisco/openh264/archive/${OPENH264_ARCHIVE_REF}.tar.gz")
set(HIREDIS_SOURCE_URL "https://github.com/redis/hiredis/archive/${HIREDIS_ARCHIVE_REF}.tar.gz")
set(NVCC_HDR_SOURCE_URL "https://github.com/FFmpeg/nv-codec-headers/releases/download/n${NVCC_HDR_SOURCE_REF}/nv-codec-headers-${NVCC_HDR_SOURCE_REF}.tar.gz")
set(X264_SOURCE_URL "https://code.videolan.org/videolan/x264/-/archive/${X264_ARCHIVE_REF}.tar.bz2")
set(WEBP_SOURCE_URL "https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-${WEBP_SOURCE_REF}.tar.gz")
set(SPDLOG_SOURCE_URL "https://github.com/gabime/spdlog/archive/${SPDLOG_ARCHIVE_REF}.tar.gz")
set(WHISPER_SOURCE_URL "https://github.com/ggml-org/whisper.cpp/archive/${WHISPER_ARCHIVE_REF}.tar.gz")

# ==============================================================================
# Detect OS
# ==============================================================================
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(EXISTS /etc/os-release)
        file(READ /etc/os-release _os_release)
        string(REGEX MATCH "(^|\n)NAME=\"?([^\"\n]+)\"?" _m "${_os_release}")
        set(OSNAME "${CMAKE_MATCH_2}")
        string(REGEX MATCH "VERSION=\"?([0-9]+)" _m "${_os_release}")
        set(OSVERSION "${CMAKE_MATCH_1}")
    endif()
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(OSNAME "Mac OS X")
endif()

message(STATUS "[OME Prerequisites] OS: ${OSNAME} ${OSVERSION}")
message(STATUS "[OME Prerequisites] Prefix: ${PREFIX}")

# ==============================================================================
# Helper: run shell command and fail loudly
# ==============================================================================
macro(ome_run cmd label)
    message(STATUS "[OME Prerequisites] Building: ${label}")
    set(_ome_script "${TEMP_PATH}/_ome_${label}.sh")
    file(WRITE "${_ome_script}" "#!/bin/bash\nset -e\nexport ${_COMMON_ENV}\n${cmd}\n")
    execute_process(
        COMMAND bash "${_ome_script}"
        RESULT_VARIABLE _ret
        ECHO_OUTPUT_VARIABLE
        ECHO_ERROR_VARIABLE
    )
    file(REMOVE "${_ome_script}")
    if(NOT _ret EQUAL 0)
        message(FATAL_ERROR "[OME Prerequisites] FAILED: ${label}")
    endif()
endmacro()

# ==============================================================================
# Install base packages
# ==============================================================================
if(OSNAME MATCHES "Ubuntu")
    ome_run("${_SUDO}apt-get install -y build-essential autoconf automake libtool zlib1g-dev \
        tclsh cmake curl pkg-config bc uuid-dev git libgomp1 ninja-build" "apt base packages")
elseif(OSNAME MATCHES "Rocky|AlmaLinux|Red")
    ome_run("${_SUDO}dnf install -y bc gcc-c++ autoconf libtool tcl bzip2 zlib-devel \
        cmake libuuid-devel which diffutils perl-IPC-Cmd git libgomp ninja-build" "dnf base packages")
elseif(OSNAME MATCHES "Amazon Linux")
    ome_run("${_SUDO}yum install -y bc gcc-c++ autoconf libtool tcl bzip2 zlib-devel \
        cmake libuuid-devel perl-IPC-Cmd git libgomp ninja-build" "yum base packages")
elseif(OSNAME MATCHES "Fedora")
    ome_run("${_SUDO}yum install -y gcc-c++ make autoconf libtool zlib-devel tcl cmake \
        bc libuuid-devel perl-IPC-Cmd git libgomp ninja-build" "yum base packages (fedora)")
elseif(OSNAME MATCHES "Mac OS X")
    ome_run("brew install pkg-config nasm automake libtool xz cmake make ninja" "brew base packages")
else()
    message(WARNING "[OME Prerequisites] Unsupported OS: ${OSNAME}. Skipping base package installation.")
endif()

# ==============================================================================
# Install Clang (when OME_USE_CLANG=ON, which is the default)
# ==============================================================================
if(OME_USE_CLANG)
    message(STATUS "[OME Prerequisites] Installing clang/lld (OME_USE_CLANG=ON)")
    if(OSNAME MATCHES "Ubuntu")
        ome_run("${_SUDO}apt-get install -y clang lld" "apt clang")
    elseif(OSNAME MATCHES "Rocky|AlmaLinux|Red")
        ome_run("${_SUDO}dnf install -y clang lld" "dnf clang")
    elseif(OSNAME MATCHES "Amazon Linux")
        ome_run("${_SUDO}yum install -y clang" "yum clang")
    elseif(OSNAME MATCHES "Fedora")
        ome_run("${_SUDO}yum install -y clang lld" "yum clang (fedora)")
    elseif(OSNAME MATCHES "Mac OS X")
        # Xcode CLT ships clang; nothing extra needed
        message(STATUS "[OME Prerequisites] macOS: Xcode CLT already provides clang. Skipping.")
    else()
        message(WARNING "[OME Prerequisites] Unsupported OS: ${OSNAME}. Skipping clang installation.")
    endif()
else()
    message(STATUS "[OME Prerequisites] OME_USE_CLANG=OFF - skipping clang installation")
endif()

# ==============================================================================
# Individual install functions (implemented as cmake variables holding shell code)
# ==============================================================================

set(_COMMON_ENV "PATH=${PREFIX}/bin:$PATH PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig:${PREFIX}/lib64/pkgconfig:$PKG_CONFIG_PATH")
set(_J "-j$(nproc 2>/dev/null || sysctl -n hw.ncpu)")
# On macOS we install to a user-writable prefix — no ${_SUDO}needed
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_SUDO "")
else()
    set(_SUDO "sudo")
endif()

# ---- NASM ----
set(_install_nasm "
mkdir -p ${TEMP_PATH}/nasm && cd ${TEMP_PATH}/nasm &&
curl -sSLf ${NASM_SOURCE_URL} | tar -xz --strip-components=1 &&
./autogen.sh && ./configure --prefix=${PREFIX} &&
make ${_J} && touch nasm.1 ndisasm.1 && ${_SUDO}make install && rm -rf ${TEMP_PATH}/nasm
")

# ---- OpenSSL ----
set(_install_openssl "
mkdir -p ${TEMP_PATH}/openssl && cd ${TEMP_PATH}/openssl &&
curl -sSLf ${OPENSSL_SOURCE_URL} | tar -xz --strip-components=1 &&
./config --prefix=${PREFIX} --openssldir=${PREFIX} --libdir=lib -Wl,-rpath,${PREFIX}/lib shared no-idea no-mdc2 no-rc5 no-ec2m no-ecdh no-ecdsa no-async &&
make ${_J} && ${_SUDO}make install_sw && rm -rf ${TEMP_PATH}/openssl
")

# ---- libsrtp ----
set(_install_libsrtp "
mkdir -p ${TEMP_PATH}/srtp && cd ${TEMP_PATH}/srtp &&
curl -sSLf ${SRTP_SOURCE_URL} | tar -xz --strip-components=1 &&
./configure --prefix=${PREFIX} --enable-openssl --with-openssl-dir=${PREFIX} &&
make ${_J} shared_library && ${_SUDO}make install && rm -rf ${TEMP_PATH}/srtp
")

# ---- SRT ----
set(_install_libsrt "
mkdir -p ${TEMP_PATH}/srt && cd ${TEMP_PATH}/srt &&
curl -sSLf ${SRT_SOURCE_URL} | tar -xz --strip-components=1 &&
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${PREFIX} -DENABLE_SHARED=1 -DENABLE_STATIC=0 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 &&
cmake --build build ${_J} &&
${_SUDO}cmake --install build && rm -rf ${TEMP_PATH}/srt
")

# ---- Opus ----
set(_install_libopus "
mkdir -p ${TEMP_PATH}/opus && cd ${TEMP_PATH}/opus &&
curl -sSLf ${OPUS_SOURCE_URL} | tar -xz --strip-components=1 &&
autoreconf -fiv && ./configure --prefix=${PREFIX} --enable-shared --disable-static &&
make ${_J} && ${_SUDO}make install && ${_SUDO}rm -rf ${PREFIX}/share && rm -rf ${TEMP_PATH}/opus
")

# ---- libvpx ----
# On macOS, libvpx's shared library build system uses Linux GNU linker flags
# (--no-undefined, -soname, --version-script) which macOS ld does not support.
# Build as static instead; FFmpeg and OME can link against libvpx.a.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_VPX_SHARED "--disable-shared --enable-static")
else()
    set(_VPX_SHARED "--enable-shared --disable-static")
endif()
set(_install_libvpx "
mkdir -p ${TEMP_PATH}/vpx && cd ${TEMP_PATH}/vpx &&
curl -sSLf ${VPX_SOURCE_URL} | tar -xz --strip-components=1 &&
./configure --prefix=${PREFIX} --enable-vp8 --enable-pic ${_VPX_SHARED} --disable-vp9 --disable-debug --disable-examples --disable-docs --disable-install-bins &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/vpx
")

# ---- libwebp ----
set(_install_libwebp "
mkdir -p ${TEMP_PATH}/webp && cd ${TEMP_PATH}/webp &&
curl -sSLf ${WEBP_SOURCE_URL} | tar -xz --strip-components=1 &&
./configure --prefix=${PREFIX} --enable-shared --disable-static &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/webp
")

# ---- fdk-aac ----
set(_install_fdk_aac "
mkdir -p ${TEMP_PATH}/aac && cd ${TEMP_PATH}/aac &&
curl -sSLf ${FDKAAC_SOURCE_URL} | tar -xz --strip-components=1 &&
autoreconf -fiv && ./configure --prefix=${PREFIX} --enable-shared --disable-static --datadir=/tmp/aac &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/aac
")

# ---- openh264 ----
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_OPENH264_OS "OS=darwin")
else()
    set(_OPENH264_OS "OS=linux")
endif()
set(_install_libopenh264 "
mkdir -p ${TEMP_PATH}/openh264 && cd ${TEMP_PATH}/openh264 &&
curl -sSLf ${OPENH264_SOURCE_URL} | tar -xz --strip-components=1 &&
sed -i -e 's|PREFIX=/usr/local|PREFIX=${PREFIX}|' Makefile &&
make ${_OPENH264_OS} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/openh264
")

# ---- x264 (optional) ----
set(_install_libx264 "
mkdir -p ${TEMP_PATH}/x264 && cd ${TEMP_PATH}/x264 &&
curl -sLf ${X264_SOURCE_URL} | tar -jx --strip-components=1 &&
./configure --prefix=${PREFIX} --enable-shared --enable-pic --disable-cli &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/x264
")

# ---- nv-codec-headers (optional) ----
set(_install_nvcc_hdr "
mkdir -p ${TEMP_PATH}/nvcc-hdr && cd ${TEMP_PATH}/nvcc-hdr &&
curl -sSLf ${NVCC_HDR_SOURCE_URL} | tar -xz --strip-components=1 &&
${_SUDO}make PREFIX=${PREFIX} LIBDIR=lib install
")

# ---- FFmpeg ----
set(_FFMPEG_ADDI_LICENSE      "")
set(_FFMPEG_ADDI_LIBS         "--disable-nvdec --disable-vaapi --disable-vdpau --disable-cuda-llvm --disable-cuvid --disable-ffnvcodec")
# --disable-new-dtags is a Linux GNU ld flag; not supported on macOS
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_FFMPEG_DTAGS_FLAG "")
else()
    set(_FFMPEG_DTAGS_FLAG " -Wl,--disable-new-dtags")
endif()
set(_FFMPEG_ADDI_ENCODER      "")
set(_FFMPEG_ADDI_DECODER      "")
set(_FFMPEG_ADDI_FILTERS      "")
set(_FFMPEG_ADDI_CFLAGS       "")
set(_FFMPEG_ADDI_LDFLAGS      "")
set(_FFMPEG_ADDI_EXTRA_LIBS   "")
set(_FFMPEG_PATCH_CMDS        "")

# scte35 patch (always applied if patch file exists)
set(_SCTE35_PATCH "${CMAKE_CURRENT_LIST_DIR}/../misc/patches/ffmpeg_n${FFMPEG_VERSION}_scte35.patch")
if(EXISTS "${_SCTE35_PATCH}")
    string(APPEND _FFMPEG_PATCH_CMDS "patch -p1 < ${_SCTE35_PATCH} && \n")
endif()

if(ENABLE_X264)
    string(APPEND _FFMPEG_ADDI_LIBS    " --enable-libx264")
    string(APPEND _FFMPEG_ADDI_ENCODER ",libx264")
    string(APPEND _FFMPEG_ADDI_LICENSE " --enable-gpl --enable-nonfree")
endif()

if(ENABLE_NVIDIA)
    string(REPLACE "PATH=${PREFIX}/bin:" "PATH=/usr/local/cuda/bin:${PREFIX}/bin:" _COMMON_ENV "${_COMMON_ENV}")
    string(APPEND _FFMPEG_ADDI_LIBS    " --enable-cuda-nvcc --enable-cuda-llvm --enable-nvenc --enable-nvdec --enable-ffnvcodec --enable-cuvid")
    string(APPEND _FFMPEG_ADDI_ENCODER ",h264_nvenc,hevc_nvenc")
    string(APPEND _FFMPEG_ADDI_DECODER ",h264_cuvid,hevc_cuvid")
    string(APPEND _FFMPEG_ADDI_FILTERS ",scale_cuda,hwdownload,hwupload,hwupload_cuda")
    string(APPEND _FFMPEG_ADDI_CFLAGS  " -I/usr/local/cuda/include")
    string(APPEND _FFMPEG_ADDI_LDFLAGS " -L/usr/local/cuda/lib64")
    string(APPEND _FFMPEG_ADDI_LICENSE " --enable-nonfree")
endif()

if(ENABLE_QSV)
    string(APPEND _FFMPEG_ADDI_LIBS    " --enable-libmfx")
    string(APPEND _FFMPEG_ADDI_ENCODER ",h264_qsv,hevc_qsv")
    string(APPEND _FFMPEG_ADDI_DECODER ",vp8_qsv,h264_qsv,hevc_qsv")
endif()

if(ENABLE_XMA)
    set(_XMA_PATCH "${CMAKE_CURRENT_LIST_DIR}/../misc/patches/ffmpeg_n${FFMPEG_VERSION}_u30ma.patch")
    if(EXISTS "${_XMA_PATCH}")
        string(APPEND _FFMPEG_PATCH_CMDS "patch -p1 < ${_XMA_PATCH} && \n")
    else()
        message(WARNING "[OME Prerequisites] XMA patch not found: ${_XMA_PATCH}")
    endif()
    string(APPEND _FFMPEG_ADDI_LIBS      " --enable-x86asm --enable-libxma2api --enable-libxvbm --enable-libxrm --enable-cross-compile")
    string(APPEND _FFMPEG_ADDI_ENCODER   ",h264_vcu_mpsoc,hevc_vcu_mpsoc")
    string(APPEND _FFMPEG_ADDI_DECODER   ",h264_vcu_mpsoc,hevc_vcu_mpsoc")
    string(APPEND _FFMPEG_ADDI_FILTERS   ",multiscale_xma,xvbm_convert")
    string(APPEND _FFMPEG_ADDI_CFLAGS    " -I/opt/xilinx/xrt/include/xma2")
    string(APPEND _FFMPEG_ADDI_LDFLAGS   " -L/opt/xilinx/xrt/lib -L/opt/xilinx/xrm/lib -Wl,-rpath,/opt/xilinx/xrt/lib,-rpath,/opt/xilinx/xrm/lib")
    string(APPEND _FFMPEG_ADDI_EXTRA_LIBS " --extra-libs=-lxma2api --extra-libs=-lxrt_core --extra-libs=-lxrm --extra-libs=-lxrt_coreutil --extra-libs=-lpthread --extra-libs=-ldl")
endif()

if(ENABLE_NILOGAN)
    if(NILOGAN_PATCH_PATH AND EXISTS "${NILOGAN_PATCH_PATH}")
        string(APPEND _FFMPEG_PATCH_CMDS "cp ${NILOGAN_PATCH_PATH} . && patch -t -p1 < \$(basename ${NILOGAN_PATCH_PATH}) && \n")
    else()
        message(WARNING "[OME Prerequisites] ENABLE_NILOGAN=ON but NILOGAN_PATCH_PATH not set or not found")
    endif()
    if(NILOGAN_XCODER_COMPILE_PATH AND NOT NILOGAN_XCODER_COMPILE_PATH STREQUAL "")
        string(APPEND _FFMPEG_PATCH_CMDS "(cd ${NILOGAN_XCODER_COMPILE_PATH} && bash build.sh && ldconfig) && \n")
    endif()
    string(APPEND _FFMPEG_ADDI_LIBS    " --enable-libxcoder_logan --enable-ni_logan --enable-avfilter --enable-pthreads")
    string(APPEND _FFMPEG_ADDI_ENCODER ",h264_ni_logan,h265_ni_logan")
    string(APPEND _FFMPEG_ADDI_DECODER ",h264_ni_logan,h265_ni_logan")
    string(APPEND _FFMPEG_ADDI_FILTERS ",hwdownload,hwupload,hwupload_ni_logan")
    string(APPEND _FFMPEG_ADDI_LICENSE " --enable-gpl --enable-nonfree")
    string(APPEND _FFMPEG_ADDI_LDFLAGS " -lm -ldl")
endif()

set(_FFMPEG_CONFIGURE_CMD
    "./configure"
    "--prefix=${PREFIX}"
    "--disable-everything --disable-programs --disable-avdevice --disable-dwt --disable-lsp --disable-faan --disable-pixelutils"
    "--enable-shared --disable-static --enable-pic"
    "--enable-zlib --enable-libopus --enable-libvpx --enable-libfdk_aac --enable-libopenh264 --enable-openssl"
    "--enable-network --enable-libsrt --enable-dct --enable-rdft --enable-libwebp"
    "--extra-cflags=\"-I${PREFIX}/include${_FFMPEG_ADDI_CFLAGS}\""
    "--extra-ldflags=\"-L${PREFIX}/lib -Wl,-rpath,${PREFIX}/lib${_FFMPEG_DTAGS_FLAG}${_FFMPEG_ADDI_LDFLAGS}\""
    "--extra-libs=-ldl"
    "${_FFMPEG_ADDI_EXTRA_LIBS}"
    "${_FFMPEG_ADDI_LICENSE}"
    "${_FFMPEG_ADDI_LIBS}"
    "--enable-encoder=libvpx_vp8,libopus,libfdk_aac,libopenh264,mjpeg,png,libwebp${_FFMPEG_ADDI_ENCODER}"
    "--enable-decoder=aac,aac_latm,aac_fixed,mp3float,mp3,h264,hevc,opus,vp8,mjpeg,png${_FFMPEG_ADDI_DECODER}"
    "--enable-parser=aac,aac_latm,aac_fixed,h264,hevc,opus,vp8,png,jpg"
    "--enable-protocol=tcp,udp,rtp,file,rtmp,tls,rtmps,libsrt"
    "--enable-demuxer=rtsp,flv,live_flv,mp4,mp3,image2"
    "--enable-muxer=mp4,webm,mpegts,flv,mpjpeg"
    "--enable-filter=asetnsamples,aresample,aformat,channelmap,channelsplit,scale,transpose,fps,settb,asettb,crop,format${_FFMPEG_ADDI_FILTERS}"
)
list(JOIN _FFMPEG_CONFIGURE_CMD " " _FFMPEG_CONFIGURE_LINE)

set(_install_ffmpeg "
mkdir -p ${TEMP_PATH}/ffmpeg && cd ${TEMP_PATH}/ffmpeg &&
curl -sSLf ${FFMPEG_SOURCE_URL} | tar -xz --strip-components=1 &&
${_FFMPEG_PATCH_CMDS}${_FFMPEG_CONFIGURE_LINE} &&
make ${_J} && ${_SUDO}make install && ${_SUDO}rm -rf ${PREFIX}/share && rm -rf ${TEMP_PATH}/ffmpeg
")

# ---- stubs ----
# Built via CMake (misc/stubs/CMakeLists.txt) instead of the legacy Makefile.
set(_STUB_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(_install_stubs "
cmake -S ${_STUB_DIR} -B ${_STUB_DIR}/build/stubs -DOME_BUILD_STUBS=ON -DCMAKE_INSTALL_PREFIX=${PREFIX} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 &&
cmake --build ${_STUB_DIR}/build/stubs --target stubs -j$(nproc) &&
${_SUDO}cmake --install ${_STUB_DIR}/build/stubs --component stubs
")

# ---- jemalloc ----
if(ENABLE_JEMALLOC_PROF)
    set(_JEMALLOC_PROF_FLAG "--enable-prof")
else()
    set(_JEMALLOC_PROF_FLAG "")
endif()
set(_install_jemalloc "
mkdir -p ${TEMP_PATH}/jemalloc && cd ${TEMP_PATH}/jemalloc &&
curl -sSLf ${JEMALLOC_SOURCE_URL} | tar -jx --strip-components=1 &&
./configure --prefix=${PREFIX} --enable-shared ${_JEMALLOC_PROF_FLAG} &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/jemalloc
")

# ---- PCRE2 ----
set(_install_libpcre2 "
mkdir -p ${TEMP_PATH}/pcre2 && cd ${TEMP_PATH}/pcre2 &&
curl -sSLf ${PCRE2_SOURCE_URL} | tar -xz --strip-components=1 &&
./configure --prefix=${PREFIX} --enable-shared --disable-static &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/pcre2
")

# ---- hiredis ----
set(_install_hiredis "
mkdir -p ${TEMP_PATH}/hiredis && cd ${TEMP_PATH}/hiredis &&
curl -sSLf ${HIREDIS_SOURCE_URL} | tar -xz --strip-components=1 &&
make ${_J} PREFIX=${PREFIX} LIBRARY_PATH=lib && ${_SUDO}make install PREFIX=${PREFIX} LIBRARY_PATH=lib && rm -rf ${TEMP_PATH}/hiredis
")

# ---- spdlog ----
set(_install_spdlog "
mkdir -p ${TEMP_PATH}/spdlog && cd ${TEMP_PATH}/spdlog &&
curl -sSLf ${SPDLOG_SOURCE_URL} | tar -xz --strip-components=1 &&
mkdir -p build && cd build &&
cmake .. -DCMAKE_INSTALL_PREFIX=${PREFIX} -DCMAKE_INSTALL_LIBDIR=${PREFIX}/lib -DCMAKE_POLICY_VERSION_MINIMUM=3.5 &&
make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/spdlog
")

# ---- whisper.cpp ----
set(_WHISPER_CUDA 0)
if(ENABLE_NVIDIA)
    set(_WHISPER_CUDA 1)
endif()
set(_WHISPER_CMAKE_ARGS
    "cmake -B build -S ."
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_PREFIX=${PREFIX}"
    "-DCMAKE_INSTALL_RPATH=${PREFIX}/lib"
    "-DBUILD_SHARED_LIBS=ON"
    "-DWHISPER_BUILD_EXAMPLES=OFF"
    "-DWHISPER_BUILD_TESTS=OFF"
    "-DWHISPER_BUILD_SERVER=OFF"
    "-DGGML_CUDA=${_WHISPER_CUDA}"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
)
if(OME_HWACCEL_NVIDIA)
    list(APPEND _WHISPER_CMAKE_ARGS "\"-DCMAKE_CUDA_ARCHITECTURES=61\;75\;80\;86\;89\"")
    find_program(_OME_NVCC nvcc HINTS /usr/local/cuda/bin /usr/cuda/bin)
    if(_OME_NVCC)
        list(APPEND _WHISPER_CMAKE_ARGS "-DCMAKE_CUDA_COMPILER=${_OME_NVCC}")
    else()
        message(WARNING "[OME] nvcc not found - whisper CUDA build may fail. Install CUDA toolkit or add nvcc to PATH.")
    endif()
endif()
list(JOIN _WHISPER_CMAKE_ARGS " " _WHISPER_CMAKE_LINE)
set(_install_whisper "
mkdir -p ${TEMP_PATH}/whisper && cd ${TEMP_PATH}/whisper &&
curl -sSLf ${WHISPER_SOURCE_URL} | tar -xz --strip-components=1 &&
${_WHISPER_CMAKE_LINE} &&
cd build && make ${_J} && ${_SUDO}make install && rm -rf ${TEMP_PATH}/whisper
")

# ==============================================================================
# Install sequence
# ==============================================================================
set(_targets
    nasm
    openssl
    libsrtp
    libsrt
    libopus
    libopenh264
    libvpx
    libwebp
    fdk_aac
    ffmpeg
    jemalloc
    libpcre2
    hiredis
    spdlog
)
# stubs are GPU acceleration shims — not needed on macOS
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND _targets stubs)
endif()
set(_targets ${_targets}
    whisper
)

if(ENABLE_X264)
    list(INSERT _targets 5 libx264)
endif()

if(ENABLE_NVIDIA)
    list(APPEND _targets nvcc_hdr)
endif()

# Override with single target if requested
if(DEFINED TARGET)
    if("${TARGET}" STREQUAL "ffmpeg")
        # ffmpeg depends on codec libs; install them first in case they are missing
        set(_ffmpeg_deps nasm libopus libvpx libwebp libopenh264 fdk_aac)
        if(ENABLE_X264)
            list(APPEND _ffmpeg_deps libx264)
        endif()
        if(ENABLE_NVIDIA)
            list(APPEND _ffmpeg_deps nvcc_hdr)
        endif()
        set(_targets ${_ffmpeg_deps} ffmpeg)
    elseif("${TARGET}" STREQUAL "libvpx")
        # libvpx requires nasm as assembler
        set(_targets nasm libvpx)
    elseif("${TARGET}" STREQUAL "fdk_aac" OR "${TARGET}" STREQUAL "libx264")
        # these also require nasm
        set(_targets nasm ${TARGET})
    else()
        set(_targets ${TARGET})
    endif()
endif()

message(STATUS "[OME Prerequisites] Install targets: ${_targets}")
message(STATUS "[OME Prerequisites] This will build and install to: ${PREFIX}")
message(STATUS "[OME Prerequisites] Temp directory: ${TEMP_PATH}")

file(MAKE_DIRECTORY ${TEMP_PATH})

foreach(_t ${_targets})
    if(DEFINED _install_${_t})
        ome_run("${_install_${_t}}" "${_t}")
    else()
        message(WARNING "[OME Prerequisites] No install script for: ${_t}")
    endif()
endforeach()

message(STATUS "[OME Prerequisites] All dependencies installed successfully.")
file(REMOVE_RECURSE "${TEMP_PATH}")
message(STATUS "[OME Prerequisites] Cleaned up temp directory: ${TEMP_PATH}")
message(STATUS "[OME Prerequisites] You can now configure and build OvenMediaEngine:")
message(STATUS "  cmake -Bbuild -DCMAKE_BUILD_TYPE=Release .")
message(STATUS "  cmake --build build -j\$(nproc)")
