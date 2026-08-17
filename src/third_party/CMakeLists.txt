#
# third_party/CMakeLists.txt
#

# ==============================================================================
# jsoncpp  (bundled source)
# ==============================================================================
file(GLOB JSONCPP_SRC "jsoncpp-1.9.3/*.cpp" "jsoncpp-1.9.3/*.c")
add_library(jsoncpp STATIC ${JSONCPP_SRC})
target_include_directories(jsoncpp PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/jsoncpp-1.9.3"
    ${OME_GLOBAL_INCLUDE_DIRS}
)
target_compile_features(jsoncpp PUBLIC cxx_std_17)

# ==============================================================================
# pugixml  (bundled source - build from pugixml-1.9/src/pugixml.cpp)
# ==============================================================================
add_library(pugixml STATIC "pugixml-1.9/src/pugixml.cpp")
target_include_directories(pugixml PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/pugixml-1.9/src"
    ${OME_GLOBAL_INCLUDE_DIRS}
)
target_compile_features(pugixml PUBLIC cxx_std_17)

# Register bundled third-party static libs in the global OME_STATIC_LIBS list
# so main/CMakeLists.txt can pull them in automatically.
set_property(GLOBAL APPEND PROPERTY OME_STATIC_LIBS jsoncpp pugixml)
