cmake_minimum_required(VERSION 3.14)
project(tl-expected
  HOMEPAGE_URL https://tl.tartanllama.xyz
  DESCRIPTION "C++11/14/17 std::expected with functional-style extensions"
  VERSION 1.3.1
  LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(CMakeDependentOption)
include(GNUInstallDirs)
include(FetchContent)
include(CTest)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

option(EXPECTED_BUILD_PACKAGE "Build package files as well" ON)

cmake_dependent_option(EXPECTED_BUILD_TESTS
  "Enable tl::expected tests" ON
  "BUILD_TESTING" OFF)

cmake_dependent_option(EXPECTED_BUILD_PACKAGE_DEB
  "Create a DEB" ON
  "EXPECTED_BUILD_PACKAGE" OFF)

add_library(expected INTERFACE)
target_include_directories(expected
  INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if (NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  add_library(tl::expected ALIAS expected)
endif()

# Installation help
configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}-config.cmake.in"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
  INSTALL_DESTINATION "share/cmake/${PROJECT_NAME}")

write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
  COMPATIBILITY SameMajorVersion
  ARCH_INDEPENDENT)

install(TARGETS expected
  EXPORT ${PROJECT_NAME}-targets
  INCLUDES DESTINATION "${CMAKE_INSTALL_DATADIR}")

install(EXPORT ${PROJECT_NAME}-targets
  DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/${PROJECT_NAME}"
  NAMESPACE tl::
  FILE "${PROJECT_NAME}-targets.cmake")

install(FILES
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/cmake/${PROJECT_NAME}")

install(DIRECTORY "include/" TYPE INCLUDE)

if(EXPECTED_BUILD_TESTS)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  set(CATCH_INSTALL_HELPERS OFF)
  set(CATCH_BUILD_TESTING OFF)
  set(CATCH_INSTALL_DOCS OFF)
  FetchContent_Declare(Catch2 URL
    https://github.com/catchorg/Catch2/archive/v2.13.10.zip) 
  FetchContent_MakeAvailable(Catch2)

  file(GLOB test-sources CONFIGURE_DEPENDS tests/*.cpp)
  list(FILTER test-sources EXCLUDE REGEX "tests/test.cpp")
  add_executable(${PROJECT_NAME}-tests "${test-sources}")
  target_compile_options(${PROJECT_NAME}-tests PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

  target_link_libraries(${PROJECT_NAME}-tests
    PRIVATE
      Catch2::Catch2
      expected)
  add_test(NAME tl::expected::tests COMMAND ${PROJECT_NAME}-tests)
endif()

if (NOT EXPECTED_BUILD_PACKAGE)
  return()
endif()

list(APPEND source-generators TBZ2 TGZ TXZ ZIP)

if (CMAKE_HOST_WIN32)
  list(APPEND binary-generators "WIX")
endif()

if (EXPECTED_BUILD_PACKAGE_DEB)
  list(APPEND binary-generators "DEB")
endif()

if (EXPECTED_BUILD_RPM)
  list(APPEND binary-generators "RPM")
endif()


set(CPACK_SOURCE_GENERATOR ${source-generators})
set(CPACK_GENERATOR ${binary-generators})

set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sy Brand")

list(APPEND CPACK_SOURCE_IGNORE_FILES /.git/ /build/ .gitignore .DS_Store)

include(CPack)

