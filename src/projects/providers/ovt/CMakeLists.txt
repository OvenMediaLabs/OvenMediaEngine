ome_add_static_library(ovt_provider
    DEPS
        base_provider
        bitstream
        ovt_packetizer
        monitoring
        orchestrator
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_ovt
        SRCS ${_srcs}
    )
endif()
