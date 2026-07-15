ome_add_static_library(hls_publisher
    DEPS
        base_publisher
        mpegts_container
        webvtt_packager
        access_controller
        dump
        http
        monitoring
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_publishers_hls
        SRCS ${_srcs}
    )
endif()
