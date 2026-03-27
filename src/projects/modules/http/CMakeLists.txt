ome_add_static_library(http
    SOURCES_DIRS_R
        server
        client
        protocol
        cors
        hpack
    DEPS
        ovsocket
        physical_port
        ovcrypto
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_modules
        SRCS ${_srcs}
    )
endif()
