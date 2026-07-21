ome_add_static_library(ffmpeg_wrapper)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_modules
        SRCS ${_srcs}
    )
endif()
