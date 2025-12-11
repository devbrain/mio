# mio

A cross-platform C++17 header-only library for memory mapped file IO.

This is a modernized fork of [mandreyel/mio](https://github.com/mandreyel/mio), updated to require C++17 and take advantage of modern C++ features. The original library was created by [mandreyel](https://github.com/mandreyel) and is licensed under the MIT license.

## Features

- Header-only with no dependencies
- Cross-platform: Windows, Linux, macOS
- Supports both read-only and read-write mappings
- Move-only semantics with zero-cost abstraction over system APIs
- Optional shared ownership via `shared_mmap`
- Works with file paths or existing file handles/descriptors
- Automatic page boundary alignment
- Support for `std::filesystem::path`
- Support for `std::byte` via type aliases
- Optional `std::span` conversion (C++20)
- Single-header version available

## Requirements

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2017 15.7+, Apple Clang 11+)
- CMake 3.14+ (for building tests)

## Why mio?

Memory mapping is a powerful technique that can significantly improve IO performance by allowing direct access to file contents through memory pointers, avoiding explicit read/write system calls.

The primary motivations for mio over alternatives like Boost.Iostreams:

1. **File handle support**: mio can establish a memory mapping with an already open file handle/descriptor, not just file paths.

2. **Automatic offset alignment**: Boost.Iostreams requires users to pick offsets exactly at page boundaries, which is error-prone. mio manages this internally, accepting any offset and finding the nearest page boundary automatically.

3. **Flexible ownership semantics**: Boost.Iostreams uses `std::shared_ptr` for all mappings, incurring heap allocation overhead even when not needed. mio provides two classes:
   - `mio::mmap_source` / `mio::mmap_sink`: Move-only, zero-cost abstraction
   - `mio::shared_mmap_source` / `mio::shared_mmap_sink`: Shared ownership when needed

4. **No dependencies**: mio is a standalone header-only library with no external dependencies.

## Quick Start

```cpp
#include <mio/mmap.hpp>
#include <system_error>
#include <cstdio>

int main()
{
    std::error_code ec;

    // Memory map a file for reading
    mio::mmap_source source;
    source.map("data.bin", ec);
    if (ec) {
        std::printf("Error: %s\n", ec.message().c_str());
        return 1;
    }

    // Access data directly
    for (const char& byte : source) {
        // process byte...
    }

    return 0;
}
```

## Usage Examples

### Creating a Read-Only Mapping

There are three ways to create a mapping:

**Using the constructor (throws on failure):**
```cpp
#include <mio/mmap.hpp>

// Map entire file
mio::mmap_source mmap("path/to/file");

// Map with offset and length
mio::mmap_source mmap("path/to/file", offset, length);
```

**Using the factory function:**
```cpp
std::error_code ec;
auto mmap = mio::make_mmap_source("path/to/file", ec);
if (ec) { /* handle error */ }

// With offset and length
auto mmap = mio::make_mmap_source("path/to/file", offset, length, ec);
```

**Using the map() member function:**
```cpp
mio::mmap_source mmap;
std::error_code ec;
mmap.map("path/to/file", ec);
if (ec) { /* handle error */ }
```

### Creating a Read-Write Mapping

Use `mmap_sink` instead of `mmap_source`:

```cpp
#include <mio/mmap.hpp>
#include <algorithm>

int main()
{
    std::error_code ec;

    // Map file for writing
    mio::mmap_sink mmap = mio::make_mmap_sink("output.bin", ec);
    if (ec) { return 1; }

    // Modify the mapped memory
    std::fill(mmap.begin(), mmap.end(), 0);
    mmap[0] = 'H';
    mmap[1] = 'i';

    // Sync changes to disk (also done automatically on destruction)
    mmap.sync(ec);

    return 0;
}
```

### Using File Handles/Descriptors

```cpp
// POSIX
#include <fcntl.h>
#include <mio/mmap.hpp>

int fd = open("file.txt", O_RDONLY);
mio::mmap_source mmap(fd, 0, mio::map_entire_file);

// Windows
#include <windows.h>
#include <mio/mmap.hpp>

HANDLE handle = CreateFileA("file.txt", GENERIC_READ, ...);
mio::mmap_source mmap(handle, 0, mio::map_entire_file);
```

### Using std::filesystem::path

```cpp
#include <mio/mmap.hpp>
#include <filesystem>

std::filesystem::path path = "/data/file.bin";
std::error_code ec;

mio::mmap_source mmap;
mmap.map(path, ec);
```

### Shared Ownership

When multiple owners need access to the same mapping:

```cpp
#include <mio/shared_mmap.hpp>

// Create a shared mapping
mio::shared_mmap_source shared1("path/to/file");

// Copy (both share the same underlying mapping)
mio::shared_mmap_source shared2 = shared1;

// Move from a regular mmap
mio::mmap_source mmap("path/to/file");
mio::shared_mmap_source shared3(std::move(mmap));
```

