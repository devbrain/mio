/* Copyright 2017 https://github.com/mandreyel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MIO_MMAP_HEADER
#define MIO_MMAP_HEADER

// -----------------------------------------------------------------------------
// mmap.hpp - Cross-platform memory-mapped file I/O
// -----------------------------------------------------------------------------
//
// This header provides a modern C++17 interface for memory-mapped file I/O.
// Memory mapping allows a file to be mapped directly into virtual memory,
// enabling efficient random access to file contents without explicit read/write
// calls. The OS handles paging data in and out transparently.
//
// Key features:
// - Cross-platform: Works on Windows, Linux, macOS
// - Header-only: No compilation required
// - Move-only semantics: Zero-cost abstraction with no heap allocation
// - Automatic page alignment: Users can specify any offset
// - STL-compatible iterators: Works with standard algorithms
// - Exception-safe: Offers both throwing and non-throwing APIs
//
// Basic usage:
//   // Read-only mapping
//   mio::mmap_source file("data.bin");
//   for (char c : file) { ... }
//
//   // Read-write mapping
//   mio::mmap_sink file("data.bin");
//   file[0] = 'X';
//   file.sync();  // Flush changes to disk
//
//   // Error handling without exceptions
//   std::error_code ec;
//   auto file = mio::make_mmap_source("data.bin", ec);
//   if (ec) { handle_error(ec); }
//
// Thread safety:
//   Individual mmap objects are not thread-safe. Concurrent access to the same
//   mmap object requires external synchronization. However, multiple threads
//   can safely read from the same mapped memory region if no writes occur.
//
// -----------------------------------------------------------------------------

// #include "mio/page.hpp"
/* Copyright 2017 https://github.com/mandreyel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MIO_PAGE_HEADER
#define MIO_PAGE_HEADER

// -----------------------------------------------------------------------------
// page.hpp - Memory page utilities for memory-mapped file I/O
// -----------------------------------------------------------------------------
//
// This header provides utilities for working with operating system memory pages,
// which are the fundamental units of memory management used by the virtual memory
// system. Memory mapping operations must work with page-aligned addresses.
//
// Key concepts:
// - Page size: The minimum granularity at which memory can be mapped. Typically
//   4KB on most systems, but can vary (e.g., 64KB on Windows for allocation).
// - Page alignment: Memory mapping offsets must be aligned to page boundaries
//   for the OS to accept them. This library handles alignment automatically.
//
// Usage:
//   size_t ps = mio::page_size();  // Get system page size (cached after first call)
//   size_t aligned = mio::make_offset_page_aligned(offset);  // Align to page boundary
//
// -----------------------------------------------------------------------------

#ifdef _WIN32
# include <windows.h>
#else
# include <unistd.h>
#endif

namespace mio {

/**
 * Specifies whether a memory mapping should be read-only or read-write.
 *
 * This enum is used by `basic_mmap` to determine the protection flags passed
 * to the underlying OS memory mapping APIs:
 * - On POSIX: PROT_READ vs PROT_READ|PROT_WRITE for mmap()
 * - On Windows: PAGE_READONLY vs PAGE_READWRITE for CreateFileMapping(),
 *               FILE_MAP_READ vs FILE_MAP_WRITE for MapViewOfFile()
 *
 * Note: There is no write-only mode because memory-mapped regions that are
 * writable must also be readable on most operating systems.
 */
enum class access_mode
{
    read,   ///< Read-only access. Writes to mapped memory cause segfaults.
    write   ///< Read-write access. Changes are synced to the file.
};

/**
 * Returns the operating system's page allocation granularity in bytes.
 *
 * Memory mapping operations work in units of pages. When mapping a file at a
 * specific offset, the offset must be aligned to a page boundary. This function
 * returns the page size so callers can perform alignment calculations.
 *
 * Implementation details:
 * - On Windows: Uses GetSystemInfo() to get dwAllocationGranularity (typically 64KB)
 * - On POSIX: Uses sysconf(_SC_PAGE_SIZE) (typically 4KB)
 *
 * Note: On Windows, the "allocation granularity" (64KB) differs from the actual
 * page size (4KB). Memory mapping must be aligned to allocation granularity,
 * not the page size. This function returns the correct value for mapping.
 *
 * Performance: The value is queried once on first call and cached in a static
 * variable, so subsequent calls have no syscall overhead.
 *
 * @return The system's page allocation granularity in bytes.
 */
[[nodiscard]] inline size_t page_size()
{
    // Use a lambda for static initialization - thread-safe in C++11 and later.
    // The lambda executes exactly once, and the result is cached.
    static const size_t page_size = []
    {
#ifdef _WIN32
        SYSTEM_INFO SystemInfo;
        GetSystemInfo(&SystemInfo);
        // On Windows, use allocation granularity (typically 64KB), not page size.
        // This is the minimum alignment required for MapViewOfFile offsets.
        return SystemInfo.dwAllocationGranularity;
#else
        // On POSIX systems, page size is typically 4KB but can vary.
        return sysconf(_SC_PAGE_SIZE);
#endif
    }();
    return page_size;
}

/**
 * Rounds down an offset to the nearest page-aligned boundary.
 *
 * Memory mapping APIs require the file offset to be aligned to the system's
 * page allocation granularity. This function takes an arbitrary offset and
 * returns the largest page-aligned offset that is less than or equal to it.
 *
 * Example (assuming 4KB page size):
 *   make_offset_page_aligned(0)     -> 0
 *   make_offset_page_aligned(100)   -> 0
 *   make_offset_page_aligned(4096)  -> 4096
 *   make_offset_page_aligned(5000)  -> 4096
 *   make_offset_page_aligned(8192)  -> 8192
 *
 * The library uses this internally to handle user-specified offsets that may
 * not be page-aligned. When mapping at offset N:
 * 1. Compute aligned_offset = make_offset_page_aligned(N)
 * 2. Map starting at aligned_offset
 * 3. Return pointer adjusted by (N - aligned_offset) to user
 *
 * This allows users to specify any offset without worrying about alignment.
 *
 * @param offset The byte offset to align (from start of file).
 * @return The largest page-aligned offset <= the input offset.
 */
[[nodiscard]] inline size_t make_offset_page_aligned(size_t offset) noexcept
{
    const size_t page_size_ = page_size();
    // Integer division truncates toward zero, effectively rounding down.
    // Then multiply back to get the aligned value.
    // Example: offset=5000, page_size=4096 -> 5000/4096=1 -> 1*4096=4096
    return offset / page_size_ * page_size_;
}

} // namespace mio

#endif // MIO_PAGE_HEADER


#include <iterator>
#include <string>
#include <system_error>
#include <cstdint>
#include <cstddef>
#include <filesystem>

#if __cplusplus >= 202002L
#include <span>
#endif

#ifdef _WIN32
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif // WIN32_LEAN_AND_MEAN
# include <windows.h>
#else // ifdef _WIN32
// Define INVALID_HANDLE_VALUE for POSIX systems to simplify cross-platform code.
// On POSIX, file descriptors are integers, and -1 indicates an invalid handle.
# define INVALID_HANDLE_VALUE -1
#endif // ifdef _WIN32

namespace mio {

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

/**
 * Sentinel value to indicate that the entire file should be mapped.
 *
 * When passed as the `length` parameter to map() or constructors, this causes
 * the mapping to extend from the specified offset to the end of the file.
 *
 * Example:
 *   mio::mmap_source file("data.bin", 100, mio::map_entire_file);
 *   // Maps from byte 100 to end of file
 */
inline constexpr size_t map_entire_file = 0;

// -----------------------------------------------------------------------------
// Platform-specific types
// -----------------------------------------------------------------------------

#ifdef _WIN32
/**
 * On Windows, file handles are HANDLE (typedef for void*).
 * Use CreateFile/OpenFile to obtain a handle, CloseHandle to release.
 */
using file_handle_type = HANDLE;
#else
/**
 * On POSIX systems (Linux, macOS, etc.), file handles are integers (file descriptors).
 * Use open() to obtain a descriptor, close() to release.
 */
using file_handle_type = int;
#endif

/**
 * Sentinel value representing an invalid file handle.
 *
 * Can be used to check if a file handle is valid:
 *   if (handle != mio::invalid_handle) { ... }
 *
 * Platform details:
 * - Windows: INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
 * - POSIX: -1
 *
 * Note: On Windows, this is `const` rather than `constexpr` because
 * INVALID_HANDLE_VALUE involves a cast that MSVC doesn't allow in constexpr.
 */
#ifdef _WIN32
inline const file_handle_type invalid_handle = INVALID_HANDLE_VALUE;
#else
inline constexpr file_handle_type invalid_handle = -1;
#endif

// -----------------------------------------------------------------------------
// basic_mmap - Core memory mapping class
// -----------------------------------------------------------------------------

/**
 * A memory-mapped file region with configurable access mode and byte type.
 *
 * This class template provides direct memory access to file contents via the
 * operating system's virtual memory facilities. The mapped region appears as
 * a contiguous array of bytes that can be accessed through pointers, iterators,
 * or the subscript operator.
 *
 * Template parameters:
 * @tparam AccessMode Either `access_mode::read` for read-only mappings or
 *                    `access_mode::write` for read-write mappings.
 * @tparam ByteT      The byte type for the mapped data. Must be 1 byte in size.
 *                    Common choices: char, unsigned char, std::byte.
 *
 * Ownership semantics:
 * - Move-only: Cannot be copied, but can be moved.
 * - RAII: Automatically unmaps on destruction.
 * - File handle ownership: Handles opened by this class are closed on unmap.
 *   Handles provided by the user are NOT closed.
 *
 * Memory layout:
 * - The OS maps pages starting at a page-aligned offset.
 * - data() returns a pointer adjusted to the user-requested offset.
 * - length() returns the user-requested length.
 * - mapped_length() returns the actual mapped size (includes alignment padding).
 *
 * @see basic_mmap_source, basic_mmap_sink for convenient type aliases
 * @see basic_shared_mmap for shared ownership variant
 */
template<access_mode AccessMode, typename ByteT>
struct basic_mmap
{
    // -------------------------------------------------------------------------
    // Type aliases (STL container compatibility)
    // -------------------------------------------------------------------------

    using value_type = ByteT;                              ///< The byte type (char, unsigned char, std::byte)
    using size_type = size_t;                              ///< Size and offset type
    using reference = value_type&;                         ///< Reference to a byte
    using const_reference = const value_type&;             ///< Const reference to a byte
    using pointer = value_type*;                           ///< Pointer to mapped data
    using const_pointer = const value_type*;               ///< Const pointer to mapped data
    using difference_type = std::ptrdiff_t;                ///< Signed difference between pointers
    using iterator = pointer;                              ///< Random access iterator type
    using const_iterator = const_pointer;                  ///< Const random access iterator type
    using reverse_iterator = std::reverse_iterator<iterator>;              ///< Reverse iterator
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;  ///< Const reverse iterator
    using iterator_category = std::random_access_iterator_tag;             ///< Iterator category tag
    using handle_type = file_handle_type;                  ///< Platform file handle type

