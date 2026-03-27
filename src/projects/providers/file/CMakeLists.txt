ome_add_static_library(file_provider
    DEPS
        base_provider
        ffmpeg_wrapper
        rtp_rtcp
        rtsp_module
        sdp
        orchestrator
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_file
        SRCS ${_srcs}
    )
endif()
