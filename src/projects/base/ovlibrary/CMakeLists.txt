ome_add_static_library(ovlibrary
    SOURCES_DIRS
        logger
        logger/formatters
        logger/pattern_flags
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_base
        SRCS ${_srcs}
    )
endif()