### Using std::byte (C++17)

```cpp
#include <mio/mmap.hpp>

std::error_code ec;
mio::bmmap_source byte_map;  // basic_mmap_source<std::byte>
byte_map.map("binary.dat", ec);

for (std::byte b : byte_map) {
    // process bytes...
}
```

### Using std::span (C++20)

```cpp
#include <mio/mmap.hpp>
#include <span>

mio::mmap_source mmap("data.bin");
std::span<const char> span = mmap.as_span();
```

### Complete Example

```cpp
#include <mio/mmap.hpp>
#include <system_error>
#include <fstream>
#include <cstdio>
#include <cassert>

int main()
{
    const char* path = "example.txt";

    // Create a file to map (mio requires the file to exist)
    {
        std::ofstream file(path);
        file << std::string(1000, 'x');
    }

    std::error_code ec;

    // Create a read-write mapping
    mio::mmap_sink rw_mmap = mio::make_mmap_sink(path, 0, mio::map_entire_file, ec);
    if (ec) {
        std::printf("Error: %s\n", ec.message().c_str());
        return 1;
    }

    // Use iterator-based algorithms
    std::fill(rw_mmap.begin(), rw_mmap.end(), 'a');

    // Use range-based for loop
    for (auto& byte : rw_mmap) {
        byte += 1;  // 'a' -> 'b'
    }

    // Use subscript operator
    rw_mmap[0] = 'Z';

    // Sync and unmap
    rw_mmap.sync(ec);
    rw_mmap.unmap();

    // Verify with read-only mapping
    mio::mmap_source ro_mmap;
    ro_mmap.map(path, ec);
    if (ec) { return 1; }

    assert(ro_mmap[0] == 'Z');
    assert(ro_mmap[1] == 'b');

    std::printf("Success!\n");
    return 0;
}
```

## CMake Integration

### Using FetchContent (Recommended)

The simplest way to integrate mio into your CMake project:

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyProject)

include(FetchContent)

FetchContent_Declare(
    mio
    GIT_REPOSITORY https://github.com/your-username/mio.git
    GIT_TAG        master  # or a specific tag/commit
)
FetchContent_MakeAvailable(mio)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE mio::mio)
```

### Using find_package

If mio is installed on your system:

```cmake
find_package(mio REQUIRED)
target_link_libraries(my_app PRIVATE mio::mio)
```

### As a Subdirectory

Copy or add mio as a git submodule to your project:

```cmake
add_subdirectory(external/mio)
target_link_libraries(my_app PRIVATE mio::mio)
```

### Windows API Targets

On Windows, `mio::mio` defines `WIN32_LEAN_AND_MEAN` and `NOMINMAX` to minimize the Windows API surface and prevent macro conflicts. If this causes issues, use the alternative targets:

```cmake
# Full Windows API (no WIN32_LEAN_AND_MEAN or NOMINMAX)
target_link_libraries(my_app PRIVATE mio::mio_full_winapi)

# Minimal Windows API (explicit WIN32_LEAN_AND_MEAN and NOMINMAX)
target_link_libraries(my_app PRIVATE mio::mio_min_winapi)
```

## Single Header

A single-header version is available at `single_include/mio/mio.hpp`:

```cpp
#include <mio/mio.hpp>  // single header version
```

The single header is regenerated automatically during CMake configuration, or manually via:

```sh
cmake --build build --target mio_amalgamate
```

## Building and Testing

```sh
mkdir build && cd build
cmake -DMIO_BUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `MIO_BUILD_TESTS` | `ON` (standalone) | Build test suite |
| `MIO_BUILD_SINGLE_HEADER` | `ON` (standalone) | Generate single-header amalgamation |
| `MIO_WINDOWS_FULL_API` | `OFF` | Disable WIN32_LEAN_AND_MEAN and NOMINMAX |
| `MIO_INSTALL` | `ON` (standalone) | Enable installation targets |

## API Reference

### Type Aliases

```cpp
// Read-only mappings
using mmap_source = basic_mmap_source<char>;
using ummap_source = basic_mmap_source<unsigned char>;
using bmmap_source = basic_mmap_source<std::byte>;

// Read-write mappings
using mmap_sink = basic_mmap_sink<char>;
using ummap_sink = basic_mmap_sink<unsigned char>;
using bmmap_sink = basic_mmap_sink<std::byte>;

// Shared ownership variants
using shared_mmap_source = basic_shared_mmap_source<char>;
using shared_mmap_sink = basic_shared_mmap_sink<char>;
// ... and unsigned char / std::byte variants
```

### Constants

```cpp
inline constexpr size_t map_entire_file = 0;  // Pass as length to map entire file
inline constexpr auto invalid_handle = ...;   // Platform-specific invalid handle value
```

### Page Size

```cpp
#include <mio/page.hpp>

size_t page = mio::page_size();  // System page allocation granularity
```

