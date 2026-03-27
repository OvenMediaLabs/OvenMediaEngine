ome_add_static_library(orchestrator
    DEPS
        config
        monitoring
        http
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB_RECURSE _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_orchestrator
        SRCS ${_srcs}
    )
endif()
