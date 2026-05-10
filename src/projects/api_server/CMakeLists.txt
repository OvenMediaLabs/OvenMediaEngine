ome_add_static_library(api_server
    SOURCES_DIRS_R
        controllers
        converters
        helpers
    DEPS
        http
        json_serdes
        monitoring
        config
        ovlibrary
        ovcrypto
        orchestrator
)

if(OME_BUILD_TESTS)
    file(GLOB_RECURSE _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_api_server
        SRCS ${_srcs}
    )
endif()