## Error Handling

mio provides two error handling mechanisms:

### Exception-based (Constructors)

Constructors throw `std::system_error` on failure:

```cpp
try {
    mio::mmap_source mmap("nonexistent.txt");
} catch (const std::system_error& e) {
    std::cerr << "Mapping failed: " << e.what() << "\n";
    std::cerr << "Error code: " << e.code().value() << "\n";
}
```

### Error code-based (Factory functions and map())

Factory functions and `map()` report errors via `std::error_code`:

```cpp
std::error_code ec;
mio::mmap_source mmap;
mmap.map("file.txt", ec);

if (ec) {
    std::cerr << "Mapping failed: " << ec.message() << "\n";
    // ec.value() contains the system error code
    // ec.category() identifies the error category
}
```

### Common Error Conditions

| Condition | Cause |
|-----------|-------|
| `std::errc::no_such_file_or_directory` | File does not exist |
| `std::errc::permission_denied` | Insufficient permissions |
| `std::errc::invalid_argument` | Empty path, null pointer, or invalid offset |
| `std::errc::bad_file_descriptor` | Invalid file handle |
| `std::errc::not_enough_memory` | System cannot allocate mapping |

### Checking Mapping State

```cpp
mio::mmap_source mmap;
mmap.map("file.txt", ec);

if (mmap.is_open()) {
    // Mapping is valid and can be used
}

if (mmap.empty()) {
    // No data mapped (size == 0)
}
```

## Undefined Behavior

The following operations result in undefined behavior and must be avoided:

### Accessing Unmapped Memory

```cpp
mio::mmap_source mmap;
// UB: mmap is not mapped
char c = mmap[0];        // UB
auto* p = mmap.data();   // Returns nullptr, dereferencing is UB
```

Always check `is_open()` before accessing data:

```cpp
if (mmap.is_open()) {
    char c = mmap[0];  // OK
}
```

### Out-of-Bounds Access

```cpp
mio::mmap_source mmap("file.txt");  // file has 100 bytes
char c = mmap[100];  // UB: valid indices are 0-99
```

### Using Mapping After Unmap

```cpp
mio::mmap_source mmap("file.txt");
const char* ptr = mmap.data();
mmap.unmap();
char c = *ptr;  // UB: ptr is dangling
```

### File Modification During Mapping

Modifying a file's size while it is memory mapped leads to undefined behavior:

```cpp
mio::mmap_source mmap("file.txt");
// UB: truncating or extending the file while mapped
truncate("file.txt", 0);
```

### Mismatched Access Modes

Using a file handle with different permissions than the mapping mode:

```cpp
int fd = open("file.txt", O_RDONLY);
// Potential failure or UB: handle is read-only but sink requires write
mio::mmap_sink mmap(fd, 0, mio::map_entire_file);
```

### Null Path to Constructor

```cpp
const char* path = nullptr;
mio::mmap_source mmap(path);  // UB: null pointer
```

Use factory functions or `map()` with error codes for safe handling:

```cpp
std::error_code ec;
mio::mmap_source mmap;
mmap.map(static_cast<const char*>(nullptr), ec);
// ec will be set to std::errc::invalid_argument
```

### Thread Safety

mio mappings are not thread-safe. Concurrent access to the same `mmap` object from multiple threads without synchronization is undefined behavior. However, multiple threads can safely read from the same mapped memory region if the `mmap` object itself is not modified.

```cpp
mio::mmap_source mmap("file.txt");

// OK: Multiple threads reading mapped data
std::thread t1([&]{ auto c = mmap[0]; });
std::thread t2([&]{ auto c = mmap[1]; });

// UB: One thread modifying mmap object while others use it
std::thread t3([&]{ mmap.unmap(); });  // UB if t1/t2 still running
```

## Platform Notes

### Windows

- Wide character paths are supported via `std::filesystem::path`
- UTF-8 encoded paths are automatically converted
- File handles (`HANDLE`) can be used directly

### macOS

- Requires macOS 10.15+ (Catalina) for `std::filesystem` support
- Uses standard POSIX mmap APIs

### Linux

- Uses standard POSIX mmap APIs
- Works with any file descriptor

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

This library is based on [mio](https://github.com/mandreyel/mio) by [mandreyel](https://github.com/mandreyel). The original implementation provided the foundation for this modernized C++17 version.

Changes from the original:
- Upgraded minimum C++ standard from C++11 to C++17
- Replaced SFINAE patterns with `if constexpr` and `static_assert`
- Added `std::filesystem::path` support (required)
- Added `std::byte` type aliases
- Added `std::span` conversion (C++20)
- Added `[[nodiscard]]` attributes
- Simplified string handling using `std::string_view`
- Replaced Python amalgamation script with pure CMake solution
- Modernized CMake build system
- Added GitHub Actions CI for Linux, macOS, and Windows
