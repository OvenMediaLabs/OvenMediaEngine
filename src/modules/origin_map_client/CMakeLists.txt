ome_add_static_library(origin_map_client)

if(OME_BUILD_TESTS)
    ome_add_tests(ome_test_origin_map_client
        SRCS ${CMAKE_CURRENT_SOURCE_DIR}/origin_map_client_test.cpp)
endif()
