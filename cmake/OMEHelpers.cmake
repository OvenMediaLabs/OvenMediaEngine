#
# OMEHelpers.cmake
# Helper macros and functions for building OvenMediaEngine static libraries.
#

include(CMakeParseArguments)

# ------------------------------------------------------------------------------
# ome_add_static_library(target_name
#     [SOURCES_DIRS  dir1 dir2 ...]   - non-recursive glob in subdirs
#     [SOURCES_DIRS_R dir1 dir2 ...]  - recursive glob in subdirs
#     [EXCLUDE  pat1 pat2 ...]        - filename patterns to exclude from globbed
#                                       sources (e.g. "*_test.cpp" "test_*.cpp")
#                                       Useful to keep unit-test files out of the
#                                       production library while living in the
#                                       same directory.
#     [DEPS  dep1 dep2 ...]           - Declared link dependencies (OME targets,
#                                       imported targets, or plain lib names).
#                                       NOTE: OvenMediaEngine is a monolithic binary
#                                       linked via $<LINK_GROUP:RESCAN,...>, so all
#                                       OME static libs are always linked regardless
#                                       of what is listed here.  DEPS does NOT
#                                       enforce actual linkage — it serves as
#                                       documentation of intentional dependencies
#                                       and provides CMake build-order hints.
#     [PUBLIC_DEPS dep1 dep2 ...]     - Same as DEPS but propagates to consumers
#                                       via target_link_libraries PUBLIC scope.
# )
#
# Globs all *.cpp / *.c from CMAKE_CURRENT_SOURCE_DIR (non-recursive) plus any
# listed subdirectories, applies EXCLUDE filters, creates a STATIC library,
# and wires up global includes, compile options, and link dependencies.
# ------------------------------------------------------------------------------
function(ome_add_static_library target_name)
    cmake_parse_arguments(ARG
        ""
        ""
        "SOURCES_DIRS;SOURCES_DIRS_R;EXCLUDE;DEPS;PUBLIC_DEPS"
        ${ARGN}
    )

    # ── Source collection ─────────────────────────────────────────────────────

    # Root directory (non-recursive)
    file(GLOB SELF_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    )
    set(SRCS ${SELF_SRCS})

    # Named subdirectories - non-recursive glob
    foreach(dir ${ARG_SOURCES_DIRS})
        file(GLOB dir_srcs
            "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.c"
        )
        list(APPEND SRCS ${dir_srcs})
    endforeach()

    # Named subdirectories - recursive glob
    foreach(dir ${ARG_SOURCES_DIRS_R})
        file(GLOB_RECURSE dir_srcs
            "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.c"
        )
        list(APPEND SRCS ${dir_srcs})
    endforeach()

    # ── Exclusion filter ──────────────────────────────────────────────────────
    # Always exclude *_test.cpp files from production library sources.
    # Test files live alongside source (co-location pattern) but are compiled
    # only by the test targets defined inside each module's CMakeLists.txt.
    list(APPEND ARG_EXCLUDE "*_test.cpp")

    # Convert each glob-style pattern (filename only) to a regex and remove
    # matching files from the list.  Patterns are matched against the filename
    # component only (not the full path), so "test_*.cpp" or "*_test.cpp" work
    # as expected without needing to know the full path.
    foreach(pat ${ARG_EXCLUDE})
        # Escape regex metacharacters, then turn glob wildcards into .*
        string(REGEX REPLACE "([][+.^${}()|\\\\])" "\\\\\\1" pat_re "${pat}")
        string(REPLACE "*" ".*" pat_re "${pat_re}")
        string(REPLACE "?" "." pat_re "${pat_re}")
        # Match against the filename part only
        list(FILTER SRCS EXCLUDE REGEX "/${pat_re}$")
    endforeach()

    # ── Library creation ──────────────────────────────────────────────────────

    if(NOT SRCS)
        message(STATUS "[OME] No source files found for '${target_name}' in ${CMAKE_CURRENT_SOURCE_DIR} - creating INTERFACE library")
        add_library(${target_name} INTERFACE)
        target_include_directories(${target_name} INTERFACE
            ${OME_GLOBAL_INCLUDE_DIRS}
        )
        set_target_properties(${target_name} PROPERTIES OME_HEADER_ONLY TRUE)
        # Link INTERFACE deps (PUBLIC_DEPS propagate; DEPS become INTERFACE here)
        if(ARG_DEPS OR ARG_PUBLIC_DEPS)
            target_link_libraries(${target_name} INTERFACE
                ${ARG_DEPS} ${ARG_PUBLIC_DEPS}
            )
        endif()
        return()
    endif()

    add_library(${target_name} STATIC ${SRCS})

    target_include_directories(${target_name} PUBLIC
        ${OME_GLOBAL_INCLUDE_DIRS}
    )

    target_compile_options(${target_name} PRIVATE
        ${OME_GLOBAL_CFLAGS}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_17)

    # ── Link dependencies ─────────────────────────────────────────────────────
    if(ARG_DEPS)
        target_link_libraries(${target_name} PRIVATE ${ARG_DEPS})
    endif()
    if(ARG_PUBLIC_DEPS)
        target_link_libraries(${target_name} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()

    # Auto-register so main collects all OME static libs automatically.
    set_property(GLOBAL APPEND PROPERTY OME_STATIC_LIBS ${target_name})
endfunction()


# ------------------------------------------------------------------------------
# ome_target_pkg_config(target scope pkg1 pkg2 ...)
# Links pkg-config modules to a target.
# scope: PRIVATE | PUBLIC | INTERFACE
# Handles INTERFACE (header-only) targets transparently.
# ------------------------------------------------------------------------------
function(ome_target_pkg_config target scope)
    # Determine effective scope for INTERFACE libraries
    get_target_property(_is_iface ${target} OME_HEADER_ONLY)
    if(_is_iface)
        set(scope INTERFACE)
    endif()

    foreach(pkg ${ARGN})
        string(MAKE_C_IDENTIFIER "${pkg}" pkg_id)
        string(TOUPPER "${pkg_id}" pkg_var)
        set(var "PKG_${pkg_var}")

        if(NOT TARGET PkgConfig::${var})
            pkg_check_modules(${var} REQUIRED IMPORTED_TARGET ${pkg})
        endif()

        target_link_libraries(${target} ${scope} PkgConfig::${var})
    endforeach()
endfunction()

# ------------------------------------------------------------------------------
# ome_add_tests(target
#     SRCS      file1 file2 ...  # absolute paths to *_test.cpp files to compile
#     EXT_LIBS  lib1 lib2 ...   # external/pkg-config/system libs
# )
#
# Creates a per-module test binary named <target> and registers it with ctest.
# A ctest label is automatically derived from the target name by stripping the
# leading "ome_test_" / "ome_tests_" prefix (e.g. ome_test_ovlibrary → ovlibrary).
# This allows filtering tests by module:
#
#   ctest --test-dir build/Debug -L ovlibrary
#
# Typical usage inside a module's CMakeLists.txt:
#
#   if(OME_BUILD_TESTS)
#       file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
#       ome_add_tests(ome_test_mymodule
#           SRCS ${_srcs}
#           EXT_LIBS pthread)
#   endif()
#
# Target creation is DEFERRED to the end of the root CMakeLists.txt so that
# OME_STATIC_LIBS is fully populated before LINK_GROUP:RESCAN is evaluated.
# ------------------------------------------------------------------------------
function(ome_add_tests target)
    cmake_parse_arguments(ARG "" "" "SRCS;EXT_LIBS" ${ARGN})

    if(NOT ARG_SRCS)
        message(STATUS "[OME Test] No source files given for '${target}' - skipping")
        return()
    endif()

    set_property(GLOBAL APPEND PROPERTY OME_PENDING_TESTS "${target}")
    foreach(_src ${ARG_SRCS})
        set_property(GLOBAL APPEND PROPERTY "OME_TEST_SRCS__${target}" "${_src}")
    endforeach()
    foreach(_lib ${ARG_EXT_LIBS})
        set_property(GLOBAL APPEND PROPERTY "OME_TEST_EXTLIBS__${target}" "${_lib}")
    endforeach()

    # Schedule creation exactly once.
    get_property(_scheduled GLOBAL PROPERTY OME_TESTS_SCHEDULED)
    if(NOT _scheduled)
        set_property(GLOBAL PROPERTY OME_TESTS_SCHEDULED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
            CALL _ome_create_all_registered_tests)
    endif()
endfunction()

# Internal: called exactly once, deferred to after all src/ subdirs are done.
function(_ome_create_all_registered_tests)
    get_property(_targets GLOBAL PROPERTY OME_PENDING_TESTS)
    foreach(_target ${_targets})
        if(NOT TARGET ${_target})
            _ome_materialize_test("${_target}")
        endif()
    endforeach()

    if(_targets)
        add_custom_target(tests DEPENDS ${_targets})
    endif()
endfunction()

# Internal: creates one test executable target with a ctest label.
function(_ome_materialize_test target)
    get_property(_srcs    GLOBAL PROPERTY "OME_TEST_SRCS__${target}")
    get_property(_extlibs GLOBAL PROPERTY "OME_TEST_EXTLIBS__${target}")

    if(NOT _srcs)
        message(WARNING "[OME Test] No SRCS for '${target}' - skipping")
        return()
    endif()

    # Prepend common pool so every test binary gets the same base set of
    # external libraries without each module having to repeat them.
    get_property(_common_libs GLOBAL PROPERTY OME_COMMON_TEST_EXT_LIBS)
    if(_common_libs)
        list(PREPEND _extlibs ${_common_libs})
    endif()

    # Deduplicate (multiple modules registering same target may repeat libs).
    if(_extlibs)
        list(REMOVE_DUPLICATES _extlibs)
    endif()

    # Derive label: strip leading "ome_test_" or "ome_tests_" prefix.
    string(REGEX REPLACE "^ome_tests?_" "" _label "${target}")

    add_executable(${target} ${_srcs})

    target_include_directories(${target} PRIVATE ${OME_GLOBAL_INCLUDE_DIRS})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE ${OME_GLOBAL_CFLAGS})
    target_compile_definitions(${target} PRIVATE
        __STDC_CONSTANT_MACROS
        SPDLOG_COMPILED_LIB
    )

    get_property(_all_libs GLOBAL PROPERTY OME_STATIC_LIBS)
    set(_real_libs)
    foreach(_lib ${_all_libs})
        get_target_property(_is_iface ${_lib} OME_HEADER_ONLY)
        if(NOT _is_iface)
            list(APPEND _real_libs ${_lib})
        endif()
    endforeach()

    if(_real_libs)
        if(APPLE)
            target_link_libraries(${target} PRIVATE
                -Wl,-all_load
                ${_real_libs}
            )
        else()
            target_link_libraries(${target} PRIVATE
                "$<LINK_GROUP:RESCAN,${_real_libs}>"
            )
        endif()
    endif()

    target_link_libraries(${target} PRIVATE
        GTest::gtest_main
        GTest::gmock
        ${_extlibs}
    )

    # macOS Gatekeeper scans newly built binaries on first run, which can exceed
    # the default 5-second discovery timeout. Use 30s on macOS only.
    if(APPLE)
        set(_discovery_timeout 30)
    else()
        set(_discovery_timeout 5)
    endif()
    gtest_discover_tests(${target}
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        PROPERTIES TIMEOUT 60 LABELS "${_label}"
        DISCOVERY_TIMEOUT ${_discovery_timeout}
    )
endfunction()