    // Ensure ByteT is exactly 1 byte to maintain correct pointer arithmetic
    static_assert(sizeof(ByteT) == sizeof(char), "ByteT must be the same size as char.");

private:
    // -------------------------------------------------------------------------
    // Member variables
    // -------------------------------------------------------------------------

    // Pointer to the first user-requested byte. This is offset from the actual
    // mapping start to account for page alignment adjustments.
    pointer data_ = nullptr;

    // User-requested length (may be less than the actual mapped length due to
    // page alignment requirements).
    size_type length_ = 0;

    // Actual mapped length, which includes any padding needed to align the
    // start of the mapping to a page boundary. Always >= length_.
    size_type mapped_length_ = 0;

    // The file handle used for the mapping. On Windows, this is the original
    // file handle (HANDLE); on POSIX, this is the file descriptor (int).
    handle_type file_handle_ = INVALID_HANDLE_VALUE;

#ifdef _WIN32
    // Windows requires a separate "file mapping object" handle created via
    // CreateFileMapping(). This handle is used for MapViewOfFile() and must
    // be closed separately from the file handle.
    handle_type file_mapping_handle_ = INVALID_HANDLE_VALUE;
#endif

    // Tracks whether this object owns the file handle (and should close it).
    // True when map() was called with a path (we opened the file).
    // False when map() was called with an existing handle (user owns it).
    bool is_handle_internal_ = false;

public:
    // -------------------------------------------------------------------------
    // Constructors and destructor
    // -------------------------------------------------------------------------

    /**
     * Default constructor. Creates an unmapped object.
     *
     * An unmapped object has is_open() == false and empty() == true.
     * Accessing data on an unmapped object is undefined behavior.
     *
     * Use map() or a factory function to establish a mapping before use.
     */
    basic_mmap() = default;

#ifdef __cpp_exceptions
    /**
     * Constructs a mapping from a file path. Throws on failure.
     *
     * This constructor opens the file, creates a memory mapping, and closes
     * the file handle when unmapped (or on destruction). The file must exist.
     *
     * @param path   Path to the file to map. Supports std::filesystem::path.
     * @param offset Byte offset from the start of the file (default: 0).
     *               Does not need to be page-aligned; alignment is handled internally.
     * @param length Number of bytes to map, or `map_entire_file` for the whole file.
     *
     * @throws std::system_error If the file cannot be opened or mapped.
     *         The error code indicates the specific failure reason.
     *
     * Example:
     *   try {
     *       mio::mmap_source file("data.bin");
     *   } catch (const std::system_error& e) {
     *       std::cerr << "Mapping failed: " << e.what() << "\n";
     *   }
     */
    basic_mmap(const std::filesystem::path& path, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        map(path, offset, length, error);
        if(error) { throw std::system_error(error); }
    }

    /**
     * Constructs a mapping from an existing file handle. Throws on failure.
     *
     * The file handle must be valid and opened with appropriate access rights
     * (read for mmap_source, read-write for mmap_sink). The handle is NOT
     * closed by this object; the caller retains ownership.
     *
     * @param handle Valid file handle (HANDLE on Windows, int fd on POSIX).
     * @param offset Byte offset from the start of the file (default: 0).
     * @param length Number of bytes to map, or `map_entire_file` for the whole file.
     *
     * @throws std::system_error If the mapping cannot be created.
     *
     * Example (POSIX):
     *   int fd = open("data.bin", O_RDONLY);
     *   mio::mmap_source file(fd, 0, mio::map_entire_file);
     *   // file does NOT close fd; caller must close(fd) when done
     */
    basic_mmap(const handle_type handle, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        map(handle, offset, length, error);
        if(error) { throw std::system_error(error); }
    }
#endif // __cpp_exceptions

    /**
     * Deleted copy constructor. basic_mmap is move-only.
     *
     * Use std::move() to transfer ownership, or use shared_mmap for
     * shared ownership semantics.
     */
    basic_mmap(const basic_mmap&) = delete;

    /**
     * Move constructor. Transfers ownership of the mapping.
     *
     * After the move, the source object is in an unmapped state
     * (is_open() == false). The destination object takes ownership
     * of all resources.
     */
    basic_mmap(basic_mmap&&);

    /** Deleted copy assignment. basic_mmap is move-only. */
    basic_mmap& operator=(const basic_mmap&) = delete;

    /**
     * Move assignment. Transfers ownership of the mapping.
     *
     * If this object currently holds a mapping, it is unmapped first.
     * Then ownership is transferred from the source object.
     */
    basic_mmap& operator=(basic_mmap&&);

    /**
     * Destructor. Unmaps the memory region and releases resources.
     *
     * For write-mode mappings, sync() is called first to flush pending
     * changes to disk (errors are silently ignored in destructor).
     *
     * If the file handle was opened internally (via path), it is closed.
     * User-provided handles are NOT closed.
     */
    ~basic_mmap();

    // -------------------------------------------------------------------------
    // Handle accessors
    // -------------------------------------------------------------------------

    /**
     * Returns the file handle used for the mapping.
     *
     * On POSIX, this is the file descriptor passed to mmap().
     * On Windows, this is the file handle passed to CreateFileMapping().
     *
     * @return The file handle, or `invalid_handle` if not mapped.
     */
    [[nodiscard]] handle_type file_handle() const noexcept { return file_handle_; }

    /**
     * Returns the mapping-specific handle.
     *
     * On POSIX, this is the same as file_handle() (no separate mapping handle).
     * On Windows, this is the file mapping object handle created by
     * CreateFileMapping(), which is separate from the file handle.
     *
     * @return The mapping handle, or `invalid_handle` if not mapped.
     */
    [[nodiscard]] handle_type mapping_handle() const noexcept;

    // -------------------------------------------------------------------------
    // State queries
    // -------------------------------------------------------------------------

    /**
     * Returns true if a valid memory mapping exists.
     *
     * An open mapping has accessible data via data(), begin(), end(), and
     * operator[]. Accessing data on a closed mapping is undefined behavior.
     *
     * @return true if the mapping is open and valid.
     */
    [[nodiscard]] bool is_open() const noexcept { return file_handle_ != invalid_handle; }

    /**
     * Returns true if the mapping is empty (zero length).
     *
     * This is provided for STL container compatibility. An empty mapping
     * has size() == 0. Note that a mapping can be open but empty.
     *
     * @return true if length() == 0.
     */
    [[nodiscard]] bool empty() const noexcept { return length() == 0; }

    /**
     * Returns true if memory has been mapped.
     *
     * On Windows, this checks if the file mapping object was created.
     * On POSIX, this is equivalent to is_open().
     *
     * @return true if memory mapping exists.
     */
    [[nodiscard]] bool is_mapped() const noexcept;

    // -------------------------------------------------------------------------
    // Size queries
    // -------------------------------------------------------------------------

    /**
     * Returns the logical size of the mapped region in bytes.
     *
     * This is the number of bytes the user requested to map, which is
     * what you can safely access via the iterators and operator[].
     *
     * Alias for length(), provided for STL container compatibility.
     *
     * @return The number of accessible bytes.
     */
    [[nodiscard]] size_type size() const noexcept { return length(); }

    /**
     * Returns the logical length of the mapped region in bytes.
     *
     * This is the user-requested length, not the actual mapped length
     * (which may be larger due to page alignment requirements).
     *
     * @return The number of accessible bytes.
     */
    [[nodiscard]] size_type length() const noexcept { return length_; }

    /**
     * Returns the actual mapped length in bytes.
     *
     * This may be larger than length() because:
     * 1. The mapping must start at a page-aligned offset.
     * 2. The user's requested offset might not be page-aligned.
     * 3. Extra bytes before the user's offset are included in the mapping.
     *
     * The difference (mapped_length() - length()) equals mapping_offset().
     *
     * @return The actual number of bytes mapped (>= length()).
     */
    [[nodiscard]] size_type mapped_length() const noexcept { return mapped_length_; }

    /**
     * Returns the offset from the actual mapping start to the user's data.
     *
     * When the user requests an offset that isn't page-aligned, the actual
     * mapping starts at the previous page boundary. This function returns
     * the number of "padding" bytes between the mapping start and data().
     *
     * For example, if page_size is 4096 and user requests offset 100:
     * - Actual mapping starts at offset 0
     * - data() points to offset 100
     * - mapping_offset() returns 100
     *
     * @return Bytes between actual mapping start and user data start.
     */
    [[nodiscard]] size_type mapping_offset() const noexcept
    {
        return mapped_length_ - length_;
    }

    // -------------------------------------------------------------------------
    // Data access
    // -------------------------------------------------------------------------

    /**
     * Returns a pointer to the first byte of the mapped region.
     *
     * The pointer is adjusted to the user's requested offset, not the
     * actual page-aligned mapping start.
     *
     * Non-const version requires write access mode (compile-time check).
     *
     * @return Pointer to mapped data, or nullptr if not mapped.
     */
    [[nodiscard]] pointer data() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const data() requires write access");
        return data_;
    }

    /** @copydoc data() */
    [[nodiscard]] const_pointer data() const noexcept { return data_; }

    // -------------------------------------------------------------------------
    // Iterator access (STL compatibility)
    // -------------------------------------------------------------------------

