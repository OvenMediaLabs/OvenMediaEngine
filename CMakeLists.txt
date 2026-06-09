cmake_minimum_required(VERSION 3.24)

# ==============================================================================
# Compiler selection - must be set BEFORE project() so CMake detects the
# right compiler ABI. Override with -DOME_USE_CLANG=OFF to use the system default.
# ==============================================================================
option(OME_USE_CLANG "Use Clang/Clang++ instead of the system default compiler" ON)
if(OME_USE_CLANG)
    find_program(_OME_CLANG   NAMES clang)
    find_program(_OME_CLANGXX NAMES clang++)
    if(_OME_CLANG AND _OME_CLANGXX)
        set(CMAKE_C_COMPILER   "${_OME_CLANG}"   CACHE STRING "C compiler"   FORCE)
        set(CMAKE_CXX_COMPILER "${_OME_CLANGXX}" CACHE STRING "C++ compiler" FORCE)
    else()
        message(WARNING "[OME] OME_USE_CLANG=ON but clang/clang++ not found - falling back to default compiler")
    endif()
endif()

project(OvenMediaEngine
    DESCRIPTION "OvenMediaEngine - Open-Source Streaming Server"
    LANGUAGES C CXX
)

# ==============================================================================
# Toolchain sanity check
# ------------------------------------------------------------------------------
# Older libstdc++ has a wait_until() overflow bug (GCC PR 91906): predicate-based
# wait_until() on a non-system_clock time_point converts via system_clock and
# overflows for ::time_point::max(), causing immediate timeout and busy-spin
# instead of waiting. The fix uses pthread_cond_clockwait() and is gated by the
# _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT macro, defined when libstdc++ is built
# against glibc >= 2.30 (present in libstdc++ 10+).
#
# Include a public standard header so libc++ environments (no <bits/...>) pass
# through this check naturally: __GLIBCXX__ stays undefined and the guard is a
# no-op.
# ==============================================================================
include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
#include <cstddef>
#if defined(__GLIBCXX__) && !defined(_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT)
#  error \"libstdc++ without pthread_cond_clockwait support\"
#endif
int main() {}
" OME_HAS_SAFE_CXX_STDLIB)

if(NOT OME_HAS_SAFE_CXX_STDLIB)
    message(FATAL_ERROR
        "\n"
        "Your libstdc++ does not enable pthread_cond_clockwait(), which means\n"
        "predicate-based wait_until() on steady_clock busy-spins instead of\n"
        "waiting (GCC PR 91906). This affects libstdc++ 9 and older, or any\n"
        "libstdc++ built against glibc < 2.30.\n"
        "\n"
        "Supported out of the box:\n"
        "  - Ubuntu 20.04.4+ / 22.04+\n"
        "  - Debian 11+ (with up-to-date libstdc++)\n"
        "  - RHEL / Rocky / AlmaLinux 9+\n"
        "  - Fedora 35+\n"
        "  - Amazon Linux 2023\n"
        "\n"
        "For other systems, use the official Docker image:\n"
        "  docker pull airensoft/ovenmediaengine:latest\n"
    )
endif()

# ==============================================================================
# CMake settings
# ==============================================================================
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
    message(STATUS "[OME] TIP: Use -G Ninja for faster builds (cmake -B build/Debug -G Ninja ...)")
endif()
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ==============================================================================
# Options
# ==============================================================================
# Build type - normalize to canonical case (Debug/Release) so comparisons work
# regardless of how the user typed it (debug, DEBUG, release, RELEASE, etc.)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug/Release)" FORCE)
else()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _BUILD_TYPE_UPPER)
    if(_BUILD_TYPE_UPPER STREQUAL "DEBUG")
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug/Release)" FORCE)
    elseif(_BUILD_TYPE_UPPER STREQUAL "RELEASE")
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug/Release)" FORCE)
    else()
        message(FATAL_ERROR "Unsupported build type: '${CMAKE_BUILD_TYPE}'. Use Debug or Release.")
    endif()
    unset(_BUILD_TYPE_UPPER)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

set(OME_DEP_PREFIX "/opt/ovenmediaengine" CACHE PATH "Installation prefix for external dependencies")

option(OME_HWACCEL_NVIDIA           "Enable NVIDIA GPU hardware acceleration"            OFF)
option(OME_HWACCEL_QSV              "Enable Intel QSV hardware acceleration"             OFF)
option(OME_HWACCEL_XMA              "Enable Xilinx XMA hardware acceleration"            OFF)
option(OME_HWACCEL_NILOGAN          "Enable Netint NiLogan acceleration"                 OFF)
set(OME_NILOGAN_PATCH_PATH          "" CACHE PATH   "Path to Netint NiLogan FFmpeg patch file (required when OME_HWACCEL_NILOGAN=ON)")
set(OME_NILOGAN_XCODER_COMPILE_PATH "" CACHE PATH   "Path to xcoder_logan source directory to compile (optional)")
option(OME_ENABLE_X264              "Enable libx264 encoder support"                     ON)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    option(OME_ENABLE_JEMALLOC      "Enable jemalloc memory allocator"                   ON)
else()
    option(OME_ENABLE_JEMALLOC      "Enable jemalloc memory allocator"                   OFF)
