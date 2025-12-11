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

#include "mio/page.hpp"

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
#include "detail/mmap.ipp"

#endif // MIO_MMAP_HEADER
