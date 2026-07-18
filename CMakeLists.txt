cmake_minimum_required(VERSION 3.16...3.31)

project(decimal VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(DECIMAL_IS_TOP_LEVEL ON)
else()
  set(DECIMAL_IS_TOP_LEVEL OFF)
endif()

option(DECIMAL_BUILD_TESTS "Build decimal tests" ${DECIMAL_IS_TOP_LEVEL})
option(DECIMAL_INSTALL "Generate decimal installation rules" ${DECIMAL_IS_TOP_LEVEL})

add_library(decimal INTERFACE)
add_library(decimal::decimal ALIAS decimal)

target_compile_features(decimal INTERFACE cxx_std_17)
target_include_directories(
  decimal
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(DECIMAL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

if(DECIMAL_INSTALL)
  include(CMakePackageConfigHelpers)

  install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(
    TARGETS decimal
    EXPORT decimalTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(
    EXPORT decimalTargets
    FILE decimalTargets.cmake
    NAMESPACE decimal::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/decimal)

  configure_package_config_file(
    cmake/decimalConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/decimalConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/decimal)

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/decimalConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/decimalConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/decimalConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/decimal)
endif()
