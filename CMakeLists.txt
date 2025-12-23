cmake_minimum_required(VERSION 3.14)

#
# Detect if mio is being configured as a subproject.
#
if(DEFINED PROJECT_NAME)
    set(MIO_SUBPROJECT ON)
else()
    set(MIO_SUBPROJECT OFF)
endif()

project(mio
    VERSION 1.1.0
    DESCRIPTION "Cross-platform C++17 header-only library for memory mapped file IO"
    HOMEPAGE_URL "https://github.com/mandreyel/mio"
    LANGUAGES CXX)

#
# CMake configuration
#
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MIO_SUBPROJECT)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

#
# C++17 is required (C++20 enables optional features like std::span)
#
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#
# Options
#
include(CMakeDependentOption)

CMAKE_DEPENDENT_OPTION(MIO_BUILD_TESTS
    "Build the mio tests"
    ON "NOT MIO_SUBPROJECT" OFF)

CMAKE_DEPENDENT_OPTION(MIO_BUILD_SINGLE_HEADER
    "Generate single-header amalgamation"
    ON "NOT MIO_SUBPROJECT" OFF)

CMAKE_DEPENDENT_OPTION(MIO_WINDOWS_FULL_API
    "Configure mio without WIN32_LEAN_AND_MEAN and NOMINMAX definitions"
    OFF "WIN32" ON)

CMAKE_DEPENDENT_OPTION(MIO_INSTALL
    "Include mio in the install set"
    ON "NOT MIO_SUBPROJECT" OFF)

#
# Compiler warnings (internal use only, not exported to consumers)
#
add_library(mio_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(mio_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(mio_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic)
elseif(MSVC)
    target_compile_options(mio_warnings INTERFACE
        /W4
        /permissive-)
endif()

#
# mio library target (header-only)
#
add_library(mio INTERFACE)
add_library(mio::mio ALIAS mio)

target_compile_features(mio INTERFACE cxx_std_17)

target_include_directories(mio INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(NOT MIO_WINDOWS_FULL_API)
    target_compile_definitions(mio INTERFACE
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>)
endif()

#
# Windows API variants
#
if(WIN32)
    # Full Windows API variant
    add_library(mio_full_winapi INTERFACE)
    add_library(mio::mio_full_winapi ALIAS mio_full_winapi)
    target_compile_features(mio_full_winapi INTERFACE cxx_std_17)
    target_include_directories(mio_full_winapi INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    # Minimal Windows API variant
    add_library(mio_min_winapi INTERFACE)
    add_library(mio::mio_min_winapi ALIAS mio_min_winapi)
    target_compile_features(mio_min_winapi INTERFACE cxx_std_17)
    target_include_directories(mio_min_winapi INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_definitions(mio_min_winapi INTERFACE
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
endif()

#
# IDE support: collect headers for project view
#
add_library(mio-headers INTERFACE)
add_library(mio::mio-headers ALIAS mio-headers)
target_link_libraries(mio-headers INTERFACE mio)

target_sources(mio-headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mio/mmap.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mio/page.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mio/shared_mmap.hpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mio/detail/mmap.ipp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mio/detail/string_util.hpp>)

#
# Single-header amalgamation
#
if(MIO_BUILD_SINGLE_HEADER)
    include(MioAmalgamate)
    mio_create_single_header(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/single_include/mio/mio.hpp"
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/include/mio/mmap.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/mio/page.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/mio/shared_mmap.hpp"
        INCLUDE_PATHS
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

#
# Tests
#
if(MIO_BUILD_TESTS)
    include(CTest)
    add_subdirectory(test)
endif()

#
# Installation
#
if(MIO_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILES_MATCHING PATTERN "*.*pp")

    install(TARGETS mio mio-headers EXPORT mioTargets)

    if(WIN32)
        install(TARGETS mio_full_winapi mio_min_winapi EXPORT mioTargets)
    endif()

    install(EXPORT mioTargets
        FILE mio-targets.cmake
        NAMESPACE mio::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mio")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/mio-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mio-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mio-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mio")

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mio-config-version.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/mio-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mio")

    #
    # CPack support
    #
    set(CPACK_PACKAGE_VENDOR "mandreyel")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    include(CPack)
endif()
