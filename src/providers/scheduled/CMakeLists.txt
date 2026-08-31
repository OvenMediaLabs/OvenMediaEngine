ome_add_static_library(scheduled_provider
    DEPS
        base_provider
        ffmpeg_wrapper
        mediarouter
        orchestrator
        ovlibrary
        task_pool
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_scheduled
        SRCS ${_srcs}
    )
endif()
