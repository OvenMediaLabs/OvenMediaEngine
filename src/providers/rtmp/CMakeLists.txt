ome_add_static_library(rtmp_provider
    SOURCES_DIRS
        tracks
        handlers
    DEPS
        base_provider
        rtmp_module
        rtmp_v2_module
        flv_container
        flv_container_v2
        bitstream
        access_controller
        physical_port
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_rtmp
        SRCS ${_srcs}
    )
endif()
