#
# Dependencies.cmake
# Locate all external libraries used by OvenMediaEngine via pkg-config.
# The custom search path ${OME_DEP_PREFIX}/lib/pkgconfig is set by
# CompilerOptions.cmake via ENV{PKG_CONFIG_PATH}.
# Exact version matching (=) is required — higher versions also trigger reinstall.
#

include(Versions)   # OME_VER_* constants shared with InstallPrerequisites.cmake
find_package(PkgConfig REQUIRED)

# ------------------------------------------------------------------------------
# Parses an OME_VER_* definition declared as either:
#   set(OME_VER_FOO <verify-version>)
#   set(OME_VER_FOO <verify-version>@<install-ref>)
#
# Usage:
#   ome_parse_dep_version(OME_VER_SRT SRT_VERIFY_VERSION SRT_SOURCE_REF SRT_HAS_OVERRIDE)
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
# Helper: import a pkg-config package and create a canonical CMake imported
# target called PkgConfig::<PKG_VAR> (same as ome_target_pkg_config uses).
#
# Usage:
#   ome_find_pkg(<variable-prefix> <pkg-config-name> <version-var>
#                [OPTIONAL]
#                [REINSTALL_TARGET target]
#                [EXTRA_ARGS ...]
#                [VERSION_OP =|>=]
#                [PROBE_LIBRARY library-name]
#                [ON_MISSING FATAL|DISABLE]
#                [ON_MISMATCH FATAL|DISABLE])
#
# Example:
#   ome_find_pkg(PKG_OPENSSL openssl OME_VER_OPENSSL REINSTALL_TARGET openssl)
#   ome_find_pkg(PKG_X264 x264 OME_VER_X264 REINSTALL_TARGET libx264
#                PROBE_LIBRARY x264 ON_MISSING DISABLE ON_MISMATCH FATAL)
#
# Finds a pkg-config package with an exact version constraint derived from the
# OME_VER_* definition. If not found or version differs, re-runs
# InstallPrerequisites for the specific REINSTALL_TARGET only (or the full
# prerequisites if not specified).
macro(ome_find_pkg var pkg version_var)
    cmake_parse_arguments(_FP "OPTIONAL" "REINSTALL_TARGET;PROBE_LIBRARY;ON_MISSING;ON_MISMATCH;VERSION_OP" "EXTRA_ARGS" ${ARGN})

    ome_parse_dep_version(${version_var} _FP_VERIFY_VERSION _FP_SOURCE_REF _FP_HAS_OVERRIDE)
    if(NOT _FP_VERSION_OP)
        set(_FP_VERSION_OP "=")
    endif()
    set(_FP_PKG_STRING "${pkg}${_FP_VERSION_OP}${_FP_VERIFY_VERSION}")

    string(REGEX REPLACE "[<>=].*$" "" _FP_PKG_NAME "${_FP_PKG_STRING}")
    string(REGEX MATCH "[<>=]+(.+)$" _FP_VERSION_MATCH "${_FP_PKG_STRING}")
    set(_FP_REQUIRED_VERSION "${CMAKE_MATCH_1}")
    set(_FP_STATUS_PREFIX "[OME] Checking for module '${_FP_PKG_NAME}'")
    if(NOT _FP_REQUIRED_VERSION STREQUAL "")
        string(APPEND _FP_STATUS_PREFIX " (required: ${_FP_REQUIRED_VERSION})")
    endif()

    if(NOT _FP_ON_MISSING)
        if(_FP_OPTIONAL)
            set(_FP_ON_MISSING DISABLE)
        else()
            set(_FP_ON_MISSING FATAL)
        endif()
    endif()
    if(NOT _FP_ON_MISMATCH)
        if(_FP_OPTIONAL)
            set(_FP_ON_MISMATCH DISABLE)
        else()
            set(_FP_ON_MISMATCH FATAL)
        endif()
    endif()

    set(_FP_FOUND_BUT_MISMATCH OFF)
    set(_FP_FOUND_VERSION "")
    set(_FP_PROBE_FOUND TRUE)

    # Always clear cached result so pkg-config re-runs every configure.
    # This ensures a deleted/changed OME_DEP_PREFIX is detected immediately
    # instead of silently using stale cached paths.
    unset(${var}_FOUND CACHE)
    unset(${var}_INCLUDE_DIRS CACHE)
    unset(${var}_LIBRARY_DIRS CACHE)
    unset(${var}_LIBRARIES CACHE)
    unset(${var}_VERSION CACHE)
    # Also clear the pkgcfg_lib_* variables set by find_library() inside
    # pkg_check_modules(IMPORTED_TARGET). If OME_DEP_PREFIX changes between
    # runs, these stale CACHE entries cause the wrong library (and its wrong
    # .pc-derived include paths) to be used for the imported target.
    get_cmake_property(_all_cache_vars CACHE_VARIABLES)
    foreach(_cv IN LISTS _all_cache_vars)
        if(_cv MATCHES "^pkgcfg_lib_${var}_")
            unset(${_cv} CACHE)
        endif()
    endforeach()
    unset(_all_cache_vars)
    unset(_cv)

    if(_FP_PROBE_LIBRARY)
        unset(_FP_PROBE_LIB CACHE)
        find_library(_FP_PROBE_LIB ${_FP_PROBE_LIBRARY} HINTS ${OME_DEP_PREFIX}/lib ${OME_DEP_PREFIX}/lib64)
        if(NOT _FP_PROBE_LIB)
            set(_FP_PROBE_FOUND FALSE)
        endif()
    endif()

    if(_FP_PROBE_FOUND)
        pkg_check_modules(${var} QUIET IMPORTED_TARGET ${_FP_PKG_STRING})
        # Validate that all reported include directories actually exist.
        # A stale or mispackaged .pc file (e.g. wrong prefix=) can report
        # non-existent paths, which causes a CMake generate-time error.
        if(${var}_FOUND)
            foreach(_fp_inc IN LISTS ${var}_INCLUDE_DIRS)
                if(NOT EXISTS "${_fp_inc}")
                    message(STATUS "[OME] '${_FP_PKG_NAME}' found but include dir missing: ${_fp_inc} - treating as not found")
                    set(${var}_FOUND FALSE)
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if((NOT _FP_PROBE_FOUND) OR (NOT ${var}_FOUND))
        unset(_FP_EXIST_FOUND)
        unset(_FP_EXIST_VERSION)
        unset(_FP_EXIST_FOUND CACHE)
        unset(_FP_EXIST_VERSION CACHE)
        pkg_check_modules(_FP_EXIST QUIET ${_FP_PKG_NAME})
        if(_FP_EXIST_FOUND)
            set(_FP_FOUND_BUT_MISMATCH ON)
            set(_FP_FOUND_VERSION "${_FP_EXIST_VERSION}")
        endif()

        # Build hwaccel forwarding args for InstallPrerequisites.cmake
        set(_FP_HWACCEL_ARGS)
        if(OME_HWACCEL_NVIDIA)
            list(APPEND _FP_HWACCEL_ARGS -DOME_HWACCEL_NVIDIA=ON)
        endif()
        if(OME_HWACCEL_QSV)
            list(APPEND _FP_HWACCEL_ARGS -DOME_HWACCEL_QSV=ON)
        endif()
        if(OME_HWACCEL_XMA)
            list(APPEND _FP_HWACCEL_ARGS -DOME_HWACCEL_XMA=ON)
        endif()
        if(OME_HWACCEL_NILOGAN)
            list(APPEND _FP_HWACCEL_ARGS -DOME_HWACCEL_NILOGAN=ON)
            if(OME_NILOGAN_PATCH_PATH AND NOT OME_NILOGAN_PATCH_PATH STREQUAL "")
                list(APPEND _FP_HWACCEL_ARGS "-DOME_NILOGAN_PATCH_PATH=${OME_NILOGAN_PATCH_PATH}")
            endif()
            if(OME_NILOGAN_XCODER_COMPILE_PATH AND NOT OME_NILOGAN_XCODER_COMPILE_PATH STREQUAL "")
                list(APPEND _FP_HWACCEL_ARGS "-DOME_NILOGAN_XCODER_COMPILE_PATH=${OME_NILOGAN_XCODER_COMPILE_PATH}")
            endif()
        endif()
        if(OME_ENABLE_X264)
            list(APPEND _FP_HWACCEL_ARGS -DOME_ENABLE_X264=ON)
        else()
            list(APPEND _FP_HWACCEL_ARGS -DOME_ENABLE_X264=OFF)
        endif()
        if(OME_SKIP_DEPENDENCY_CHECK)
            # Auto-install suppressed - report only
        elseif(_FP_REINSTALL_TARGET)
            message(STATUS "[OME] '${_FP_PKG_STRING}' not found or wrong version - reinstalling '${_FP_REINSTALL_TARGET}' ...")
            execute_process(
                COMMAND ${CMAKE_COMMAND}
                    -DOME_DEP_PREFIX=${OME_DEP_PREFIX}
                    -DTARGET=${_FP_REINSTALL_TARGET}
                    ${_FP_HWACCEL_ARGS}
                    ${_FP_EXTRA_ARGS}
                    -P "${CMAKE_SOURCE_DIR}/cmake/InstallPrerequisites.cmake"
                RESULT_VARIABLE _install_result
            )
        elseif(NOT _FP_OPTIONAL)
            message(STATUS "[OME] '${_FP_PKG_STRING}' not found - running InstallPrerequisites.cmake ...")
            execute_process(
                COMMAND ${CMAKE_COMMAND}
                    -DOME_DEP_PREFIX=${OME_DEP_PREFIX}
                    ${_FP_HWACCEL_ARGS}
                    ${_FP_EXTRA_ARGS}
                    -P "${CMAKE_SOURCE_DIR}/cmake/InstallPrerequisites.cmake"
                RESULT_VARIABLE _install_result
            )
        endif()
        if(DEFINED _install_result AND NOT _install_result EQUAL 0)
            message(FATAL_ERROR "[OME] Install failed for '${_FP_PKG_STRING}' (exit ${_install_result}).\n"
                "  Run manually: cmake -P cmake/InstallPrerequisites.cmake")
        endif()
        if(NOT OME_SKIP_DEPENDENCY_CHECK)
            if(_FP_PROBE_LIBRARY)
                unset(_FP_PROBE_LIB CACHE)
                find_library(_FP_PROBE_LIB ${_FP_PROBE_LIBRARY} HINTS ${OME_DEP_PREFIX}/lib ${OME_DEP_PREFIX}/lib64)
                if(_FP_PROBE_LIB)
                    set(_FP_PROBE_FOUND TRUE)
                else()
                    set(_FP_PROBE_FOUND FALSE)
                endif()
            endif()

            if(_FP_PROBE_FOUND)
                pkg_check_modules(${var} QUIET IMPORTED_TARGET ${_FP_PKG_STRING})
                if(${var}_FOUND)
                    foreach(_fp_inc IN LISTS ${var}_INCLUDE_DIRS)
                        if(NOT EXISTS "${_fp_inc}")
                            message(STATUS "[OME] '${_FP_PKG_NAME}' found but include dir missing: ${_fp_inc} - treating as not found")
                            set(${var}_FOUND FALSE)
                            break()
                        endif()
                    endforeach()
                endif()
            endif()

            if(_FP_PROBE_FOUND AND ${var}_FOUND)
                set(_FP_FOUND_BUT_MISMATCH OFF)
                set(_FP_FOUND_VERSION "")
            else()
                if(_FP_PROBE_FOUND)
                    unset(_FP_EXIST_FOUND)
                    unset(_FP_EXIST_VERSION)
                    unset(_FP_EXIST_FOUND CACHE)
                    unset(_FP_EXIST_VERSION CACHE)
                    pkg_check_modules(_FP_EXIST QUIET ${_FP_PKG_NAME})
                    if(_FP_EXIST_FOUND)
                        set(_FP_FOUND_BUT_MISMATCH ON)
                        set(_FP_FOUND_VERSION "${_FP_EXIST_VERSION}")
                    else()
                        set(_FP_FOUND_BUT_MISMATCH OFF)
                        set(_FP_FOUND_VERSION "")
                    endif()
                else()
                    set(_FP_FOUND_BUT_MISMATCH OFF)
                    set(_FP_FOUND_VERSION "")
                endif()
            endif()
        endif()
    endif()

    if(${var}_FOUND)
        message(STATUS "${_FP_STATUS_PREFIX} - found ${${var}_VERSION}")
    elseif(_FP_FOUND_BUT_MISMATCH)
        set(_FP_FAIL_MESSAGE "${_FP_STATUS_PREFIX} - found ${_FP_FOUND_VERSION}, version mismatch")
        if(_FP_ON_MISMATCH STREQUAL "FATAL")
            message(FATAL_ERROR "${_FP_FAIL_MESSAGE}\n"
                "  Run manually: cmake -P cmake/InstallPrerequisites.cmake")
        endif()
        message(STATUS "${_FP_FAIL_MESSAGE}")
    elseif(_FP_ON_MISSING STREQUAL "FATAL")
        message(FATAL_ERROR "${_FP_STATUS_PREFIX} - not found\n"
            "  Run manually: cmake -P cmake/InstallPrerequisites.cmake")
    else()
        message(STATUS "${_FP_STATUS_PREFIX} - not found")
    endif()

    unset(_FP_OPTIONAL)
    unset(_FP_REINSTALL_TARGET)
    unset(_install_result)
    unset(_FP_PKG_NAME)
    unset(_FP_PKG_STRING)
    unset(_FP_FOUND_BUT_MISMATCH)
    unset(_FP_FOUND_VERSION)
    unset(_FP_FAIL_MESSAGE)
    unset(_FP_EXIST_FOUND)
    unset(_FP_EXIST_VERSION)
    unset(_FP_VERSION_MATCH)
    unset(_FP_REQUIRED_VERSION)
    unset(_FP_STATUS_PREFIX)
    unset(_FP_VERIFY_VERSION)
    unset(_FP_SOURCE_REF)
    unset(_FP_HAS_OVERRIDE)
    unset(_FP_VERSION_OP)
    unset(_FP_ON_MISSING)
    unset(_FP_ON_MISMATCH)
    unset(_FP_PROBE_LIBRARY)
    unset(_FP_PROBE_LIB)
    unset(_FP_PROBE_FOUND)
