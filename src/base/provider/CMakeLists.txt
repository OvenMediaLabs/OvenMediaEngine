ome_add_static_library(base_provider
    SOURCES_DIRS
        pull_provider
        push_provider
    PUBLIC_DEPS
        base_info
    DEPS
        access_controller
        ovsocket
        monitoring
        config
        mediarouter
        orchestrator
        ovlibrary
        ovcrypto
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/pull_provider/*_test.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/push_provider/*_test.cpp"
    )
    ome_add_tests(ome_test_base
        SRCS ${_srcs}
    )
endif()
