cmake_minimum_required(VERSION 3.20)

project(mio
    VERSION 1.1.0
    DESCRIPTION "Cross-platform C++17 header-only library for memory mapped file IO"
    HOMEPAGE_URL "https://github.com/devbrain/mio"
    LANGUAGES CXX
)

# =============================================================================
# Platform Check - mio requires OS-level mmap support
# =============================================================================

if(EMSCRIPTEN)
    message(WARNING "[mio] Memory-mapped I/O is not supported on Emscripten/WebAssembly")
    # Create a dummy interface target so dependent projects don't break
    add_library(mio INTERFACE)
    add_library(mio::mio ALIAS mio)
    add_library(neutrino::mio ALIAS mio)
    return()
endif()

# =============================================================================
# Neutrino CMake Integration
# =============================================================================

include(FetchContent)

if(NOT DEFINED NEUTRINO_CMAKE_DIR)
    FetchContent_Declare(neutrino_cmake
        GIT_REPOSITORY https://github.com/devbrain/neutrino-cmake.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(neutrino_cmake)
    set(NEUTRINO_CMAKE_DIR "${neutrino_cmake_SOURCE_DIR}/cmake")
    list(APPEND CMAKE_MODULE_PATH "${NEUTRINO_CMAKE_DIR}")
endif()

include(NeutrinoInit)

# =============================================================================
# Options
# =============================================================================

neutrino_define_options(mio)

option(NEUTRINO_MIO_BUILD_SINGLE_HEADER
    "Generate single-header amalgamation"
    OFF
)

option(NEUTRINO_MIO_WINDOWS_FULL_API
    "Configure mio without WIN32_LEAN_AND_MEAN and NOMINMAX definitions"
    OFF
)

# =============================================================================
# mio library target (header-only)
# =============================================================================

add_library(mio INTERFACE)
add_library(mio::mio ALIAS mio)
add_library(neutrino::mio ALIAS mio)

target_compile_features(mio INTERFACE cxx_std_17)

target_include_directories(mio INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(NOT NEUTRINO_MIO_WINDOWS_FULL_API)
    target_compile_definitions(mio INTERFACE
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
    )
endif()

# =============================================================================
# Windows API variants
# =============================================================================

if(WIN32)
    # Full Windows API variant (no WIN32_LEAN_AND_MEAN, no NOMINMAX)
    add_library(mio_full_winapi INTERFACE)
    add_library(mio::mio_full_winapi ALIAS mio_full_winapi)

    target_compile_features(mio_full_winapi INTERFACE cxx_std_17)
    target_include_directories(mio_full_winapi INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    # Minimal Windows API variant (with WIN32_LEAN_AND_MEAN and NOMINMAX)
    add_library(mio_min_winapi INTERFACE)
    add_library(mio::mio_min_winapi ALIAS mio_min_winapi)

    target_compile_features(mio_min_winapi INTERFACE cxx_std_17)
    target_include_directories(mio_min_winapi INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_definitions(mio_min_winapi INTERFACE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# =============================================================================
# Single-header amalgamation
# =============================================================================

if(NEUTRINO_MIO_BUILD_SINGLE_HEADER)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    include(MioAmalgamate)
    mio_create_single_header(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/single_include/mio/mio.hpp"
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/include/mio/mmap.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/mio/page.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/mio/shared_mmap.hpp"
        INCLUDE_PATHS
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
endif()

# =============================================================================
# Tests
# =============================================================================

if(NEUTRINO_MIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# =============================================================================
# Installation
# =============================================================================

if(NEUTRINO_MIO_INSTALL)
    include(NeutrinoInstall)

    neutrino_install_headers(mio)

    neutrino_install_library(mio
        NAMESPACE neutrino::
        COMPATIBILITY SameMajorVersion
    )

    # Install Windows API variants
    if(WIN32)
        install(TARGETS mio_full_winapi mio_min_winapi
            EXPORT mio-targets
        )
    endif()
endif()

# =============================================================================
# Summary
# =============================================================================

neutrino_is_top_level(_mio_is_top_level)
if(_mio_is_top_level)
    neutrino_print_options(mio)
endif()