endmacro()

# ==============================================================================
# Required dependencies  (exact version required; wrong/newer version → targeted reinstall)
# ==============================================================================
ome_find_pkg(PKG_OPENSSL        openssl         OME_VER_OPENSSL         REINSTALL_TARGET openssl)
ome_find_pkg(PKG_SRT            srt             OME_VER_SRT             REINSTALL_TARGET libsrt)
ome_find_pkg(PKG_LIBSRTP2       libsrtp2        OME_VER_SRTP            REINSTALL_TARGET libsrtp)
ome_find_pkg(PKG_VPX            vpx             OME_VER_VPX             REINSTALL_TARGET libvpx)
ome_find_pkg(PKG_OPUS           opus            OME_VER_OPUS            REINSTALL_TARGET libopus)
ome_find_pkg(PKG_LIBPCRE2_8     libpcre2-8      OME_VER_PCRE2           REINSTALL_TARGET libpcre2)
ome_find_pkg(PKG_HIREDIS        hiredis         OME_VER_HIREDIS         REINSTALL_TARGET hiredis)
ome_find_pkg(PKG_SPDLOG         spdlog          OME_VER_SPDLOG          REINSTALL_TARGET spdlog)
# When OME_HWACCEL_NVIDIA is ON, whisper must be built with GGML_CUDA=ON.
# CUDA builds of whisper.cpp produce libggml-cuda.so in addition to libggml.so.
# Use that as a probe: if it is absent while NVIDIA is requested, reinstall.
if(OME_HWACCEL_NVIDIA)
    ome_find_pkg(PKG_WHISPER    whisper         OME_VER_WHISPER         REINSTALL_TARGET whisper PROBE_LIBRARY ggml-cuda)
