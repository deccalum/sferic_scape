cmake_minimum_required(VERSION 3.16)
project(sferic_scape VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(sferic_core STATIC
  src/core/buffer.cpp
  src/core/logger.cpp
  src/io/audio_file.cpp
  src/io/parametric_json.cpp
  src/io/audio_sink.cpp
  src/analysis/spectral_envelope.cpp
  src/analysis/distribution_fitter.cpp
  src/analysis/parametric_model.cpp
  src/synthesis/noise_generator.cpp
  src/synthesis/parametric_synth.cpp
)
target_include_directories(sferic_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sferic_core PUBLIC
  PkgConfig::FFTW3
  PkgConfig::SNDFILE
  spdlog::spdlog
  nlohmann_json::nlohmann_json
)
target_compile_definitions(sferic_core PUBLIC
  SFERIC_HAS_SNDFILE
  SFERIC_HAS_FFTW3
  SFERIC_HAS_SPDLOG
)
add_executable(sferic src/main.cpp)
target_link_libraries(sferic PRIVATE sferic_core)

enable_testing()
add_executable(sferic_tests tests/test_pipeline.cpp)
target_link_libraries(sferic_tests PRIVATE sferic_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(sferic_tests)
