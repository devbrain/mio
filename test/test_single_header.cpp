// Test that the single-header amalgamation compiles and works correctly
#include <mio/mio.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

// Verify std::byte aliases exist
static_assert(std::is_same_v<mio::bmmap_source::value_type, std::byte>);
static_assert(std::is_same_v<mio::bmmap_sink::value_type, std::byte>);

int main()
{
    std::error_code error;

    // Create a test file
    const char* path = "single-header-test-file";
    const std::string content = "Hello from single header test!";

    {
        std::ofstream file(path);
        file << content;
    }

    // Test basic mmap operations
    {
        mio::mmap_source source;
        source.map(path, error);
        assert(!error);
        assert(source.is_open());
        assert(source.size() == content.size());

        // Verify content using const reference to avoid non-const operator[]
        const auto& const_source = source;
        for (size_t i = 0; i < content.size(); ++i) {
            assert(const_source[i] == content[i]);
        }

        source.unmap();
        assert(!source.is_open());
    }

    // Test shared_mmap
    {
        mio::shared_mmap_source shared = mio::make_mmap<mio::shared_mmap_source>(
            path, 0, mio::map_entire_file, error);
        assert(!error);
        assert(shared.is_open());
        assert(shared.size() == content.size());
    }

    // Test std::filesystem::path
    {
        std::filesystem::path fs_path(path);
        mio::mmap_source fs_mmap;
        fs_mmap.map(fs_path, error);
        assert(!error);
        assert(fs_mmap.is_open());
    }

#if __cplusplus >= 202002L
    // Test std::span (C++20)
    {
        mio::mmap_source span_test;
        span_test.map(path, error);
        assert(!error);
        auto span = span_test.as_span();
        assert(span.size() == span_test.size());
    }
#endif

    // Cleanup
    std::remove(path);

    std::printf("single header tests passed!\n");
    return 0;
}
