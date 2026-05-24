cmake_minimum_required(VERSION 3.16)
project(sferic_scape VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Dependencies ---

# Try to find PkgConfig (available on Linux/macOS, optional on Windows)
find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    # FFTW3
    pkg_check_modules(FFTW3 IMPORTED_TARGET fftw3)

    # libsndfile
    pkg_check_modules(SNDFILE IMPORTED_TARGET sndfile)

    # JACK
    pkg_check_modules(JACK IMPORTED_TARGET jack)
else()
    # Windows: Try find_package fallback
    message(STATUS "PkgConfig not found, using find_package for dependencies")

    # FFTW3 - usually needs manual installation on Windows
    find_package(FFTW3 QUIET)

    # libsndfile - can be found via CMAKE or vcpkg
    find_package(SndFile QUIET)
    if(SndFile_FOUND)
        set(SNDFILE_FOUND TRUE)
    endif()

    # JACK - not typically available on Windows
    set(JACK_FOUND FALSE)
endif()

# Eigen3
find_package(Eigen3 3.3 QUIET)

# spdlog
find_package(spdlog QUIET)

# Google Test (fetch if not found)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# libsndfile (if not found via PkgConfig/find_package, fetch and build)
if(NOT SNDFILE_FOUND)
    message(STATUS "libsndfile not found, fetching from source...")
    set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
    FetchContent_Declare(
        libsndfile
        GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
        GIT_TAG 1.2.2
        PATCH_COMMAND ${CMAKE_COMMAND} -E echo "Patching libsndfile..."
    )
    set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
    set(ENABLE_MPEG OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(libsndfile)
    set(SNDFILE_FOUND TRUE)
endif()

# --- Core library ---

add_library(sferic_core STATIC
    src/core/buffer.cpp
    src/io/audio_file.cpp
    src/analysis/window.cpp
    src/analysis/stft.cpp
    src/analysis/multi_resolution.cpp
    src/analysis/stochastic_model.cpp
    src/spatial/stereo.cpp
    src/synthesis/noise_generator.cpp
    src/thunder/lightning_model.cpp
    src/thunder/propagation_model.cpp
    src/thunder/environment_model.cpp
    src/thunder/crack_analyzer.cpp
    src/thunder/thunder_synth.cpp
)

target_include_directories(sferic_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(SNDFILE_FOUND)
    if(PKG_CONFIG_FOUND AND TARGET PkgConfig::SNDFILE)
        target_link_libraries(sferic_core PUBLIC PkgConfig::SNDFILE)
    elseif(TARGET SndFile::sndfile)
        target_link_libraries(sferic_core PUBLIC SndFile::sndfile)
    elseif(TARGET sndfile)
        target_link_libraries(sferic_core PUBLIC sndfile)
    endif()
    target_compile_definitions(sferic_core PUBLIC SFERIC_HAS_SNDFILE)
endif()

if(FFTW3_FOUND)
    if(PKG_CONFIG_FOUND AND TARGET PkgConfig::FFTW3)
        target_link_libraries(sferic_core PUBLIC PkgConfig::FFTW3)
    elseif(TARGET FFTW3::fftw3)
        target_link_libraries(sferic_core PUBLIC FFTW3::fftw3)
    endif()
    target_compile_definitions(sferic_core PUBLIC SFERIC_HAS_FFTW3)
endif()

if(JACK_FOUND)
    if(PKG_CONFIG_FOUND AND TARGET PkgConfig::JACK)
        target_link_libraries(sferic_core PUBLIC PkgConfig::JACK)
    endif()
    target_compile_definitions(sferic_core PUBLIC SFERIC_HAS_JACK)
endif()

if(Eigen3_FOUND)
    target_link_libraries(sferic_core PUBLIC Eigen3::Eigen)
    target_compile_definitions(sferic_core PUBLIC SFERIC_HAS_EIGEN)
endif()

if(spdlog_FOUND)
    target_link_libraries(sferic_core PUBLIC spdlog::spdlog)
    target_compile_definitions(sferic_core PUBLIC SFERIC_HAS_SPDLOG)
endif()

# --- CLI tools ---

add_executable(synthesize tools/synthesize.cpp)
target_link_libraries(synthesize PRIVATE sferic_core)

add_executable(thunder tools/thunder.cpp)
target_link_libraries(thunder PRIVATE sferic_core)

# --- Tests ---

enable_testing()

add_executable(sferic_tests
    tests/test_buffer.cpp
    tests/test_audio_file.cpp
    tests/test_stft.cpp
    tests/test_stochastic.cpp
)
target_link_libraries(sferic_tests PRIVATE sferic_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(sferic_tests)
