cmake_minimum_required(VERSION 3.16)
project(sferic_scape VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(SFERIC_WARNINGS -Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
find_package(GTest REQUIRED)
set(SFERIC_QUERY_RUNNER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qr")
add_subdirectory(${SFERIC_QUERY_RUNNER_DIR} ${CMAKE_BINARY_DIR}/query_runner)
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp
  src/io/*.cpp
  src/cli/*.cpp
  src/ui/*.cpp
  src/analysis/*.cpp
  src/module/*.cpp
  src/synthesis/*.cpp
  src/*.cpp)

list(FILTER CORE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
add_library(sferic_core STATIC ${CORE_SOURCES})
target_include_directories(sferic_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(sferic_core PRIVATE ${SFERIC_WARNINGS})
target_link_libraries(sferic_core PUBLIC
  PkgConfig::FFTW3
  PkgConfig::SNDFILE
  PkgConfig::PORTAUDIO
  query_runner
)
target_compile_definitions(sferic_core PUBLIC
  SFERIC_HAS_SNDFILE
  SFERIC_HAS_FFTW3
  SFERIC_HAS_PQXX
  SFERIC_HAS_PORTAUDIO
)
add_executable(sferic src/main.cpp)
target_link_libraries(sferic PRIVATE sferic_core)
target_compile_options(sferic PRIVATE ${SFERIC_WARNINGS})

enable_testing()
include(GoogleTest)
add_executable(sferic_detector_tests tests/test_detector.cpp)
target_link_libraries(sferic_detector_tests PRIVATE sferic_core GTest::gtest_main)
target_compile_options(sferic_detector_tests PRIVATE ${SFERIC_WARNINGS})
gtest_discover_tests(sferic_detector_tests)