    /**
     * Returns an iterator to the first byte of the mapped region.
     *
     * Undefined behavior if called on an unmapped object.
     * Non-const version requires write access mode.
     *
     * @return Iterator to the first byte.
     */
    [[nodiscard]] iterator begin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const begin() requires write access");
        return data();
    }
    /** @copydoc begin() */
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    /** @copydoc begin() */
    [[nodiscard]] const_iterator cbegin() const noexcept { return data(); }

    /**
     * Returns an iterator one past the last byte of the mapped region.
     *
     * Undefined behavior if called on an unmapped object.
     * Non-const version requires write access mode.
     *
     * @return Iterator past the last byte.
     */
    [[nodiscard]] iterator end() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const end() requires write access");
        return data() + length();
    }
    /** @copydoc end() */
    [[nodiscard]] const_iterator end() const noexcept { return data() + length(); }
    /** @copydoc end() */
    [[nodiscard]] const_iterator cend() const noexcept { return data() + length(); }

    /**
     * Returns a reverse iterator to the last byte of the mapped region.
     *
     * Undefined behavior if called on an unmapped object.
     * Non-const version requires write access mode.
     *
     * @return Reverse iterator to the last byte.
     */
    [[nodiscard]] reverse_iterator rbegin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rbegin() requires write access");
        return reverse_iterator(end());
    }
    /** @copydoc rbegin() */
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    { return const_reverse_iterator(end()); }
    /** @copydoc rbegin() */
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept
    { return const_reverse_iterator(end()); }

    /**
     * Returns a reverse iterator before the first byte of the mapped region.
     *
     * Undefined behavior if called on an unmapped object.
     * Non-const version requires write access mode.
     *
     * @return Reverse iterator before the first byte.
     */
    [[nodiscard]] reverse_iterator rend() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rend() requires write access");
        return reverse_iterator(begin());
    }
    /** @copydoc rend() */
    [[nodiscard]] const_reverse_iterator rend() const noexcept
    { return const_reverse_iterator(begin()); }
    /** @copydoc rend() */
    [[nodiscard]] const_reverse_iterator crend() const noexcept
    { return const_reverse_iterator(begin()); }

    // -------------------------------------------------------------------------
    // Element access
    // -------------------------------------------------------------------------

    /**
     * Returns a reference to the byte at the given index.
     *
     * No bounds checking is performed. Accessing index >= size() is
     * undefined behavior. Calling on an unmapped object is undefined.
     *
     * Non-const version requires write access mode.
     *
     * @param i Index of the byte (0-based, relative to user's offset).
     * @return Reference to the byte at index i.
     */
    [[nodiscard]] reference operator[](const size_type i) noexcept {
        static_assert(AccessMode == access_mode::write, "non-const operator[] requires write access");
        return data_[i];
    }
    /** @copydoc operator[]() */
    [[nodiscard]] const_reference operator[](const size_type i) const noexcept { return data_[i]; }

    // -------------------------------------------------------------------------
    // C++20 std::span support
    // -------------------------------------------------------------------------

#if __cplusplus >= 202002L
    /**
     * Returns the mapped memory as a read-only std::span.
     *
     * Available in C++20 and later. Provides a lightweight, non-owning view
     * of the mapped data that can be used with range algorithms.
     *
     * @return A span viewing the entire mapped region.
     */
    [[nodiscard]] std::span<const value_type> as_span() const noexcept {
        return {data(), length()};
    }

    /**
     * Returns the mapped memory as a mutable std::span.
     *
     * Only available for write access mode. Available in C++20 and later.
     *
     * @return A mutable span viewing the entire mapped region.
     */
    template<access_mode A = AccessMode, std::enable_if_t<A == access_mode::write, int> = 0>
    [[nodiscard]] std::span<value_type> as_span() noexcept {
        return {data(), length()};
    }
#endif

    // -------------------------------------------------------------------------
    // Mapping operations
    // -------------------------------------------------------------------------

    /**
     * Establishes a memory mapping from a file path.
     *
     * Opens the file, creates a mapping, and stores the handle internally
     * (it will be closed on unmap). If a mapping already exists, it is
     * unmapped first (but only after the new mapping succeeds, for exception
     * safety).
     *
     * @param path   Path to an existing file. Empty paths cause an error.
     * @param offset Byte offset where mapping starts. Does not need to be
     *               page-aligned; alignment is handled internally.
     * @param length Number of bytes to map, or `map_entire_file` for the
     *               rest of the file from offset.
     * @param error  Output parameter for error reporting. Set to the failure
     *               reason, or cleared on success.
     *
     * Error conditions:
     * - std::errc::invalid_argument: Empty path or offset past end of file
     * - std::errc::no_such_file_or_directory: File not found
     * - std::errc::permission_denied: Insufficient permissions
     * - Other system errors from open/mmap/CreateFileMapping
     *
     * Example:
     *   mio::mmap_source file;
     *   std::error_code ec;
     *   file.map("data.bin", 0, mio::map_entire_file, ec);
     *   if (ec) { handle_error(ec); }
     */
    void map(const std::filesystem::path& path, const size_type offset,
            const size_type length, std::error_code& error);

    /**
     * Overload for const char* paths with null-pointer checking.
     *
     * Passing nullptr to std::filesystem::path constructor is undefined
     * behavior. This overload provides safe handling by checking for null
     * first and returning an error instead of UB.
     *
     * @param path   C-string path, or nullptr (which causes an error).
     * @param offset Byte offset where mapping starts.
     * @param length Number of bytes to map, or `map_entire_file`.
     * @param error  Output parameter for error reporting.
     */
    void map(const char* path, const size_type offset,
            const size_type length, std::error_code& error)
    {
        if (!path) {
            error = std::make_error_code(std::errc::invalid_argument);
            return;
        }
        map(std::filesystem::path(path), offset, length, error);
    }

    /**
     * Maps the entire file starting at offset 0.
     *
     * Convenience overload equivalent to map(path, 0, map_entire_file, error).
     *
     * @param path  Path to an existing file.
     * @param error Output parameter for error reporting.
     */
    void map(const std::filesystem::path& path, std::error_code& error)
    {
        map(path, 0, map_entire_file, error);
    }

    /**
     * Maps the entire file from a const char* path.
     *
     * Convenience overload with null-pointer checking.
     *
     * @param path  C-string path, or nullptr (which causes an error).
     * @param error Output parameter for error reporting.
     */
    void map(const char* path, std::error_code& error)
    {
        if (!path) {
            error = std::make_error_code(std::errc::invalid_argument);
            return;
        }
        map(std::filesystem::path(path), 0, map_entire_file, error);
    }

    /**
     * Establishes a memory mapping from an existing file handle.
     *
     * The handle must be valid and opened with appropriate permissions.
     * This object does NOT take ownership of the handle; the caller is
     * responsible for closing it (but not before unmapping).
     *
     * @param handle Valid file handle (HANDLE on Windows, fd on POSIX).
     * @param offset Byte offset where mapping starts.
     * @param length Number of bytes to map, or `map_entire_file`.
     * @param error  Output parameter for error reporting.
     *
     * Error conditions:
     * - std::errc::bad_file_descriptor: Invalid handle
     * - std::errc::invalid_argument: Offset past end of file
     * - Other system errors from mmap/CreateFileMapping
     *
     * Example (POSIX):
     *   int fd = open("data.bin", O_RDONLY);
     *   mio::mmap_source file;
     *   std::error_code ec;
     *   file.map(fd, 0, mio::map_entire_file, ec);
     *   // Later: close(fd) after unmapping
     */
    void map(const handle_type handle, const size_type offset,
            const size_type length, std::error_code& error);

    /**
     * Maps the entire file from an existing handle.
     *
     * Convenience overload equivalent to map(handle, 0, map_entire_file, error).
     *
     * @param handle Valid file handle.
     * @param error  Output parameter for error reporting.
     */
    void map(const handle_type handle, std::error_code& error)
    {
        map(handle, 0, map_entire_file, error);
    }

    /**
     * Releases the memory mapping and associated resources.
     *
     * After calling unmap(), is_open() returns false and the object is
     * in the same state as a default-constructed object.
     *
     * Resource cleanup:
     * - The memory region is unmapped (UnmapViewOfFile / munmap)
     * - On Windows, the file mapping object is closed
     * - If the file handle was opened by map(path, ...), it is closed
     * - If the file handle was provided by the user, it is NOT closed
     *
     * Calling unmap() on an already-unmapped object is a no-op.
     */
    void unmap();

    /**
     * Swaps the contents of two mmap objects.
     *
     * Exchanges all internal state including the mapping, file handles,
     * and ownership flags. No mapping or unmapping occurs.
     *
     * @param other The object to swap with.
     */
    void swap(basic_mmap& other) noexcept;

    /**
     * Flushes modified pages to the underlying file.
     *
     * For write-mode mappings, this ensures that any modifications to the
     * mapped memory are written to the file on disk. The function blocks
     * until the flush completes.
     *
     * Implementation:
     * - Windows: FlushViewOfFile() + FlushFileBuffers()
     * - POSIX: msync() with MS_SYNC flag
     *
     * This is called automatically by the destructor for write mappings,
     * but you can call it explicitly for checkpointing.
     *
     * Only available for write access mode (compile-time check).
     *
     * @param error Output parameter for error reporting.
     */
    void sync(std::error_code& error);

private:
    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /**
     * Returns the actual start of the memory mapping.
     *
     * The user-visible data_ pointer is offset from the actual mapping start
     * due to page alignment. This function returns the true mapping start
     * for use with unmap and sync operations.
     *
     * @return Pointer to the actual mapping start, or nullptr if unmapped.
     */
    [[nodiscard]] pointer get_mapping_start() noexcept
    {
        return !data_ ? nullptr : data_ - mapping_offset();
    }

    /** @copydoc get_mapping_start() */
    [[nodiscard]] const_pointer get_mapping_start() const noexcept
    {
        return !data_ ? nullptr : data_ - mapping_offset();
    }

    /**
     * Conditionally syncs the mapping based on access mode.
     *
     * Called by the destructor. For write mode, calls sync() (ignoring
     * errors since we're in a destructor). For read mode, does nothing.
     *
     * Uses `if constexpr` for compile-time dispatch.
     */
    void conditional_sync();
};

// -----------------------------------------------------------------------------
// Comparison operators
// -----------------------------------------------------------------------------

/**
 * Equality comparison for mmaps.
 *
 * Two mmaps are equal if they point to the same data and have the same size.
 * This compares the data pointers and sizes, not the file contents.
 */
template<access_mode AccessMode, typename ByteT>
[[nodiscard]] bool operator==(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b);

template<access_mode AccessMode, typename ByteT>
[[nodiscard]] bool operator!=(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b);

template<access_mode AccessMode, typename ByteT>
[[nodiscard]] bool operator<(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b);

template<access_mode AccessMode, typename ByteT>
[[nodiscard]] bool operator<=(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b);

template<access_mode AccessMode, typename ByteT>
[[nodiscard]] bool operator>(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b);

template<access_mode AccessMode, typename ByteT>
[[nodiscard]] bool operator>=(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b);

// -----------------------------------------------------------------------------
// Type aliases for common use cases
// -----------------------------------------------------------------------------

/**
 * Read-only memory mapping template.
 *
 * Use this for mappings where you only need to read data. Attempting to
 * modify the mapped data will cause a segmentation fault.
 *
 * @tparam ByteT The byte type (char, unsigned char, std::byte).
 */
template<typename ByteT>
using basic_mmap_source = basic_mmap<access_mode::read, ByteT>;

/**
 * Read-write memory mapping template.
 *
 * Use this for mappings where you need to modify the file contents.
 * Changes are visible immediately in memory and are synced to disk
 * on unmap (or explicit sync()).
 *
 * @tparam ByteT The byte type (char, unsigned char, std::byte).
 */