else()
    ome_find_pkg(PKG_WHISPER    whisper         OME_VER_WHISPER         REINSTALL_TARGET whisper)
endif()
ome_find_pkg(PKG_LIBAVFORMAT    libavformat     OME_VER_LIBAVFORMAT     REINSTALL_TARGET ffmpeg)
ome_find_pkg(PKG_LIBAVFILTER    libavfilter     OME_VER_LIBAVFILTER     REINSTALL_TARGET ffmpeg)
ome_find_pkg(PKG_LIBAVCODEC     libavcodec      OME_VER_LIBAVCODEC      REINSTALL_TARGET ffmpeg)
ome_find_pkg(PKG_LIBSWRESAMPLE  libswresample   OME_VER_LIBSWRESAMPLE   REINSTALL_TARGET ffmpeg)
ome_find_pkg(PKG_LIBSWSCALE     libswscale      OME_VER_LIBSWSCALE      REINSTALL_TARGET ffmpeg)
ome_find_pkg(PKG_LIBAVUTIL      libavutil       OME_VER_LIBAVUTIL       REINSTALL_TARGET ffmpeg)

# ==============================================================================
# Optional / hardware-accelerated dependencies
# ==============================================================================

# NVIDIA CUDA/NVML 
if(OME_HWACCEL_NVIDIA)
    set(_CUDA_ROOT "/usr/local/cuda")
    find_library(_NV_CUDA_LIB   cuda       HINTS ${_CUDA_ROOT}/lib64 ${_CUDA_ROOT}/lib64/stubs /usr/lib/x86_64-linux-gnu)
    find_library(_NV_CUDART_LIB cudart     HINTS ${_CUDA_ROOT}/lib64 /usr/lib/x86_64-linux-gnu)
    find_library(_NV_ML_LIB     nvidia-ml  HINTS ${_CUDA_ROOT}/lib64 ${_CUDA_ROOT}/lib64/stubs /usr/lib/x86_64-linux-gnu)
    find_program(_NV_NVCC       nvcc       HINTS ${_CUDA_ROOT}/bin)

    if(_NV_CUDA_LIB AND _NV_CUDART_LIB AND _NV_ML_LIB AND _NV_NVCC)
        message(STATUS "[OME] NVIDIA hardware acceleration: ENABLED")
        add_compile_definitions(HWACCELS_NVIDIA_ENABLED)
        include_directories(${_CUDA_ROOT}/include)
        link_directories(${_CUDA_ROOT}/lib64 ${_CUDA_ROOT}/lib64/stubs)
        set(OME_NVIDIA_LIBS cuda cudart nvidia-ml)
    else()
        message(WARNING "[OME] OME_HWACCEL_NVIDIA=ON but required NVIDIA libraries/tools not found - disabling")
        set(OME_HWACCEL_NVIDIA OFF)
    endif()

    unset(_NV_CUDA_LIB CACHE)
    unset(_NV_CUDART_LIB CACHE)
    unset(_NV_ML_LIB CACHE)
    unset(_NV_NVCC CACHE)
    unset(_CUDA_ROOT)
