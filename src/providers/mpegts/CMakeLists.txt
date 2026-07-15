ome_add_static_library(mpegts_provider
    SOURCES_DIRS chunk
    DEPS
        base_provider
        bitstream
        mpegts_container
        physical_port
        base_scte35_event
        orchestrator
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_mpegts
        SRCS ${_srcs}
    )
endif()
