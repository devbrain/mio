#
# Main test executable
#
add_executable(mio_test test.cpp)
target_link_libraries(mio_test PRIVATE mio::mio)
set_target_properties(mio_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

add_test(NAME mio_test COMMAND mio_test)

#
# C++20 test (if supported)
#
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++20" COMPILER_SUPPORTS_CXX20)

if(COMPILER_SUPPORTS_CXX20)
    add_executable(mio_test_cxx20 test.cpp)
    target_link_libraries(mio_test_cxx20 PRIVATE mio::mio)
    set_target_properties(mio_test_cxx20 PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)
    add_test(NAME mio_test_cxx20 COMMAND mio_test_cxx20)
endif()

#
# Windows-specific tests
#
if(WIN32)
    # Unicode test
    add_executable(mio_test_unicode test.cpp)
    target_link_libraries(mio_test_unicode PRIVATE mio::mio)
    target_compile_definitions(mio_test_unicode PRIVATE UNICODE _UNICODE)
    add_test(NAME mio_test_unicode COMMAND mio_test_unicode)

    # Full Windows API test
    add_executable(mio_test_fullwinapi test.cpp)
    target_link_libraries(mio_test_fullwinapi PRIVATE mio::mio_full_winapi)
    add_test(NAME mio_test_fullwinapi COMMAND mio_test_fullwinapi)

    # Minimal Windows API test
    add_executable(mio_test_minwinapi test.cpp)
    target_link_libraries(mio_test_minwinapi PRIVATE mio::mio_min_winapi)
    add_test(NAME mio_test_minwinapi COMMAND mio_test_minwinapi)
endif()

#
# Single-header test
#
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../single_include/mio/mio.hpp")
    add_executable(mio_test_single_header test_single_header.cpp)
    target_include_directories(mio_test_single_header PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../single_include")
    set_target_properties(mio_test_single_header PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    add_test(NAME mio_test_single_header COMMAND mio_test_single_header)
endif()
