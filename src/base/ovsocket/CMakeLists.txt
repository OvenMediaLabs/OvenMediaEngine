# target: ovsocket
ome_add_static_library(ovsocket
    SOURCES_DIRS socket_pool
    DEPS
        ovcrypto
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_base
        SRCS ${_srcs}
    )
endif()
