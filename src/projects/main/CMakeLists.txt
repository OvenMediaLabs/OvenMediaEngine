#
# src/projects/main/CMakeLists.txt
# Builds the final OvenMediaEngine executable.
# Mirrors the role of projects/main/AMS.mk.
#

# ome_git_info target is defined in the parent CMakeLists.txt (projects/)
# and runs before all subdirectories are processed.

# ==============================================================================
# Executable sources
# ==============================================================================
file(GLOB MAIN_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_executable(OvenMediaEngine ${MAIN_SRCS})
add_dependencies(OvenMediaEngine ome_git_info)

# make generated header discoverable
target_include_directories(OvenMediaEngine PRIVATE
    ${OME_GLOBAL_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_features(OvenMediaEngine PRIVATE cxx_std_17)
target_compile_options(OvenMediaEngine PRIVATE ${OME_GLOBAL_CFLAGS})
target_compile_definitions(OvenMediaEngine PRIVATE
    SPDLOG_COMPILED_LIB
)

# ==============================================================================
# Link all static libraries
# All OME static targets are auto-registered in the OME_STATIC_LIBS global
# property by ome_add_static_library() (see cmake/OMEHelpers.cmake).
# LINK_GROUP:RESCAN wraps the whole list in --start-group/--end-group so
# circular / forward references between static archives resolve correctly.
# No manual maintenance needed when new modules are added.
# ==============================================================================
get_property(_ome_all_libs GLOBAL PROPERTY OME_STATIC_LIBS)
target_link_libraries(OvenMediaEngine PRIVATE
    "$<LINK_GROUP:RESCAN,${_ome_all_libs}>"
)

# ==============================================================================
# System / pkg-config libraries  (mirrors add_pkg_config and LOCAL_LDFLAGS)
# ==============================================================================
target_link_libraries(OvenMediaEngine PRIVATE
    PkgConfig::PKG_SRT
    PkgConfig::PKG_LIBAVFORMAT
    PkgConfig::PKG_LIBAVFILTER
    PkgConfig::PKG_LIBAVCODEC
    PkgConfig::PKG_LIBSWRESAMPLE
    PkgConfig::PKG_LIBSWSCALE
    PkgConfig::PKG_LIBAVUTIL
    PkgConfig::PKG_OPENSSL
    PkgConfig::PKG_VPX
    PkgConfig::PKG_OPUS
    PkgConfig::PKG_LIBSRTP2
    PkgConfig::PKG_LIBPCRE2_8
    PkgConfig::PKG_HIREDIS
    PkgConfig::PKG_SPDLOG
    PkgConfig::PKG_WHISPER
    ${UUID_LIB}
    pthread
    dl
    z
    stdc++fs
)

# ==============================================================================
# Optional hardware acceleration libs
# ==============================================================================
if(OME_HWACCEL_NVIDIA AND PKG_FFNVCODEC_FOUND)
    target_link_libraries(OvenMediaEngine PRIVATE
        PkgConfig::PKG_FFNVCODEC
        ${OME_NVIDIA_LIBS}
    )
endif()

if(OME_HWACCEL_XMA AND PKG_LIBXMA2API_FOUND)
    target_link_libraries(OvenMediaEngine PRIVATE
        PkgConfig::PKG_LIBXMA2API
        PkgConfig::PKG_XVBM
        PkgConfig::PKG_LIBXRM
    )
endif()

if(OME_HWACCEL_NILOGAN AND XCODER_LOGAN_LIB)
    target_link_libraries(OvenMediaEngine PRIVATE xcoder_logan)
endif()

if(PKG_JEMALLOC_FOUND)
    target_link_libraries(OvenMediaEngine PRIVATE PkgConfig::PKG_JEMALLOC)
endif()

# Alpine musl
set(_OSTYPE $ENV{OSTYPE})
if(_OSTYPE STREQUAL "linux-musl")
    target_link_libraries(OvenMediaEngine PRIVATE execinfo)
endif()

# ==============================================================================
# Install
# ==============================================================================
set(OME_INSTALL_DIR /usr/share/ovenmediaengine)
install(TARGETS OvenMediaEngine RUNTIME DESTINATION ${OME_INSTALL_DIR})
