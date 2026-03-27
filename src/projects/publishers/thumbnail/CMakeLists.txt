ome_add_static_library(thumbnail_publisher
    DEPS
        base_publisher
        http
        ovt_packetizer
        monitoring
        orchestrator
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_publishers_thumbnail
        SRCS ${_srcs}
    )
endif()