template<typename ByteT>
using basic_mmap_sink = basic_mmap<access_mode::write, ByteT>;

// Convenient type aliases for the most common byte types:

/// Read-only mapping with char bytes (most common)
using mmap_source = basic_mmap_source<char>;

/// Read-only mapping with unsigned char bytes
using ummap_source = basic_mmap_source<unsigned char>;

/// Read-only mapping with std::byte bytes (C++17)
using bmmap_source = basic_mmap_source<std::byte>;

/// Read-write mapping with char bytes (most common)
using mmap_sink = basic_mmap_sink<char>;

/// Read-write mapping with unsigned char bytes
using ummap_sink = basic_mmap_sink<unsigned char>;

/// Read-write mapping with std::byte bytes (C++17)
using bmmap_sink = basic_mmap_sink<std::byte>;

// -----------------------------------------------------------------------------
// Factory functions
// -----------------------------------------------------------------------------

/**
 * Generic factory function for creating memory mappings.
 *
 * This is the base factory used by make_mmap_source and make_mmap_sink.
 * It can create any mmap type (including shared_mmap types).
 *
 * @tparam MMap         The mmap type to create (e.g., mmap_source, mmap_sink).
 * @tparam MappingToken The type of the mapping source (path or handle).
 *
 * @param token  File path (string, char*, filesystem::path) or handle.
 * @param offset Byte offset where mapping starts.
 * @param length Number of bytes to map, or `map_entire_file`.
 * @param error  Output parameter for error reporting.
 *
 * @return The created mmap object (empty if error occurred).
 */
template<
    typename MMap,
    typename MappingToken
> MMap make_mmap(const MappingToken& token,
        int64_t offset, int64_t length, std::error_code& error)
{
    MMap mmap;
    mmap.map(token, offset, length, error);
    return mmap;
}

/**
 * Creates a read-only memory mapping.
 *
 * Factory function that creates an mmap_source with error reporting via
 * std::error_code. Preferred over constructors when you want to avoid
 * exceptions.
 *
 * @tparam MappingToken Type that can identify a file:
 *         - std::filesystem::path
 *         - std::string, std::string_view, const char*
 *         - mmap_source::handle_type (file descriptor or HANDLE)
 *
 * @param token  The file path or handle to map.
 * @param offset Byte offset where mapping starts (automatically page-aligned).
 * @param length Number of bytes to map, or `map_entire_file`.
 * @param error  Output parameter set on failure, cleared on success.
 *
 * @return The created mmap_source (check error or is_open() for success).
 *
 * Example:
 *   std::error_code ec;
 *   auto file = mio::make_mmap_source("data.bin", 0, 1024, ec);
 *   if (!ec) {
 *       for (char c : file) { process(c); }
 *   }
 */
template<typename MappingToken>
mmap_source make_mmap_source(const MappingToken& token, mmap_source::size_type offset,
        mmap_source::size_type length, std::error_code& error)
{
    return make_mmap<mmap_source>(token, offset, length, error);
}

/**
 * Creates a read-only mapping of an entire file.
 *
 * Convenience overload that maps from offset 0 to end of file.
 *
 * @param token The file path or handle to map.
 * @param error Output parameter for error reporting.
 * @return The created mmap_source.
 */
template<typename MappingToken>
mmap_source make_mmap_source(const MappingToken& token, std::error_code& error)
{
    return make_mmap_source(token, 0, map_entire_file, error);
}

/**
 * Creates a read-write memory mapping.
 *
 * Factory function that creates an mmap_sink with error reporting via
 * std::error_code. Changes to the mapped memory will be reflected in
 * the file after sync() or unmap().
 *
 * @tparam MappingToken Type that can identify a file (path or handle).
 *
 * @param token  The file path or handle to map.
 * @param offset Byte offset where mapping starts.
 * @param length Number of bytes to map, or `map_entire_file`.
 * @param error  Output parameter for error reporting.
 *
 * @return The created mmap_sink.
 *
 * Example:
 *   std::error_code ec;
 *   auto file = mio::make_mmap_sink("output.bin", ec);
 *   if (!ec) {
 *       std::fill(file.begin(), file.end(), 0);
 *       file.sync(ec);  // Ensure changes are on disk
 *   }
 */
template<typename MappingToken>
mmap_sink make_mmap_sink(const MappingToken& token, mmap_sink::size_type offset,
        mmap_sink::size_type length, std::error_code& error)
{
    return make_mmap<mmap_sink>(token, offset, length, error);
}

/**
 * Creates a read-write mapping of an entire file.
 *
 * Convenience overload that maps from offset 0 to end of file.
 *
 * @param token The file path or handle to map.
 * @param error Output parameter for error reporting.
 * @return The created mmap_sink.
 */
template<typename MappingToken>
mmap_sink make_mmap_sink(const MappingToken& token, std::error_code& error)
{
    return make_mmap_sink(token, 0, map_entire_file, error);
}

} // namespace mio

// Include the implementation (template definitions)
// #include "detail/mmap.ipp"
/* Copyright 2017 https://github.com/mandreyel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MIO_BASIC_MMAP_IMPL
#define MIO_BASIC_MMAP_IMPL

// -----------------------------------------------------------------------------
// mmap.ipp - Implementation details for memory-mapped file I/O
// -----------------------------------------------------------------------------
//
// This file contains the implementation of basic_mmap template methods and
// platform-specific helper functions. It is included at the end of mmap.hpp.
//
// Platform abstraction:
// - Windows: Uses CreateFileMapping/MapViewOfFile/UnmapViewOfFile
// - POSIX: Uses mmap/munmap/msync
//
// The implementation handles:
// - Opening files from paths (with automatic handle management)
// - Creating memory mappings from file handles
// - Automatic page boundary alignment for arbitrary offsets
// - Flushing changes to disk (sync)
// - Proper resource cleanup on destruction or unmap
//
// -----------------------------------------------------------------------------

// #include "mio/mmap.hpp"

// #include "mio/page.hpp"

// #include "mio/detail/string_util.hpp"
/* Copyright 2017 https://github.com/mandreyel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MIO_STRING_UTIL_HEADER
#define MIO_STRING_UTIL_HEADER

#include <string_view>
#include <filesystem>

namespace mio {
namespace detail {

// std::string_view overloads (handles std::string, const char*, string literals)
[[nodiscard]] inline const char* c_str(std::string_view sv) noexcept
{
    return sv.data();
}

[[nodiscard]] inline bool empty(std::string_view sv) noexcept
{
    return sv.empty();
}

// std::filesystem::path overloads
[[nodiscard]] inline auto c_str(const std::filesystem::path& p)
{
    return p.c_str();
}

[[nodiscard]] inline bool empty(const std::filesystem::path& p)
{
    return p.empty();
}

#ifdef _WIN32
// Windows wide string support
[[nodiscard]] inline const wchar_t* c_str(std::wstring_view sv) noexcept
{
    return sv.data();
}

[[nodiscard]] inline bool empty(std::wstring_view sv) noexcept
{
    return sv.empty();
}
#endif // _WIN32

} // namespace detail
} // namespace mio

#endif // MIO_STRING_UTIL_HEADER


#include <algorithm>
#include <filesystem>

#ifndef _WIN32
# include <unistd.h>
# include <fcntl.h>
# include <sys/mman.h>
# include <sys/stat.h>
#endif

namespace mio {
namespace detail {

// -----------------------------------------------------------------------------
// Windows-specific helpers
// -----------------------------------------------------------------------------

#ifdef _WIN32
namespace win {

/**
 * Extracts the upper 32 bits of a 64-bit integer.
 *
 * Windows APIs often split 64-bit values into separate high/low DWORD
 * parameters (e.g., SetFilePointer, MapViewOfFile). This helper extracts
 * the upper 32 bits for such APIs.
 *
 * @param n A 64-bit integer value.
 * @return The upper 32 bits as a DWORD.
 */
inline DWORD int64_high(int64_t n) noexcept
{
    return n >> 32;
}

/**
 * Extracts the lower 32 bits of a 64-bit integer.
 *
 * @param n A 64-bit integer value.
 * @return The lower 32 bits as a DWORD.
 */
inline DWORD int64_low(int64_t n) noexcept
{
    return n & 0xffffffff;
}

/**
 * Opens a file for memory mapping on Windows.
 *
 * Uses CreateFileW to support Unicode paths via std::filesystem::path.
 * The file is opened with sharing enabled for both reads and writes,
 * allowing other processes to access the file while it's mapped.
 *
 * @param path The file path (Unicode-aware via std::filesystem).
 * @param mode Whether to open for read-only or read-write access.
 * @return The file handle, or INVALID_HANDLE_VALUE on failure.
 */
inline file_handle_type open_file_helper(const std::filesystem::path& path, const access_mode mode)
{
    return ::CreateFileW(
            path.c_str(),  // Use wide string for Unicode support
            mode == access_mode::read ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,  // Allow others to read/write
            0,                                    // Default security attributes
            OPEN_EXISTING,                        // File must exist
            FILE_ATTRIBUTE_NORMAL,                // No special flags
            0);                                   // No template file
}

} // namespace win
#endif // _WIN32

// -----------------------------------------------------------------------------
// Cross-platform helpers
// -----------------------------------------------------------------------------

/**
 * Returns the last system error as a std::error_code.
 *
 * This should be called immediately after a failed system call to capture
 * the error before it's overwritten by subsequent calls.
 *
 * Implementation:
 * - Windows: GetLastError() with system_category
 * - POSIX: errno with system_category
 *
 * @return The last system error wrapped in std::error_code.
 */
inline std::error_code last_error() noexcept
{
    std::error_code error;
#ifdef _WIN32
    error.assign(GetLastError(), std::system_category());
#else
    error.assign(errno, std::system_category());
#endif
    return error;
}

/**
 * Opens a file and returns its handle.
 *
 * Platform-independent file opening with appropriate access permissions.
 * The caller is responsible for closing the returned handle.
 *
 * @param path  Path to the file to open.
 * @param mode  Access mode (read or read-write).
 * @param error Output parameter for error reporting.
 * @return The file handle, or invalid_handle on failure.
 */
inline file_handle_type open_file(const std::filesystem::path& path, const access_mode mode,
        std::error_code& error)
{
    error.clear();

    // Check for empty path before attempting to open
    if(path.empty())
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return invalid_handle;
    }

#ifdef _WIN32
    const auto handle = win::open_file_helper(path, mode);
#else // POSIX
    // O_RDONLY for read mode, O_RDWR for write mode (mmap requires read access)
    const auto handle = ::open(path.c_str(),
            mode == access_mode::read ? O_RDONLY : O_RDWR);
