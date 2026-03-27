#
# CompilerOptions.cmake
# Global compiler flags mirroring those in the AMS.mk build system.
#

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------------------
# Detect Alpine Linux (needs larger stack)
# ------------------------------------------------------------------------------
set(EXTRA_FLAGS "")
if(EXISTS /etc/os-release)
    file(READ /etc/os-release OS_RELEASE_CONTENT)
    if(OS_RELEASE_CONTENT MATCHES "ID=alpine")
        message(STATUS "[OME] Alpine Linux detected - setting stack size to 1 MB")
        set(EXTRA_FLAGS "-Wl,-z,stack-size=1048576")
    endif()
endif()

# Detect Alpine also via OSTYPE (musl libc)
execute_process(
    COMMAND bash -c "echo $OSTYPE"
    OUTPUT_VARIABLE OSTYPE_VALUE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# ------------------------------------------------------------------------------
# Common flags (mirroring GLOBAL_C/CXXFLAGS_COMMON)
# ------------------------------------------------------------------------------
set(OME_COMMON_FLAGS
    -Wall
    -Wformat-security
    -Wno-attributes
    ${EXTRA_FLAGS}
)

# Enable colored diagnostics only when the terminal supports it
if((DEFINED ENV{COLORTERM}) OR (DEFINED ENV{TERM} AND NOT "$ENV{TERM}" STREQUAL "dumb"))
    list(APPEND OME_COMMON_FLAGS -fdiagnostics-color=always)
endif()

set(OME_CFLAGS_COMMON   ${OME_COMMON_FLAGS})
set(OME_CXXFLAGS_COMMON ${OME_COMMON_FLAGS})

# Project-level flags (from projects/AMS.mk)
set(OME_PROJECT_CFLAGS
    -D__STDC_CONSTANT_MACROS
    -Wfatal-errors
    -Wno-unused-function
)

set(OME_PROJECT_CXXFLAGS
    ${OME_PROJECT_CFLAGS}
    -DSPDLOG_COMPILED_LIB
)

# Combined globals exposed to all targets
set(OME_GLOBAL_CFLAGS
    ${OME_CFLAGS_COMMON}
    ${OME_PROJECT_CFLAGS}
)

set(OME_GLOBAL_CXXFLAGS
    ${OME_CXXFLAGS_COMMON}
    ${OME_PROJECT_CXXFLAGS}
)

# ------------------------------------------------------------------------------
# Per-build-type flags
# ------------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(${OME_GLOBAL_CXXFLAGS} -g -O3)
    add_link_options(-Wl,--export-dynamic -O3)
    message(STATUS "[OME] Release build: -O3 optimizations enabled")
else()
    # Debug (default)
    add_compile_options(${OME_GLOBAL_CXXFLAGS} -g -DDEBUG -D_DEBUG)
    add_link_options(-Wl,--export-dynamic)
    message(STATUS "[OME] Debug build")
    if(OME_SANITIZE_THREAD)
        add_compile_options(-fsanitize=thread)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # GCC requires linking -ltsan explicitly
            add_link_options(-fsanitize=thread -ltsan)
        else()
            # Clang links TSan runtime automatically via -fsanitize=thread
            add_link_options(-fsanitize=thread)
        endif()
        message(STATUS "[OME] ThreadSanitizer (TSan): ENABLED (${CMAKE_CXX_COMPILER_ID})")
    endif()
endif()

# Alpine musl extra libs
if(OSTYPE_VALUE STREQUAL "linux-musl")
    link_libraries(-lexecinfo)
endif()

# ------------------------------------------------------------------------------
# RPATH setup - mirror CONFIG_LIBRARY_PATHS
# ------------------------------------------------------------------------------
set(OME_LIB_PATHS ${OME_DEP_PREFIX}/lib ${OME_DEP_PREFIX}/lib64)

set(CMAKE_INSTALL_RPATH ${OME_LIB_PATHS})
set(CMAKE_BUILD_RPATH   ${OME_LIB_PATHS})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# pkg-config search paths
set(ENV{PKG_CONFIG_PATH}
    "${OME_DEP_PREFIX}/lib/pkgconfig:${OME_DEP_PREFIX}/lib64/pkgconfig:$ENV{PKG_CONFIG_PATH}"
)

# ------------------------------------------------------------------------------
# Global include directories (exposed to all targets via OME_GLOBAL_INCLUDE_DIRS)
# This is set after src/projects is known (done in src/projects/CMakeLists.txt).
# ------------------------------------------------------------------------------
set(OME_GLOBAL_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/src/projects"
    "${CMAKE_SOURCE_DIR}/src/projects/third_party"
    "${CMAKE_SOURCE_DIR}/src/projects/third_party/spdlog-1.15.1/include"
    "${OME_DEP_PREFIX}/include"
)