endif()
option(OME_ENABLE_JEMALLOC_LG_PAGE_MAX "Build jemalloc with 16 KiB maximum page size on aarch64" OFF)
option(OME_SKIP_DEPENDENCY_CHECK    "Skip auto-install of missing/wrong-version packages" OFF)
option(OME_SANITIZE_THREAD          "Enable ThreadSanitizer (TSan) - Debug only"         OFF)
option(OME_THREAD_SAFETY            "Enable Clang thread-safety analysis"                OFF)
option(OME_BUILD_TESTS              "Build unit tests (requires GTest)"                  OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    option(OME_WHISPER_STATIC       "Build Whisper/ggml as a static library"             ON)
else()
    option(OME_WHISPER_STATIC       "Build Whisper/ggml as a static library"             OFF)
endif()

message(STATUS "Options:")
message(STATUS "  OME_USE_CLANG: ${OME_USE_CLANG}")
message(STATUS "  OME_DEP_PREFIX: ${OME_DEP_PREFIX}")
message(STATUS "  OME_HWACCEL_NVIDIA: ${OME_HWACCEL_NVIDIA}")
message(STATUS "  OME_HWACCEL_QSV: ${OME_HWACCEL_QSV}")
message(STATUS "  OME_HWACCEL_XMA: ${OME_HWACCEL_XMA}")
message(STATUS "  OME_HWACCEL_NILOGAN: ${OME_HWACCEL_NILOGAN}")
message(STATUS "  OME_NILOGAN_PATCH_PATH: ${OME_NILOGAN_PATCH_PATH}")
message(STATUS "  OME_NILOGAN_XCODER_COMPILE_PATH: ${OME_NILOGAN_XCODER_COMPILE_PATH}")
message(STATUS "  OME_ENABLE_X264: ${OME_ENABLE_X264}")
message(STATUS "  OME_ENABLE_JEMALLOC: ${OME_ENABLE_JEMALLOC}")
message(STATUS "  OME_ENABLE_JEMALLOC_LG_PAGE_MAX: ${OME_ENABLE_JEMALLOC_LG_PAGE_MAX}")
message(STATUS "  OME_SKIP_DEPENDENCY_CHECK: ${OME_SKIP_DEPENDENCY_CHECK}")
message(STATUS "  OME_SANITIZE_THREAD: ${OME_SANITIZE_THREAD}")
message(STATUS "  OME_THREAD_SAFETY: ${OME_THREAD_SAFETY}")
message(STATUS "  OME_BUILD_TESTS: ${OME_BUILD_TESTS}")
message(STATUS "  OME_WHISPER_STATIC: ${OME_WHISPER_STATIC}")

# ==============================================================================
# Include cmake modules
# ==============================================================================
include(GNUInstallDirs)
include(OMEHelpers)
include(CompilerOptions)
include(Dependencies)

# ==============================================================================
# Subdirectories
# ==============================================================================

# ---------------------------------------------------------------------------
# GTest / GMock setup — must come BEFORE add_subdirectory(src) so that the
# GTest::gtest_main / GTest::gmock targets are available when source-level
# CMakeLists.txt files define their own test executables.
# ---------------------------------------------------------------------------
if(OME_BUILD_TESTS)
    enable_testing()
    include(GoogleTest)
    include(FetchContent)
    # Always fetch a pinned version — do not use the system GTest so that the
    # version is consistent across all build environments.
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Common external libraries linked into every test binary automatically.
    # Mirrors the system/pkg-config libs in src/projects/main/CMakeLists.txt.
    # Individual modules do NOT need to specify EXT_LIBS unless they need extras.
    set_property(GLOBAL PROPERTY OME_COMMON_TEST_EXT_LIBS
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
    )

    # Mirror the optional hardware-acceleration libs that
    # src/projects/main/CMakeLists.txt links into the main executable so that
    # test binaries linking the same static archives (e.g. libtranscoder.a)
    # can resolve CUDA / XMA / NiLogan symbols.
    if(OME_HWACCEL_NVIDIA)
        set_property(GLOBAL APPEND PROPERTY OME_COMMON_TEST_EXT_LIBS
            ${OME_NVIDIA_LIBS}
            PkgConfig::PKG_FFNVCODEC
        )
    endif()
    if(OME_HWACCEL_XMA AND PKG_LIBXMA2API_FOUND)
        set_property(GLOBAL APPEND PROPERTY OME_COMMON_TEST_EXT_LIBS
            PkgConfig::PKG_LIBXMA2API
            PkgConfig::PKG_XVBM
            PkgConfig::PKG_LIBXRM
        )
    endif()
    if(OME_HWACCEL_NILOGAN AND XCODER_LOGAN_LIB)
        set_property(GLOBAL APPEND PROPERTY OME_COMMON_TEST_EXT_LIBS xcoder_logan)
    endif()

    # System libraries must come last so that any static archive listed earlier
    # (e.g. libcudart_static.a from OME_NVIDIA_LIBS) can resolve its references
    # to pthread / dl / rt against them. Placing them last also keeps
    # list(REMOVE_DUPLICATES) in ome_add_tests() from dropping a trailing copy.
    set_property(GLOBAL APPEND PROPERTY OME_COMMON_TEST_EXT_LIBS
        pthread dl z stdc++fs
    )
endif()

add_subdirectory(src)


# ==============================================================================
# Install rules
# ==============================================================================
set(OME_INSTALL_DIR /usr/share/ovenmediaengine)

# Strip symbols on install for Release builds only (mirrors 'install -s' in main.mk)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INSTALL_DO_STRIP TRUE)
endif()

install(TARGETS OvenMediaEngine
    RUNTIME DESTINATION ${OME_INSTALL_DIR}
)
install(SCRIPT ${CMAKE_SOURCE_DIR}/cmake/InstallHelper.cmake)