#endif

    if(handle == invalid_handle)
    {
        error = detail::last_error();
    }
    return handle;
}

/**
 * Queries the size of a file in bytes.
 *
 * Used to determine the mapping length when map_entire_file is specified.
 *
 * @param handle Valid file handle.
 * @param error  Output parameter for error reporting.
 * @return The file size in bytes, or 0 on error.
 */
inline size_t query_file_size(file_handle_type handle, std::error_code& error)
{
    error.clear();

#ifdef _WIN32
    LARGE_INTEGER file_size;
    if(::GetFileSizeEx(handle, &file_size) == 0)
    {
        error = detail::last_error();
        return 0;
    }
	return static_cast<size_t>(file_size.QuadPart);
#else // POSIX
    struct stat sbuf;
    if(::fstat(handle, &sbuf) == -1)
    {
        error = detail::last_error();
        return 0;
    }
    return sbuf.st_size;
#endif
}

/**
 * Result structure for memory_map().
 *
 * Contains all the information needed to populate a basic_mmap after
 * a successful mapping operation.
 */
struct mmap_context
{
    char* data;              ///< Pointer to user's requested offset (not mapping start)
    int64_t length;          ///< User-requested length
    int64_t mapped_length;   ///< Actual mapped length (>= length due to alignment)
#ifdef _WIN32
    file_handle_type file_mapping_handle;  ///< Windows file mapping object handle
#endif
};

/**
 * Creates a memory mapping of a file region.
 *
 * This is the core memory mapping function that interfaces with the OS.
 * It handles page alignment automatically: if the requested offset is not
 * page-aligned, the mapping starts at the previous page boundary, and the
 * returned data pointer is adjusted to the user's requested offset.
 *
 * Memory mapping process:
 * 1. Calculate page-aligned offset (round down to page boundary)
 * 2. Adjust mapping length to include bytes from aligned offset to user offset
 * 3. Create the mapping via OS API
 * 4. Return pointer adjusted to user's requested offset
 *
 * @param file_handle Valid file handle.
 * @param offset      User-requested byte offset (will be page-aligned internally).
 * @param length      Number of bytes to map.
 * @param mode        Read or read-write access.
 * @param error       Output parameter for error reporting.
 * @return mmap_context with mapping details, or empty context on failure.
 */
inline mmap_context memory_map(const file_handle_type file_handle, const int64_t offset,
    const int64_t length, const access_mode mode, std::error_code& error)
{
    // Round down offset to page boundary for OS mapping requirement
    const int64_t aligned_offset = make_offset_page_aligned(offset);

    // Actual length to map includes bytes from aligned_offset to offset
    const int64_t length_to_map = offset - aligned_offset + length;

#ifdef _WIN32
    // Windows requires the maximum file size for the mapping
    const int64_t max_file_size = offset + length;

    // Step 1: Create a file mapping object
    // This is a Windows-specific intermediate object between file and view
    const auto file_mapping_handle = ::CreateFileMapping(
            file_handle,
            0,  // Default security
            mode == access_mode::read ? PAGE_READONLY : PAGE_READWRITE,
            win::int64_high(max_file_size),  // Upper 32 bits of size
            win::int64_low(max_file_size),   // Lower 32 bits of size
            0);  // No name (anonymous mapping)

    if(file_mapping_handle == invalid_handle)
    {
        error = detail::last_error();
        return {};
    }

    // Step 2: Map a view of the file into memory
    char* mapping_start = static_cast<char*>(::MapViewOfFile(
            file_mapping_handle,
            mode == access_mode::read ? FILE_MAP_READ : FILE_MAP_WRITE,
            win::int64_high(aligned_offset),  // Upper 32 bits of offset
            win::int64_low(aligned_offset),   // Lower 32 bits of offset
            length_to_map));

    if(mapping_start == nullptr)
    {
        // Clean up file mapping handle on failure
        ::CloseHandle(file_mapping_handle);
        error = detail::last_error();
        return {};
    }
#else // POSIX
    // POSIX mmap is simpler - maps file directly to memory
    char* mapping_start = static_cast<char*>(::mmap(
            0,  // Let OS choose mapping address (no hint)
            length_to_map,
            mode == access_mode::read ? PROT_READ : PROT_READ | PROT_WRITE,
            MAP_SHARED,      // Changes are shared with other processes
            file_handle,
            aligned_offset));

    if(mapping_start == MAP_FAILED)
    {
        error = detail::last_error();
        return {};
    }
#endif

    // Build result with adjusted pointer
    // User gets pointer to their requested offset, not the page-aligned start
    mmap_context ctx;
    ctx.data = mapping_start + offset - aligned_offset;  // Adjust for alignment
    ctx.length = length;
    ctx.mapped_length = length_to_map;
#ifdef _WIN32
    ctx.file_mapping_handle = file_mapping_handle;
#endif
    return ctx;
}

} // namespace detail

// -----------------------------------------------------------------------------
// basic_mmap implementation
// -----------------------------------------------------------------------------

/**
 * Destructor implementation.
 *
 * For write-mode mappings, syncs changes to disk first (errors ignored
 * since destructors shouldn't throw). Then unmaps the file region.
 */
template<access_mode AccessMode, typename ByteT>
basic_mmap<AccessMode, ByteT>::~basic_mmap()
{
    conditional_sync();
    unmap();
}

/**
 * Move constructor implementation.
 *
 * Transfers ownership of all resources from other to this.
 * After the move, other is left in an unmapped state.
 */
template<access_mode AccessMode, typename ByteT>
basic_mmap<AccessMode, ByteT>::basic_mmap(basic_mmap&& other)
    : data_(std::move(other.data_))
    , length_(std::move(other.length_))
    , mapped_length_(std::move(other.mapped_length_))
    , file_handle_(std::move(other.file_handle_))
#ifdef _WIN32
    , file_mapping_handle_(std::move(other.file_mapping_handle_))
#endif
    , is_handle_internal_(std::move(other.is_handle_internal_))
{
    // Reset source to unmapped state to prevent double-free
    other.data_ = nullptr;
    other.length_ = other.mapped_length_ = 0;
    other.file_handle_ = invalid_handle;
#ifdef _WIN32
    other.file_mapping_handle_ = invalid_handle;
#endif
}

/**
 * Move assignment implementation.
 *
 * Releases any existing mapping before taking ownership of other's resources.
 */
template<access_mode AccessMode, typename ByteT>
basic_mmap<AccessMode, ByteT>&
basic_mmap<AccessMode, ByteT>::operator=(basic_mmap&& other)
{
    if(this != &other)
    {
        // Release current mapping first
        unmap();

        // Transfer ownership
        data_ = std::move(other.data_);
        length_ = std::move(other.length_);
        mapped_length_ = std::move(other.mapped_length_);
        file_handle_ = std::move(other.file_handle_);
#ifdef _WIN32
        file_mapping_handle_ = std::move(other.file_mapping_handle_);
#endif
        is_handle_internal_ = std::move(other.is_handle_internal_);

        // Reset source to prevent double-free on destruction
        other.data_ = nullptr;
        other.length_ = other.mapped_length_ = 0;
        other.file_handle_ = invalid_handle;
#ifdef _WIN32
        other.file_mapping_handle_ = invalid_handle;
#endif
        other.is_handle_internal_ = false;
    }
    return *this;
}

/**
 * Returns the mapping handle.
 *
 * On POSIX, this is the same as the file handle.
 * On Windows, this is the separate file mapping object handle.
 */
template<access_mode AccessMode, typename ByteT>
typename basic_mmap<AccessMode, ByteT>::handle_type
basic_mmap<AccessMode, ByteT>::mapping_handle() const noexcept
{
#ifdef _WIN32
    return file_mapping_handle_;
#else
    return file_handle_;
#endif
}

/**
 * Maps a file by path.
 *
 * Opens the file, creates the mapping, and marks the handle as internal
 * (will be closed on unmap). Uses strong exception guarantee: if mapping
 * fails, any previous mapping is preserved.
 */
template<access_mode AccessMode, typename ByteT>
void basic_mmap<AccessMode, ByteT>::map(const std::filesystem::path& path, const size_type offset,
        const size_type length, std::error_code& error)
{
    error.clear();

    // Validate path
    if(path.empty())
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    // Open file (handle will be closed on unmap since is_handle_internal_=true)
    const auto handle = detail::open_file(path, AccessMode, error);
    if(error)
    {
        return;
    }

    // Create the mapping using the handle overload
    map(handle, offset, length, error);

    // Mark handle as internally owned (so we close it on unmap)
    // This MUST be after the call to map(), which sets is_handle_internal_=false
    if(!error)
    {
        is_handle_internal_ = true;
    }
}

/**
 * Maps a file by handle.
 *
 * Creates a memory mapping from an existing file handle. The handle is
 * NOT owned by this object (is_handle_internal_=false), so the caller
 * must keep it open while the mapping exists.
 *
 * Provides strong exception guarantee: if the new mapping fails, any
 * existing mapping is preserved. The existing mapping is only unmapped
 * after the new mapping succeeds.
 */
