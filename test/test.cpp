#include <mio/mmap.hpp>
#include <mio/shared_mmap.hpp>

#include <string>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <cassert>
#include <system_error>
#include <numeric>
#include <filesystem>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

// std::byte is now always available (C++17 required)
using byte_mmap_source = mio::basic_mmap_source<std::byte>;

// Test std::byte type aliases
static_assert(std::is_same_v<mio::bmmap_source::value_type, std::byte>);
static_assert(std::is_same_v<mio::bmmap_sink::value_type, std::byte>);

template<class MMap>
void test_at_offset(const MMap& file_view, const std::string& buffer,
        const size_t offset);
void test_at_offset(const std::string& buffer, const char* path,
        const size_t offset, std::error_code& error);
int handle_error(const std::error_code& error);

int main()
{
    std::error_code error;

    // Make sure mio compiles with non-const char* strings too.
    const char _path[] = "test-file";
    const int path_len = sizeof(_path);
    char* path = new char[path_len];
    std::copy(_path, _path + path_len, path);

    const auto page_size = mio::page_size();
    // Fill buffer, then write it to file.
    const size_t file_size = 4 * page_size - 250; // 16134, if page size is 4KiB
    std::string buffer(file_size, 0);
    // Start at first printable ASCII character.
    char v = 33;
    for (auto& b : buffer) {
       b = v;
       ++v;
       // Limit to last printable ASCII character.
       v %= 126;
       if(v == 0) {
           v = 33;
       }
    }

    std::ofstream file(path);
    file << buffer;
    file.close();

    // Test whole file mapping.
    test_at_offset(buffer, path, 0, error);
    if (error) { return handle_error(error); }

    // Test starting from below the page size.
    test_at_offset(buffer, path, page_size - 3, error);
    if (error) { return handle_error(error); }

    // Test starting from above the page size.
    test_at_offset(buffer, path, page_size + 3, error);
    if (error) { return handle_error(error); }

    // Test starting from above the page size.
    test_at_offset(buffer, path, 2 * page_size + 3, error);
    if (error) { return handle_error(error); }

    {
#define CHECK_INVALID_MMAP(m) do { \
        assert(error); \
        assert(m.empty()); \
        assert(!m.is_open()); \
        error.clear(); } while(0)

        mio::mmap_source m;

        // See if mapping an invalid file results in an error.
        m = mio::make_mmap_source("garbage-that-hopefully-doesnt-exist", 0, 0, error);
        CHECK_INVALID_MMAP(m);

        // Empty path?
        m = mio::make_mmap_source(static_cast<const char*>(0), 0, 0, error);
        CHECK_INVALID_MMAP(m);
        m = mio::make_mmap_source(std::string(), 0, 0, error);
        CHECK_INVALID_MMAP(m);

        // Invalid handle?
        m = mio::make_mmap_source(mio::invalid_handle, 0, 0, error);
        CHECK_INVALID_MMAP(m);

        // Invalid offset?
        m = mio::make_mmap_source(path, 100 * buffer.size(), buffer.size(), error);
        CHECK_INVALID_MMAP(m);
    }

    // Make sure these compile.
    {
        mio::ummap_source _1;
        mio::shared_ummap_source _2;
        // Make sure shared_mmap mapping compiles as all testing was done on
        // normal mmaps.
        auto _3 = mio::make_shared_mmap_source(path, 0, mio::map_entire_file, error);
        assert(!error);
        auto _4 = mio::make_mmap_source(path, error);
        auto _5 = mio::make_shared_mmap_source(path, error);

        // Test std::filesystem::path support (C++17)
        std::filesystem::path fs_path(path);
        mio::mmap_source fs_mmap;
        fs_mmap.map(fs_path, error);
        assert(!error);
        assert(fs_mmap.is_open());
        fs_mmap.unmap();

        // Test std::byte aliases
        mio::bmmap_source byte_source;
        mio::shared_bmmap_source shared_byte_source;

#ifdef _WIN32
        const wchar_t* wpath1 = L"test-file";
        std::filesystem::path wfs_path(wpath1);
        mio::mmap_source wpath_mmap;
        wpath_mmap.map(wfs_path, error);
        // Don't assert on error - file may not exist with this name
        error.clear();
#else
        const int fd = open(path, O_RDONLY);
        mio::mmap_source _fdmmap(fd, 0, mio::map_entire_file);
        _fdmmap.unmap();
        _fdmmap.map(fd, error);
#endif
    }

#if __cplusplus >= 202002L
    // Test std::span support (C++20)
    {
        mio::mmap_source span_test = mio::make_mmap_source(path, error);
        if (!error) {
            [[maybe_unused]] auto span = span_test.as_span();
            assert(span.size() == span_test.size());
        }
        error.clear();
    }
#endif

    std::printf("all tests passed!\n");
}

void test_at_offset(const std::string& buffer, const char* path,
        const size_t offset, std::error_code& error)
{
    // Sanity check.
    assert(offset < buffer.size());

    // Map the region of the file to which buffer was written.
    mio::mmap_source file_view = mio::make_mmap_source(
            path, offset, mio::map_entire_file, error);
    if(error) { return; }

    assert(file_view.is_open());
    [[maybe_unused]] const size_t mapped_size = buffer.size() - offset;
    assert(file_view.size() == mapped_size);

    test_at_offset(file_view, buffer, offset);

    // Turn file_view into a shared mmap.
    mio::shared_mmap_source shared_file_view(std::move(file_view));
    assert(!file_view.is_open());
    assert(shared_file_view.is_open());
    assert(shared_file_view.size() == mapped_size);

    //test_at_offset(shared_file_view, buffer, offset);
}

template<class MMap>
void test_at_offset(const MMap& file_view, const std::string& buffer,
        const size_t offset)
{
    // Then verify that mmap's bytes correspond to that of buffer.
    for(size_t buf_idx = offset, view_idx = 0;
            buf_idx < buffer.size() && view_idx < file_view.size();
            ++buf_idx, ++view_idx) {
        if(file_view[view_idx] != buffer[buf_idx]) {
            std::printf("%zuth byte mismatch: expected(%d) <> actual(%d)",
                    buf_idx, buffer[buf_idx], file_view[view_idx]);
            std::cout << std::flush;
            assert(0);
        }
    }
}

int handle_error(const std::error_code& error)
{
    const auto& errmsg = error.message();
    std::printf("Error mapping file: %s, exiting...\n", errmsg.c_str());
    return error.value();
}
