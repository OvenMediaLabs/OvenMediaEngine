ome_add_static_library(config
    SOURCES_DIRS
        engine
        items
        utilities
)

if(OME_BUILD_TESTS)
    file(GLOB_RECURSE _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_config
        SRCS ${_srcs}
    )
endif()