template<access_mode AccessMode, typename ByteT>
void basic_mmap<AccessMode, ByteT>::map(const handle_type handle,
        const size_type offset, const size_type length, std::error_code& error)
{
    error.clear();

    // Validate handle
    if(handle == invalid_handle)
    {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return;
    }

    // Get file size to validate offset and determine length
    const auto file_size = detail::query_file_size(handle, error);
    if(error)
    {
        return;
    }

    // Validate offset + length doesn't exceed file size
    if(offset + length > file_size)
    {
        error = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    // Create the memory mapping
    // If length==map_entire_file (0), map from offset to end of file
    const auto ctx = detail::memory_map(handle, offset,
            length == map_entire_file ? (file_size - offset) : length,
            AccessMode, error);

    if(!error)
    {
        // Unmap previous mapping only after new mapping succeeded
        // This provides the strong guarantee that if map() fails,
        // the object remains in its previous state
        unmap();

        // Store new mapping state
        file_handle_ = handle;
        is_handle_internal_ = false;  // Caller owns the handle
        data_ = reinterpret_cast<pointer>(ctx.data);
        length_ = ctx.length;
        mapped_length_ = ctx.mapped_length;
#ifdef _WIN32
        file_mapping_handle_ = ctx.file_mapping_handle;
#endif
    }
}

/**
 * Synchronizes the mapped memory to disk.
 *
 * Flushes any modified pages in the mapping to the underlying file.
 * This is a blocking operation that returns when the write completes.
 *
 * Only available for write-mode mappings (enforced by static_assert).
 */
template<access_mode AccessMode, typename ByteT>
void basic_mmap<AccessMode, ByteT>::sync(std::error_code& error)
{
    static_assert(AccessMode == access_mode::write, "sync() requires write access");

    error.clear();

    if(!is_open())
    {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return;
    }

    if(data())
    {
#ifdef _WIN32
        // Windows: Flush the view, then flush the file buffers
        // FlushViewOfFile writes to system cache; FlushFileBuffers writes to disk
        if(::FlushViewOfFile(get_mapping_start(), mapped_length_) == 0
           || ::FlushFileBuffers(file_handle_) == 0)
#else // POSIX
        // POSIX: msync with MS_SYNC for synchronous write
        if(::msync(get_mapping_start(), mapped_length_, MS_SYNC) != 0)
#endif
        {
            error = detail::last_error();
            return;
        }
    }
}

/**
 * Unmaps the file and releases resources.
 *
 * Performs cleanup in the following order:
 * 1. Unmap the memory region
 * 2. Close file mapping handle (Windows only)
 * 3. Close file handle (only if opened internally via path)
 * 4. Reset all member variables to default state
 *
 * Calling unmap() on an already-unmapped object is safe (no-op).
 */
template<access_mode AccessMode, typename ByteT>
void basic_mmap<AccessMode, ByteT>::unmap()
{
    if(!is_open()) { return; }

    // Step 1: Unmap the memory region
#ifdef _WIN32
    if(is_mapped())
    {
        ::UnmapViewOfFile(get_mapping_start());
        ::CloseHandle(file_mapping_handle_);
    }
#else // POSIX
    if(data_) { ::munmap(const_cast<pointer>(get_mapping_start()), mapped_length_); }
#endif

    // Step 2: Close file handle if we opened it (internal handle)
    // External handles (provided by user) are NOT closed
    if(is_handle_internal_)
    {
#ifdef _WIN32
        ::CloseHandle(file_handle_);
#else // POSIX
        ::close(file_handle_);
#endif
    }

    // Step 3: Reset to default (unmapped) state
    data_ = nullptr;
    length_ = mapped_length_ = 0;
    file_handle_ = invalid_handle;
#ifdef _WIN32
    file_mapping_handle_ = invalid_handle;
#endif
}

/**
 * Checks if the memory mapping exists.
 *
 * On Windows, checks the file mapping handle (distinct from file handle).
 * On POSIX, equivalent to is_open().
 */
template<access_mode AccessMode, typename ByteT>
bool basic_mmap<AccessMode, ByteT>::is_mapped() const noexcept
{
#ifdef _WIN32
    return file_mapping_handle_ != invalid_handle;
#else // POSIX
    return is_open();
#endif
}

/**
 * Swaps contents with another mmap.
 *
 * Efficiently exchanges all state without any system calls.
 */
template<access_mode AccessMode, typename ByteT>
void basic_mmap<AccessMode, ByteT>::swap(basic_mmap& other) noexcept
{
    if(this != &other)
    {
        using std::swap;
        swap(data_, other.data_);
        swap(file_handle_, other.file_handle_);
#ifdef _WIN32
        swap(file_mapping_handle_, other.file_mapping_handle_);
#endif
        swap(length_, other.length_);
        swap(mapped_length_, other.mapped_length_);
        swap(is_handle_internal_, other.is_handle_internal_);
    }
}

/**
 * Conditionally syncs based on access mode.
 *
 * Called from destructor. Uses `if constexpr` for compile-time dispatch:
 * - Write mode: Calls sync() (ignoring errors in destructor context)
 * - Read mode: No-op
 */
template<access_mode AccessMode, typename ByteT>
void basic_mmap<AccessMode, ByteT>::conditional_sync()
{
    if constexpr (AccessMode == access_mode::write) {
        // Destructor can't handle errors, so we just try our best
        std::error_code ec;
        sync(ec);
        // Error intentionally ignored - destructor shouldn't throw
    }
    // For read mode: nothing to do
}

// -----------------------------------------------------------------------------
// Comparison operators
// -----------------------------------------------------------------------------

/**
 * Equality: Same data pointer and size.
 */
template<access_mode AccessMode, typename ByteT>
bool operator==(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b)
{
    return a.data() == b.data()
        && a.size() == b.size();
}

template<access_mode AccessMode, typename ByteT>
bool operator!=(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b)
{
    return !(a == b);
}

/**
 * Ordering: Compare by data pointer first, then by size.
 */
template<access_mode AccessMode, typename ByteT>
bool operator<(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b)
{
    if(a.data() == b.data()) { return a.size() < b.size(); }
    return a.data() < b.data();
}

template<access_mode AccessMode, typename ByteT>
bool operator<=(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b)
{
    return !(a > b);
}

template<access_mode AccessMode, typename ByteT>
bool operator>(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b)
{
    if(a.data() == b.data()) { return a.size() > b.size(); }
    return a.data() > b.data();
}

template<access_mode AccessMode, typename ByteT>
bool operator>=(const basic_mmap<AccessMode, ByteT>& a,
        const basic_mmap<AccessMode, ByteT>& b)
{
    return !(a < b);
}

} // namespace mio

#endif // MIO_BASIC_MMAP_IMPL


#endif // MIO_MMAP_HEADER
/* Copyright 2017 https://github.com/mandreyel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MIO_SHARED_MMAP_HEADER
#define MIO_SHARED_MMAP_HEADER

// -----------------------------------------------------------------------------
// shared_mmap.hpp - Shared ownership memory-mapped file I/O
// -----------------------------------------------------------------------------
//
// This header provides shared ownership variants of the basic_mmap classes.
// While basic_mmap has move-only semantics (single ownership), shared_mmap
// allows multiple owners to share the same memory mapping via std::shared_ptr.
//
// Key differences from basic_mmap:
// - Copyable: Multiple shared_mmap instances can reference the same mapping
// - Heap allocation: Uses std::shared_ptr internally (one allocation per mapping)
// - Reference counting: Mapping is released when last owner is destroyed
// - No throwing constructors: Use factory functions for error handling
//
// When to use shared_mmap vs basic_mmap:
// - Use basic_mmap (default) when a single owner is sufficient
// - Use shared_mmap when the mapping needs to be shared across components
// - Use shared_mmap when lifetime management is complex or unclear
//
// API note:
// Unlike basic_mmap, shared_mmap does NOT have throwing constructors from
// file paths. This is intentional to avoid overload ambiguity on Windows where
// char* can implicitly convert to both std::filesystem::path and void* (HANDLE).
// Use the factory functions make_shared_mmap_source/make_shared_mmap_sink instead.
//
// Usage:
//   std::error_code ec;
//   auto file = mio::make_shared_mmap_source("data.bin", ec);
//   if (ec) { handle_error(ec); }
//
//   // Multiple owners
//   auto copy = file;  // Both share the same mapping
//
//   // Move from basic_mmap
//   mio::mmap_source owned("data.bin");
//   mio::shared_mmap_source shared(std::move(owned));
//
// -----------------------------------------------------------------------------

// #include "mio/mmap.hpp"


#include <cassert>
#include <system_error>
#include <memory>

namespace mio {

// -----------------------------------------------------------------------------
// basic_shared_mmap - Shared ownership memory mapping
// -----------------------------------------------------------------------------

/**
 * A memory-mapped file region with shared ownership semantics.
 *
 * This class wraps a basic_mmap in a std::shared_ptr, allowing multiple
 * owners to share the same memory mapping. The mapping is automatically
 * released when the last shared_mmap referencing it is destroyed.
 *
 * Template parameters:
 * @tparam AccessMode Either `access_mode::read` for read-only mappings or
 *                    `access_mode::write` for read-write mappings.
 * @tparam ByteT      The byte type for the mapped data (char, unsigned char, std::byte).
 *
 * Ownership semantics:
 * - Copyable: Copies share the same underlying mapping (reference counted).
 * - Movable: Moving transfers the shared_ptr (efficient, no mapping changes).
 * - RAII: Last owner's destruction unmaps the file.
 *
 * API compatibility:
 * - Exposes nearly the same interface as basic_mmap
 * - Iterators, data(), size(), etc. work identically
 * - Main difference: no throwing constructors from paths (use factories)
 *
 * Thread safety:
 * - The shared_ptr itself is thread-safe for copying/assignment
 * - Concurrent access to the mapped data requires external synchronization
 *
 * @see basic_mmap for single-ownership variant
 * @see make_shared_mmap_source, make_shared_mmap_sink factory functions
 */
template<
    access_mode AccessMode,
    typename ByteT
