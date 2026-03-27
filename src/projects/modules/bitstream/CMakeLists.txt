ome_add_static_library(bitstream
    SOURCES_DIRS
        aac
        opus
        vp8
        nalu
        h264
        h265
        mp3
    DEPS ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB_RECURSE _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_modules
        SRCS ${_srcs}

    )
endif()
