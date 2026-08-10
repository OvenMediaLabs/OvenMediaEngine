ome_add_static_library(bmff_container
    SOURCES_DIRS fmp4_packager
    DEPS
        bitstream
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/fmp4_packager/*_test.cpp")
    ome_add_tests(ome_test_bmff
        SRCS ${_srcs}
    )
endif()