> class basic_shared_mmap
{
    // The underlying single-owner mmap, wrapped in shared_ptr for ref counting
    using impl_type = basic_mmap<AccessMode, ByteT>;
    std::shared_ptr<impl_type> pimpl_;

public:
    // -------------------------------------------------------------------------
    // Type aliases (mirror basic_mmap for compatibility)
    // -------------------------------------------------------------------------

    using value_type = typename impl_type::value_type;             ///< Byte type
    using size_type = typename impl_type::size_type;               ///< Size/offset type
    using reference = typename impl_type::reference;               ///< Byte reference
    using const_reference = typename impl_type::const_reference;   ///< Const byte reference
    using pointer = typename impl_type::pointer;                   ///< Data pointer
    using const_pointer = typename impl_type::const_pointer;       ///< Const data pointer
    using difference_type = typename impl_type::difference_type;   ///< Pointer difference
    using iterator = typename impl_type::iterator;                 ///< Iterator type
    using const_iterator = typename impl_type::const_iterator;     ///< Const iterator
    using reverse_iterator = typename impl_type::reverse_iterator; ///< Reverse iterator
    using const_reverse_iterator = typename impl_type::const_reverse_iterator; ///< Const reverse iterator
    using iterator_category = typename impl_type::iterator_category;           ///< Iterator category
    using handle_type = typename impl_type::handle_type;           ///< File handle type
    using mmap_type = impl_type;                                   ///< Underlying mmap type

    // -------------------------------------------------------------------------
    // Constructors
    // -------------------------------------------------------------------------

    /**
     * Default constructor. Creates an empty shared_mmap.
     *
     * An empty shared_mmap has is_open() == false and contains no mapping.
     * Use map() or factory functions to establish a mapping.
     */
    basic_shared_mmap() = default;

    /**
     * Copy constructor. Creates a new reference to the same mapping.
     *
     * After copying, both shared_mmaps point to the same underlying mapping.
     * The mapping will remain valid until all references are destroyed.
     * This is an O(1) operation (just copies a shared_ptr).
     */
    basic_shared_mmap(const basic_shared_mmap&) = default;

    /**
     * Copy assignment. Replaces the current mapping reference.
     *
     * If this shared_mmap was the last reference to a previous mapping,
     * that mapping is released. Then this object starts sharing the
     * source's mapping.
     */
    basic_shared_mmap& operator=(const basic_shared_mmap&) = default;

    /**
     * Move constructor. Transfers the mapping reference.
     *
     * The source shared_mmap becomes empty (is_open() == false).
     * More efficient than copy when the source is no longer needed.
     */
    basic_shared_mmap(basic_shared_mmap&&) = default;

    /**
     * Move assignment. Transfers the mapping reference.
     *
     * The source shared_mmap becomes empty. If this was the last reference
     * to a previous mapping, that mapping is released first.
     */
    basic_shared_mmap& operator=(basic_shared_mmap&&) = default;

    /**
     * Constructs from a basic_mmap by taking ownership.
     *
     * The basic_mmap is moved into a new shared_ptr, enabling shared
     * ownership. The source mmap is left in an unmapped state.
     *
     * This is useful for upgrading a single-owner mapping to shared ownership:
     *   mio::mmap_source owned("data.bin");
     *   mio::shared_mmap_source shared(std::move(owned));
     *
     * @param mmap The mmap to take ownership of (will be moved-from).
     */
    basic_shared_mmap(mmap_type&& mmap)
        : pimpl_(std::make_shared<mmap_type>(std::move(mmap)))
    {}

    /**
     * Assignment from a basic_mmap by taking ownership.
     *
     * Creates a new shared_ptr containing the moved mmap. Any previous
     * mapping reference is released (and unmapped if this was the last ref).
     *
     * @param mmap The mmap to take ownership of.
     * @return Reference to this object.
     */
    basic_shared_mmap& operator=(mmap_type&& mmap)
    {
        pimpl_ = std::make_shared<mmap_type>(std::move(mmap));
        return *this;
    }

    /**
     * Constructs from an existing shared_ptr to a mmap.
     *
     * Allows integration with code that already manages mmap lifetime
     * via shared_ptr. This shared_mmap becomes another owner of that mapping.
     *
     * @param mmap Shared pointer to an existing mmap (can be nullptr).
     */
    basic_shared_mmap(std::shared_ptr<mmap_type> mmap) : pimpl_(std::move(mmap)) {}

    /**
     * Assignment from an existing shared_ptr.
     *
     * @param mmap Shared pointer to assign from.
     * @return Reference to this object.
     */
    basic_shared_mmap& operator=(std::shared_ptr<mmap_type> mmap)
    {
        pimpl_ = std::move(mmap);
        return *this;
    }


    /**
     * Destructor.
     *
     * Releases this object's reference to the shared mapping. If this was
     * the last reference, the underlying mmap is destroyed (which unmaps
     * the file and, for write mode, syncs changes to disk).
     */
    ~basic_shared_mmap() = default;

    // -------------------------------------------------------------------------
    // Shared pointer access
    // -------------------------------------------------------------------------

    /**
     * Returns the underlying shared_ptr.
     *
     * Useful for:
     * - Checking reference count: get_shared_ptr().use_count()
     * - Comparing identity: a.get_shared_ptr() == b.get_shared_ptr()
     * - Interop with code expecting shared_ptr<mmap_type>
     *
     * @return The shared_ptr managing the underlying mmap.
     */
    [[nodiscard]] std::shared_ptr<mmap_type> get_shared_ptr() { return pimpl_; }

    // -------------------------------------------------------------------------
    // Handle accessors
    // -------------------------------------------------------------------------

    /**
     * Returns the file handle used for the mapping.
     *
     * @return The file handle, or `invalid_handle` if not mapped.
     * @see basic_mmap::file_handle()
     */
    [[nodiscard]] handle_type file_handle() const noexcept
    {
        return pimpl_ ? pimpl_->file_handle() : invalid_handle;
    }

    /**
     * Returns the mapping-specific handle.
     *
     * On POSIX, same as file_handle(). On Windows, returns the file
     * mapping object handle.
     *
     * @return The mapping handle, or `invalid_handle` if not mapped.
     */
    [[nodiscard]] handle_type mapping_handle() const noexcept
    {
        return pimpl_ ? pimpl_->mapping_handle() : invalid_handle;
    }

    // -------------------------------------------------------------------------
    // State queries
    // -------------------------------------------------------------------------

    /**
     * Returns true if a valid memory mapping exists.
     *
     * Checks both that the shared_ptr is valid and that the underlying
     * mmap is open.
     *
     * @return true if the mapping is open and accessible.
     */
    [[nodiscard]] bool is_open() const noexcept { return pimpl_ && pimpl_->is_open(); }

    /**
     * Returns true if memory has been mapped.
     *
     * @return true if a memory mapping exists.
     */
    [[nodiscard]] bool is_mapped() const noexcept { return pimpl_ && pimpl_->is_mapped(); }

    /**
     * Returns true if the mapping is empty (no data to access).
     *
     * Returns true if either:
     * - The shared_ptr is null (no mapping established)
     * - The underlying mmap has zero length
     *
     * @return true if size() == 0 or no mapping exists.
     */
    [[nodiscard]] bool empty() const noexcept { return !pimpl_ || pimpl_->empty(); }

    // -------------------------------------------------------------------------
    // Size queries
    // -------------------------------------------------------------------------

    /**
     * Returns the logical size of the mapped region in bytes.
     *
     * @return Number of accessible bytes, or 0 if not mapped.
     */
    [[nodiscard]] size_type size() const noexcept { return pimpl_ ? pimpl_->length() : 0; }

    /**
     * Returns the logical length of the mapped region in bytes.
     *
     * @return Number of accessible bytes, or 0 if not mapped.
     */
    [[nodiscard]] size_type length() const noexcept { return pimpl_ ? pimpl_->length() : 0; }

    /**
     * Returns the actual mapped length in bytes.
     *
     * May be larger than length() due to page alignment.
     *
     * @return Actual mapped size, or 0 if not mapped.
     */
    [[nodiscard]] size_type mapped_length() const noexcept
    {
        return pimpl_ ? pimpl_->mapped_length() : 0;
    }

    // -------------------------------------------------------------------------
    // Data access
    // -------------------------------------------------------------------------

    /**
     * Returns a pointer to the first byte of the mapped region.
     *
     * Non-const version requires write access mode.
     * Asserts in debug mode if called on an empty shared_mmap.
     *
     * @return Pointer to mapped data.
     */
    [[nodiscard]] pointer data() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const data() requires write access");
        assert(pimpl_ && "data() called on empty shared_mmap");
        return pimpl_->data();
    }

    /**
     * Returns a const pointer to the first byte of the mapped region.
     *
     * @return Const pointer to mapped data, or nullptr if not mapped.
     */
    [[nodiscard]] const_pointer data() const noexcept { return pimpl_ ? pimpl_->data() : nullptr; }

    // -------------------------------------------------------------------------
    // Iterator access
    // -------------------------------------------------------------------------

    /**
     * Returns an iterator to the first byte.
     *
     * Non-const version requires write access mode.
     * Undefined behavior if called on an empty shared_mmap.
     *
     * @return Iterator to the first byte.
     */
    [[nodiscard]] iterator begin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const begin() requires write access");
        assert(pimpl_ && "begin() called on empty shared_mmap");
        return pimpl_->begin();
    }

    /** @copydoc begin() */
    [[nodiscard]] const_iterator begin() const noexcept {
        assert(pimpl_ && "begin() called on empty shared_mmap");
        return pimpl_->begin();
    }

    /** @copydoc begin() */
    [[nodiscard]] const_iterator cbegin() const noexcept {
        assert(pimpl_ && "cbegin() called on empty shared_mmap");
        return pimpl_->cbegin();
    }

    /**
     * Returns an iterator one past the last byte.
     *
     * Non-const version requires write access mode.
     * Undefined behavior if called on an empty shared_mmap.
     *
     * @return Iterator past the last byte.
     */
    [[nodiscard]] iterator end() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const end() requires write access");
        assert(pimpl_ && "end() called on empty shared_mmap");
        return pimpl_->end();
    }

    /** @copydoc end() */
    [[nodiscard]] const_iterator end() const noexcept {
        assert(pimpl_ && "end() called on empty shared_mmap");
        return pimpl_->end();
    }

    /** @copydoc end() */
    [[nodiscard]] const_iterator cend() const noexcept {
        assert(pimpl_ && "cend() called on empty shared_mmap");
        return pimpl_->cend();
    }

    /**
     * Returns a reverse iterator to the last byte.
     *
     * Non-const version requires write access mode.
     *
     * @return Reverse iterator to the last byte.
     */
    [[nodiscard]] reverse_iterator rbegin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rbegin() requires write access");
        assert(pimpl_ && "rbegin() called on empty shared_mmap");
        return pimpl_->rbegin();
    }

    /** @copydoc rbegin() */
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept {
        assert(pimpl_ && "rbegin() called on empty shared_mmap");
        return pimpl_->rbegin();
    }

    /** @copydoc rbegin() */
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept {
        assert(pimpl_ && "crbegin() called on empty shared_mmap");
        return pimpl_->crbegin();
    }

    /**
     * Returns a reverse iterator before the first byte.
     *
     * Non-const version requires write access mode.
     *
     * @return Reverse iterator before the first byte.
     */
    [[nodiscard]] reverse_iterator rend() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rend() requires write access");
        assert(pimpl_ && "rend() called on empty shared_mmap");
        return pimpl_->rend();
    }

    /** @copydoc rend() */
    [[nodiscard]] const_reverse_iterator rend() const noexcept {
        assert(pimpl_ && "rend() called on empty shared_mmap");
        return pimpl_->rend();
    }

    /** @copydoc rend() */
    [[nodiscard]] const_reverse_iterator crend() const noexcept {
        assert(pimpl_ && "crend() called on empty shared_mmap");
        return pimpl_->crend();
    }

    // -------------------------------------------------------------------------
    // Element access
    // -------------------------------------------------------------------------

    /**
     * Returns a reference to the byte at the given index.
     *
     * No bounds checking. Non-const version requires write access mode.
     *
     * @param i Index of the byte (0-based).
     * @return Reference to the byte.
     */
    [[nodiscard]] reference operator[](const size_type i) noexcept {
        static_assert(AccessMode == access_mode::write, "non-const operator[] requires write access");
        assert(pimpl_ && "operator[] called on empty shared_mmap");
        return (*pimpl_)[i];
    }

    /** @copydoc operator[]() */
    [[nodiscard]] const_reference operator[](const size_type i) const noexcept {
        assert(pimpl_ && "operator[] called on empty shared_mmap");
        return (*pimpl_)[i];
    }

    // -------------------------------------------------------------------------
    // C++20 std::span support
    // -------------------------------------------------------------------------