endif()

# Intel QSV (Deprecated)
if(OME_HWACCEL_QSV)
    pkg_check_modules(PKG_LIBMFX IMPORTED_TARGET libmfx)
    if(PKG_LIBMFX_FOUND)
        message(STATUS "[OME] Intel QSV hardware acceleration: ENABLED")
        add_compile_definitions(HWACCELS_QSV_ENABLED)
    else()
        message(WARNING "[OME] OME_HWACCEL_QSV=ON but libmfx not found")
        set(OME_HWACCEL_QSV OFF)
    endif()
endif()

# Xilinx XMA
if(OME_HWACCEL_XMA)
    ome_find_pkg(PKG_LIBXMA2API libxma2api OPTIONAL)
    ome_find_pkg(PKG_XVBM       xvbm       OPTIONAL)
    ome_find_pkg(PKG_LIBXRM     libxrm     OPTIONAL)
    if(PKG_LIBXMA2API_FOUND AND PKG_LIBXRM_FOUND AND PKG_XVBM_FOUND)
        message(STATUS "[OME] Xilinx XMA hardware acceleration: ENABLED")
        add_compile_definitions(HWACCELS_XMA_ENABLED)
    else()
        message(WARNING "[OME] OME_HWACCEL_XMA=ON but libxma2api/libxrm not found")
        set(OME_HWACCEL_XMA OFF)
    endif()
