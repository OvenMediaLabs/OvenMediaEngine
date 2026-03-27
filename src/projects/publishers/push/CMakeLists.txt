ome_add_static_library(push_publisher
    DEPS
        base_publisher
        ffmpeg_wrapper
        monitoring
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_publishers_push
        SRCS ${_srcs}
    )
endif()
