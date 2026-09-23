ome_add_static_library(ovt_packetizer)

# The wire table must stop building when an enumerator has no wire value (see ovt_wire.cpp).
set_source_files_properties(ovt_wire.cpp PROPERTIES COMPILE_OPTIONS "-Werror=switch")

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_modules
        SRCS ${_srcs}
    )
endif()