endif()

# Netint NiLogan
if(OME_HWACCEL_NILOGAN)
    find_library(XCODER_LOGAN_LIB xcoder_logan)
    if(XCODER_LOGAN_LIB)
        message(STATUS "[OME] Netint NiLogan hardware acceleration: ENABLED")
        add_compile_definitions(HWACCELS_NILOGAN_ENABLED)
    else()
        message(WARNING "[OME] OME_HWACCEL_NILOGAN=ON but libxcoder_logan.so not found")
        set(OME_HWACCEL_NILOGAN OFF)
    endif()
endif()

# jemalloc - required for Release builds, optional for Debug (can be forced with OME_ENABLE_JEMALLOC=ON)
# Note: when built with --enable-prof, jemalloc reports its pkg-config version as "<ver>_0"
# (e.g. "5.3.0_0"), so we use >= instead of = to avoid a false version mismatch.
if(OME_ENABLE_JEMALLOC OR (CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT DEFINED OME_ENABLE_JEMALLOC))
    ome_find_pkg(PKG_JEMALLOC jemalloc OME_VER_JEMALLOC
        VERSION_OP >=
        REINSTALL_TARGET jemalloc
        EXTRA_ARGS -DENABLE_JEMALLOC_PROF=${OME_USE_JEMALLOC_PROFILE}
    )
    if(PKG_JEMALLOC_FOUND)
        message(STATUS "[OME] jemalloc: ENABLED")
        add_compile_definitions(OME_USE_JEMALLOC)

        # Profiling support - only meaningful when jemalloc is active
        if(OME_USE_JEMALLOC_PROFILE)
            add_compile_definitions(OME_USE_JEMALLOC_PROFILE)
            message(STATUS "[OME] jemalloc profiling: ENABLED")
        endif()
    endif()
