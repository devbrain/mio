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
        // sysconf returns long; cast to size_t (always positive for _SC_PAGE_SIZE).
        return static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
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