#if __cplusplus >= 202002L
    /**
     * Returns the mapped memory as a read-only std::span.
     *
     * @return A span viewing the mapped region, or empty span if not mapped.
     */
    [[nodiscard]] std::span<const value_type> as_span() const noexcept {
        return pimpl_ ? pimpl_->as_span() : std::span<const value_type>{};
    }

    /**
     * Returns the mapped memory as a mutable std::span.
     *
     * Only available for write access mode.
     *
     * @return A mutable span viewing the mapped region.
     */
    [[nodiscard]] std::span<value_type> as_span() noexcept {
        static_assert(AccessMode == access_mode::write, "mutable as_span() requires write access");
        assert(pimpl_ && "as_span() called on empty shared_mmap");
        return pimpl_->as_span();
    }
#endif

    // -------------------------------------------------------------------------
    // Mapping operations
    // -------------------------------------------------------------------------

    /**
     * Establishes a memory mapping from a file path.
     *
     * Creates or reuses the internal shared_ptr to hold the new mapping.
     * If this shared_mmap already references a mapping, behavior depends
     * on whether there are other owners:
     * - If sole owner: reuses the same mmap object
     * - If shared: creates a new mmap (other owners keep their reference)
     *
     * @param path   Path to an existing file.
     * @param offset Byte offset where mapping starts.
     * @param length Number of bytes to map, or `map_entire_file`.
     * @param error  Output parameter for error reporting.
     *
     * Note: The std::filesystem::path overload is used to avoid overload
     * ambiguity on Windows where char* could match both path and HANDLE.
     */
    void map(const std::filesystem::path& path, const size_type offset,
        const size_type length, std::error_code& error)
    {
        map_impl(path, offset, length, error);
    }

    /**
     * Maps the entire file from a path.
     *
     * @param path  Path to an existing file.
     * @param error Output parameter for error reporting.
     */
    void map(const std::filesystem::path& path, std::error_code& error)
    {
        map_impl(path, 0, map_entire_file, error);
    }

    /**
     * Establishes a memory mapping from an existing file handle.
     *
     * The handle is NOT owned by this object; caller must keep it open
     * while the mapping exists and close it afterward.
     *
     * @param handle Valid file handle (HANDLE on Windows, fd on POSIX).
     * @param offset Byte offset where mapping starts.
     * @param length Number of bytes to map, or `map_entire_file`.
     * @param error  Output parameter for error reporting.
     */
    void map(const handle_type handle, const size_type offset,
        const size_type length, std::error_code& error)
    {
        map_impl(handle, offset, length, error);
    }

    /**
     * Maps the entire file from a handle.
     *
     * @param handle Valid file handle.
     * @param error  Output parameter for error reporting.
     */
    void map(const handle_type handle, std::error_code& error)
    {
        map_impl(handle, 0, map_entire_file, error);
    }

    /**
     * Releases this object's reference to the mapping.
     *
     * If this is the sole owner, the underlying mmap is unmapped.
     * Otherwise, other shared_mmaps continue to have access.
     *
     * After calling, is_open() returns false for this object.
     */
    void unmap() { if(pimpl_) pimpl_->unmap(); }

    /**
     * Swaps contents with another shared_mmap.
     *
     * Efficiently exchanges the shared_ptr (no mapping changes).
     *
     * @param other The shared_mmap to swap with.
     */
    void swap(basic_shared_mmap& other) noexcept { pimpl_.swap(other.pimpl_); }

    /**
     * Flushes modified pages to the underlying file.
     *
     * Only available for write access mode.
     *
     * @param error Output parameter for error reporting.
     */
    void sync(std::error_code& error) {
        static_assert(AccessMode == access_mode::write, "sync() requires write access");
        if(pimpl_) pimpl_->sync(error);
    }

    // -------------------------------------------------------------------------
    // Comparison operators
    // -------------------------------------------------------------------------

    /**
     * Equality comparison.
     *
     * Two shared_mmaps are equal if they reference the same underlying mmap
     * (same shared_ptr). This is identity comparison, not content comparison.
     */
    [[nodiscard]] friend bool operator==(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return a.pimpl_ == b.pimpl_;
    }

    [[nodiscard]] friend bool operator!=(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return !(a == b);
    }

    /**
     * Ordering comparisons.
     *
     * Compares the shared_ptr addresses, providing a consistent ordering
     * for use in ordered containers.
     */
    [[nodiscard]] friend bool operator<(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return a.pimpl_ < b.pimpl_;
    }

    [[nodiscard]] friend bool operator<=(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return a.pimpl_ <= b.pimpl_;
    }

    [[nodiscard]] friend bool operator>(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return a.pimpl_ > b.pimpl_;
    }

    [[nodiscard]] friend bool operator>=(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return a.pimpl_ >= b.pimpl_;
    }

private:
    // -------------------------------------------------------------------------
    // Private implementation
    // -------------------------------------------------------------------------

    /**
     * Internal map implementation.
     *
     * If pimpl_ is null, creates a new mmap via make_mmap and wraps it.
     * If pimpl_ exists, reuses it by calling map() on the existing mmap.
     *
     * @tparam MappingToken Path or handle type.
     * @param token  File path or handle.
     * @param offset Byte offset.
     * @param length Number of bytes to map.
     * @param error  Error output parameter.
     */
    template<typename MappingToken>
    void map_impl(const MappingToken& token, const size_type offset,
        const size_type length, std::error_code& error)
    {
        if(!pimpl_)
        {
            // No existing mapping - create a new one
            mmap_type mmap = make_mmap<mmap_type>(token, offset, length, error);
            if(error) { return; }
            pimpl_ = std::make_shared<mmap_type>(std::move(mmap));
        }
        else
        {
            // Reuse existing mmap object (note: this may affect other owners
            // if they exist, which is probably unintended - consider if pimpl_
            // should be replaced instead when use_count() > 1)
            pimpl_->map(token, offset, length, error);
        }
    }
};

// -----------------------------------------------------------------------------
// Type aliases for common use cases
// -----------------------------------------------------------------------------

/**
 * Read-only shared memory mapping template.
 *
 * @tparam ByteT The byte type (char, unsigned char, std::byte).
 */
template<typename ByteT>
using basic_shared_mmap_source = basic_shared_mmap<access_mode::read, ByteT>;

/**
 * Read-write shared memory mapping template.
 *
 * @tparam ByteT The byte type (char, unsigned char, std::byte).
 */
template<typename ByteT>
using basic_shared_mmap_sink = basic_shared_mmap<access_mode::write, ByteT>;

// Convenient type aliases for common byte types:

/// Shared read-only mapping with char bytes (most common)
using shared_mmap_source = basic_shared_mmap_source<char>;

/// Shared read-only mapping with unsigned char bytes
using shared_ummap_source = basic_shared_mmap_source<unsigned char>;

/// Shared read-only mapping with std::byte bytes (C++17)
using shared_bmmap_source = basic_shared_mmap_source<std::byte>;

/// Shared read-write mapping with char bytes (most common)
using shared_mmap_sink = basic_shared_mmap_sink<char>;

/// Shared read-write mapping with unsigned char bytes
using shared_ummap_sink = basic_shared_mmap_sink<unsigned char>;

/// Shared read-write mapping with std::byte bytes (C++17)
using shared_bmmap_sink = basic_shared_mmap_sink<std::byte>;

// -----------------------------------------------------------------------------
// Factory functions
// -----------------------------------------------------------------------------

/**
 * Creates a shared read-only memory mapping.
 *
 * This is the recommended way to create shared_mmap_source objects.
 * Factory functions are used instead of throwing constructors to avoid
 * overload ambiguity on Windows (char* -> void* implicit conversion).
 *
 * @tparam MappingToken Type that can identify a file:
 *         - std::filesystem::path, std::string, const char*
 *         - shared_mmap_source::handle_type (file descriptor or HANDLE)
 *
 * @param token  The file path or handle to map.
 * @param offset Byte offset where mapping starts.
 * @param length Number of bytes to map, or `map_entire_file`.
 * @param error  Output parameter set on failure, cleared on success.
 *
 * @return The created shared_mmap_source.
 *
 * Example:
 *   std::error_code ec;
 *   auto file = mio::make_shared_mmap_source("data.bin", 0, 1024, ec);
 *   if (!ec) {
 *       auto copy = file;  // Both share the same mapping
 *       for (char c : copy) { process(c); }
 *   }
 */
template<typename MappingToken>
shared_mmap_source make_shared_mmap_source(const MappingToken& token,
        shared_mmap_source::size_type offset,
        shared_mmap_source::size_type length, std::error_code& error)
{
    return make_mmap<shared_mmap_source>(token, offset, length, error);
}

/**
 * Creates a shared read-only mapping of an entire file.
 *
 * Convenience overload that maps from offset 0 to end of file.
 *
 * @param token The file path or handle to map.
 * @param error Output parameter for error reporting.
 * @return The created shared_mmap_source.
 */
template<typename MappingToken>
shared_mmap_source make_shared_mmap_source(const MappingToken& token, std::error_code& error)
{
    return make_shared_mmap_source(token, 0, map_entire_file, error);
}

/**
 * Creates a shared read-write memory mapping.
 *
 * This is the recommended way to create shared_mmap_sink objects.
 *
 * @tparam MappingToken Type that can identify a file (path or handle).
 *
 * @param token  The file path or handle to map.
 * @param offset Byte offset where mapping starts.
 * @param length Number of bytes to map, or `map_entire_file`.
 * @param error  Output parameter for error reporting.
 *
 * @return The created shared_mmap_sink.
 *
 * Example:
 *   std::error_code ec;
 *   auto file = mio::make_shared_mmap_sink("output.bin", ec);
 *   if (!ec) {
 *       std::fill(file.begin(), file.end(), 0);
 *       file.sync(ec);
 *   }
 */
template<typename MappingToken>
shared_mmap_sink make_shared_mmap_sink(const MappingToken& token,
        shared_mmap_sink::size_type offset,
        shared_mmap_sink::size_type length, std::error_code& error)
{
    return make_mmap<shared_mmap_sink>(token, offset, length, error);
}

/**
 * Creates a shared read-write mapping of an entire file.
 *
 * Convenience overload that maps from offset 0 to end of file.
 *
 * @param token The file path or handle to map.
 * @param error Output parameter for error reporting.
 * @return The created shared_mmap_sink.
 */
template<typename MappingToken>
shared_mmap_sink make_shared_mmap_sink(const MappingToken& token, std::error_code& error)
{
    return make_shared_mmap_sink(token, 0, map_entire_file, error);
}

} // namespace mio

#endif // MIO_SHARED_MMAP_HEADER