else()
    if(OME_USE_JEMALLOC_PROFILE)
        message(FATAL_ERROR "[OME] OME_USE_JEMALLOC_PROFILE=ON requires OME_ENABLE_JEMALLOC=ON")
    endif()
endif()

# libx264 - FFmpeg uses this internally when built with --enable-libx264.
# Keep the legacy "probe and enable" flow:
#   1. Probe libx264 visibility with find_library().
#   2. If not visible yet, try installing it.
#   3. If still not visible, leave x264 encoder support disabled.
#   4. If visible, require the expected pkg-config version exactly.
if(OME_ENABLE_X264)
    ome_find_pkg(PKG_X264 x264 OME_VER_X264
        REINSTALL_TARGET libx264
        PROBE_LIBRARY x264
        ON_MISSING DISABLE
        ON_MISMATCH FATAL
    )
    if(PKG_X264_FOUND)
        message(STATUS "[OME] libx264: found (${PKG_X264_VERSION}) - enabling THIRDP_LIBX264_ENABLED")
        add_compile_definitions(THIRDP_LIBX264_ENABLED)
    else()
        message(STATUS "[OME] libx264: not found - x264 encoder disabled")
    endif()
else()
    message(STATUS "[OME] libx264: disabled by OME_ENABLE_X264=OFF")
endif()

# ==============================================================================
# Stubs (GPU stub .so files) are installed unconditionally, regardless of hardware support.
# ==============================================================================
if(NOT IS_DIRECTORY "${OME_DEP_PREFIX}/lib/stubs")
    message(STATUS "[OME] ${OME_DEP_PREFIX}/lib/stubs not found - installing stubs libraries ...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -DOME_DEP_PREFIX=${OME_DEP_PREFIX}
            -DTARGET=stubs
            -P "${CMAKE_SOURCE_DIR}/cmake/InstallPrerequisites.cmake"
        RESULT_VARIABLE _stubs_ret
    )
    if(NOT _stubs_ret EQUAL 0)
        message(FATAL_ERROR "[OME] Failed to install stubs.")
    endif()
    unset(_stubs_ret)
endif()


# uuid (system library, not pkg-config)
find_library(UUID_LIB uuid REQUIRED)
