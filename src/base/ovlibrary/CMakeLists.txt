ome_add_static_library(ovlibrary
    SOURCES_DIRS
        logger
        logger/formatters
        logger/pattern_flags
)

if(OME_BUILD_TESTS)
    # Pick up `*_test.cpp` from this directory plus the `tsa/`
    # sub-directory so that thread-safety-analysis wrapper tests live
    # next to the header they cover.
    #
    # `tsa/mutex_negative_test.cpp` is excluded from the GTest binary:
    # it is compiled separately under `-Xclang -verify` so that Clang
    # treats each `// expected-warning {{...}}` marker as the test
    # assertion. Including it in the GTest binary would either fail
    # the link (no GTest main usage) or fail the compile (the markers
    # only resolve under `-verify`).
    file(GLOB _srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tsa/*_test.cpp"
    )
    list(REMOVE_ITEM _srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/tsa/mutex_negative_test.cpp"
    )
    ome_add_tests(ome_test_base
        SRCS ${_srcs}
    )

    # Compile-time TSA regression guard: compile `tsa/mutex_negative_test.cpp`
    # under Clang `-Xclang -verify` (see that file's header for how the verify
    # markers work). Gated on OME_THREAD_SAFETY=ON + Clang + tests enabled; in
    # OFF or non-Clang builds the annotations expand to nothing and cannot fire.
    # `-fsyntax-only`, so nothing is linked or run - only the verify pass matters.
    if(OME_THREAD_SAFETY AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(_neg_src "${CMAKE_CURRENT_SOURCE_DIR}/tsa/mutex_negative_test.cpp")
        set(_neg_stamp "${CMAKE_CURRENT_BINARY_DIR}/tsa_negative_verify.stamp")

        # The verify target is intentionally a standalone `-fsyntax-only`
        # invocation rather than an ome_add_tests() target: `-Xclang -verify`
        # is a Clang-specific compile mode, not a runnable test binary, so it
        # cannot go through the normal test path. `mutex_negative_test.cpp` is
        # self-contained (it includes only `mutex.h` and the std headers), so
        # the build settings it needs from the common path are just the global
        # include dirs and the C++ standard, both sourced from the same
        # variables the rest of the build uses (`OME_GLOBAL_INCLUDE_DIRS`,
        # `CMAKE_CXX_STANDARD`) so they cannot drift.
        set(_neg_includes "")
        foreach(_inc ${OME_GLOBAL_INCLUDE_DIRS})
            list(APPEND _neg_includes "-I${_inc}")
        endforeach()

        # Mirror the global C++ standard, but fall back to 17 if it is somehow
        # unset (e.g. driven via target features instead of the global variable)
        # so the hardcoded flag can never degrade to a bare `-std=c++`.
        if(CMAKE_CXX_STANDARD)
            set(_neg_cxx_standard ${CMAKE_CXX_STANDARD})
        else()
            set(_neg_cxx_standard 17)
        endif()

        # Single source of truth for the verify command, shared by the build
        # target and the ctest entry below so the two cannot drift apart.
        set(_neg_verify_command
            ${CMAKE_CXX_COMPILER}
            -std=c++${_neg_cxx_standard}
            -DOME_THREAD_SAFETY
            -Wthread-safety
            -Xclang -verify
            -fsyntax-only
            ${_neg_includes}
            "${_neg_src}"
        )

        add_custom_command(
            OUTPUT "${_neg_stamp}"
            COMMAND ${_neg_verify_command}
            COMMAND ${CMAKE_COMMAND} -E touch "${_neg_stamp}"
            DEPENDS "${_neg_src}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tsa/mutex.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tsa/annotations.h"
            COMMENT "[OME] TSA negative test (clang -verify on mutex_negative_test.cpp)"
            VERBATIM
        )

        add_custom_target(ome_tsa_negative_verify ALL
            DEPENDS "${_neg_stamp}"
        )

        # Also register a ctest entry so `ctest -L tsa` runs the
        # equivalent check from a clean state. The compile is cheap
        # (-fsyntax-only) so re-running it under ctest is fine.
        add_test(
            NAME tsa_negative_verify
            COMMAND ${_neg_verify_command}
        )
        set_tests_properties(tsa_negative_verify PROPERTIES
            LABELS "tsa;base"
            TIMEOUT 60
        )
    endif()
endif()
