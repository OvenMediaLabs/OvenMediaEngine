ome_add_static_library(base_publisher
    PUBLIC_DEPS
        base_info
    DEPS
        ovsocket
        monitoring
        config
        mediarouter
        ovlibrary
        ovcrypto
        http
        base_event
        managed_queue
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_base
        SRCS ${_srcs}
    )
endif()
