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
if(APPLE)
    set(OME_DEP_PREFIX "$ENV{HOME}/opt/ovenmediaengine" CACHE PATH "Installation prefix for external dependencies")
else()
    set(OME_DEP_PREFIX "/opt/ovenmediaengine" CACHE PATH "Installation prefix for external dependencies")
endif()

option(OME_HWACCEL_NVIDIA           "Enable NVIDIA GPU hardware acceleration"            OFF)
option(OME_HWACCEL_QSV              "Enable Intel QSV hardware acceleration"             OFF)
option(OME_HWACCEL_XMA              "Enable Xilinx XMA hardware acceleration"            OFF)
option(OME_HWACCEL_NILOGAN          "Enable Netint NiLogan acceleration"                 OFF)
set(OME_NILOGAN_PATCH_PATH          "" CACHE PATH   "Path to Netint NiLogan FFmpeg patch file (required when OME_HWACCEL_NILOGAN=ON)")
set(OME_NILOGAN_XCODER_COMPILE_PATH "" CACHE PATH   "Path to xcoder_logan source directory to compile (optional)")
option(OME_ENABLE_X264              "Enable libx264 encoder support"                     ON)
option(OME_ENABLE_JEMALLOC          "Enable jemalloc memory allocator"                  OFF)
option(OME_BUILD_STUBS              "Build GPU stub libraries"                           OFF)
option(OME_SKIP_DEPENDENCY_CHECK    "Skip auto-install of missing/wrong-version packages" OFF)
option(OME_SANITIZE_THREAD          "Enable ThreadSanitizer (TSan) - Debug only"         OFF)
option(OME_BUILD_TESTS              "Build unit tests (requires GTest)"                  OFF)

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
        pthread dl z stdc++fs
    )
endif()

add_subdirectory(src)

# Auto-enable stubs when GPU acceleration is active
if(OME_HWACCEL_NVIDIA OR OME_HWACCEL_XMA)
    set(OME_BUILD_STUBS ON)
endif()

if(OME_BUILD_STUBS)
    add_subdirectory(misc/stubs)
endif()

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
