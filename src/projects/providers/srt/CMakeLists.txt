ome_add_static_library(srt_provider
    SOURCES_DIRS chunk
    DEPS
        base_provider
        mpegts_provider
        physical_port
        srt
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_srt
        SRCS ${_srcs}
    )
endif()
