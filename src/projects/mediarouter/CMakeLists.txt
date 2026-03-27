ome_add_static_library(mediarouter
    SOURCES_DIRS events
    DEPS
        transcoder
        base_info
        base_amf_event
        managed_queue
        config
        monitoring
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB_RECURSE _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_mediarouter
        SRCS ${_srcs}
    )
endif()
