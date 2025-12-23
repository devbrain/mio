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

#include "mio/mmap.hpp"
#include "mio/page.hpp"
#include "mio/detail/string_util.hpp"

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
    return static_cast<DWORD>(static_cast<uint64_t>(n) >> 32);
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
    error.assign(static_cast<int>(GetLastError()), std::system_category());
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
    // st_size is off_t (signed); cast to size_t (safe after successful fstat).
    return static_cast<size_t>(sbuf.st_size);
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
    // Cast offset to size_t for make_offset_page_aligned (offset is non-negative for valid mappings).
    const int64_t aligned_offset = static_cast<int64_t>(
        make_offset_page_aligned(static_cast<size_t>(offset)));

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
    // Cast length_to_map to SIZE_T (MapViewOfFile expects SIZE_T for size parameter).
    char* mapping_start = static_cast<char*>(::MapViewOfFile(
            file_mapping_handle,
            mode == access_mode::read ? FILE_MAP_READ : FILE_MAP_WRITE,
            win::int64_high(aligned_offset),  // Upper 32 bits of offset
            win::int64_low(aligned_offset),   // Lower 32 bits of offset
            static_cast<SIZE_T>(length_to_map)));

    if(mapping_start == nullptr)
    {
        // Clean up file mapping handle on failure
        ::CloseHandle(file_mapping_handle);
        error = detail::last_error();
        return {};
    }
#else // POSIX
    // POSIX mmap is simpler - maps file directly to memory
    // Cast length_to_map to size_t (mmap expects size_t for length parameter).
    char* mapping_start = static_cast<char*>(::mmap(
            0,  // Let OS choose mapping address (no hint)
            static_cast<size_t>(length_to_map),
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
    // Cast size_type to int64_t for memory_map (values are non-negative, safe conversion).
    const auto ctx = detail::memory_map(handle,
            static_cast<int64_t>(offset),
            static_cast<int64_t>(length == map_entire_file ? (file_size - offset) : length),
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
        // Cast int64_t to size_type (safe: values are from successful mapping, always non-negative).
        length_ = static_cast<size_type>(ctx.length);
        mapped_length_ = static_cast<size_type>(ctx.mapped_length);
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
