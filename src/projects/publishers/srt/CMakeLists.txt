ome_add_static_library(srt_publisher
    DEPS
        base_publisher
        mpegts_container
        monitoring
        orchestrator
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_publishers_srt
        SRCS ${_srcs}
    )
endif()
