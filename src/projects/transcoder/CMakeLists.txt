ome_add_static_library(transcoder
    SOURCES_DIRS
        codec
        codec/decoder
        codec/encoder
        filter
    DEPS
        bitstream
        ffmpeg_wrapper
        monitoring
        config
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB_RECURSE _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_transcoder
        SRCS ${_srcs}
    )
endif()
