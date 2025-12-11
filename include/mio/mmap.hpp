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
# define INVALID_HANDLE_VALUE -1
#endif // ifdef _WIN32

namespace mio {

/// This value may be provided as the `length` parameter to the constructor or
/// `map`, in which case a memory mapping of the entire file is created.
inline constexpr size_t map_entire_file = 0;

#ifdef _WIN32
using file_handle_type = HANDLE;
#else
using file_handle_type = int;
#endif

/// This value represents an invalid file handle type. This can be used to
/// determine whether `basic_mmap::file_handle` is valid, for example.
#ifdef _WIN32
// INVALID_HANDLE_VALUE is ((HANDLE)(LONG_PTR)-1), which is not constexpr-friendly on MSVC
inline const file_handle_type invalid_handle = INVALID_HANDLE_VALUE;
#else
inline constexpr file_handle_type invalid_handle = -1;
#endif

template<access_mode AccessMode, typename ByteT>
struct basic_mmap
{
    using value_type = ByteT;
    using size_type = size_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using difference_type = std::ptrdiff_t;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using iterator_category = std::random_access_iterator_tag;
    using handle_type = file_handle_type;

    static_assert(sizeof(ByteT) == sizeof(char), "ByteT must be the same size as char.");

private:
    // Points to the first requested byte, and not to the actual start of the mapping.
    pointer data_ = nullptr;

    // Length--in bytes--requested by user (which may not be the length of the
    // full mapping) and the length of the full mapping.
    size_type length_ = 0;
    size_type mapped_length_ = 0;

    // Letting user map a file using both an existing file handle and a path
    // introduces some complexity (see `is_handle_internal_`).
    // On POSIX, we only need a file handle to create a mapping, while on
    // Windows systems the file handle is necessary to retrieve a file mapping
    // handle, but any subsequent operations on the mapped region must be done
    // through the latter.
    handle_type file_handle_ = INVALID_HANDLE_VALUE;
#ifdef _WIN32
    handle_type file_mapping_handle_ = INVALID_HANDLE_VALUE;
#endif

    // Letting user map a file using both an existing file handle and a path
    // introduces some complexity in that we must not close the file handle if
    // user provided it, but we must close it if we obtained it using the
    // provided path. For this reason, this flag is used to determine when to
    // close `file_handle_`.
    bool is_handle_internal_;

public:
    /**
     * The default constructed mmap object is in a non-mapped state, that is,
     * any operation that attempts to access nonexistent underlying data will
     * result in undefined behaviour/segmentation faults.
     */
    basic_mmap() = default;

#ifdef __cpp_exceptions
    /**
     * The same as invoking the `map` function, except any error that may occur
     * while establishing the mapping is wrapped in a `std::system_error` and is
     * thrown.
     */
    basic_mmap(const std::filesystem::path& path, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        map(path, offset, length, error);
        if(error) { throw std::system_error(error); }
    }

    /**
     * The same as invoking the `map` function, except any error that may occur
     * while establishing the mapping is wrapped in a `std::system_error` and is
     * thrown.
     */
    basic_mmap(const handle_type handle, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        map(handle, offset, length, error);
        if(error) { throw std::system_error(error); }
    }
#endif // __cpp_exceptions

    /**
     * `basic_mmap` has single-ownership semantics, so transferring ownership
     * may only be accomplished by moving the object.
     */
    basic_mmap(const basic_mmap&) = delete;
    basic_mmap(basic_mmap&&);
    basic_mmap& operator=(const basic_mmap&) = delete;
    basic_mmap& operator=(basic_mmap&&);

    /**
     * If this is a read-write mapping, the destructor invokes sync. Regardless
     * of the access mode, unmap is invoked as a final step.
     */
    ~basic_mmap();

    /**
     * On UNIX systems 'file_handle' and 'mapping_handle' are the same. On Windows,
     * however, a mapped region of a file gets its own handle, which is returned by
     * 'mapping_handle'.
     */
    [[nodiscard]] handle_type file_handle() const noexcept { return file_handle_; }
    [[nodiscard]] handle_type mapping_handle() const noexcept;

    /** Returns whether a valid memory mapping has been created. */
    [[nodiscard]] bool is_open() const noexcept { return file_handle_ != invalid_handle; }

    /**
     * Returns true if no mapping was established, that is, conceptually the
     * same as though the length that was mapped was 0. This function is
     * provided so that this class has Container semantics.
     */
    [[nodiscard]] bool empty() const noexcept { return length() == 0; }

    /** Returns true if a mapping was established. */
    [[nodiscard]] bool is_mapped() const noexcept;

    /**
     * `size` and `length` both return the logical length, i.e. the number of bytes
     * user requested to be mapped, while `mapped_length` returns the actual number of
     * bytes that were mapped which is a multiple of the underlying operating system's
     * page allocation granularity.
     */
    [[nodiscard]] size_type size() const noexcept { return length(); }
    [[nodiscard]] size_type length() const noexcept { return length_; }
    [[nodiscard]] size_type mapped_length() const noexcept { return mapped_length_; }

    /** Returns the offset relative to the start of the mapping. */
    [[nodiscard]] size_type mapping_offset() const noexcept
    {
        return mapped_length_ - length_;
    }

    /**
     * Returns a pointer to the first requested byte, or `nullptr` if no memory mapping
     * exists. Non-const version only available for write access mode.
     */
    [[nodiscard]] pointer data() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const data() requires write access");
        return data_;
    }
    [[nodiscard]] const_pointer data() const noexcept { return data_; }

    /**
     * Returns an iterator to the first requested byte, if a valid memory mapping
     * exists, otherwise this function call is undefined behaviour.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] iterator begin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const begin() requires write access");
        return data();
    }
    [[nodiscard]] const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data(); }

    /**
     * Returns an iterator one past the last requested byte, if a valid memory mapping
     * exists, otherwise this function call is undefined behaviour.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] iterator end() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const end() requires write access");
        return data() + length();
    }
    [[nodiscard]] const_iterator end() const noexcept { return data() + length(); }
    [[nodiscard]] const_iterator cend() const noexcept { return data() + length(); }

    /**
     * Returns a reverse iterator to the last memory mapped byte, if a valid
     * memory mapping exists, otherwise this function call is undefined
     * behaviour. Non-const version only available for write access mode.
     */
    [[nodiscard]] reverse_iterator rbegin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rbegin() requires write access");
        return reverse_iterator(end());
    }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    { return const_reverse_iterator(end()); }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept
    { return const_reverse_iterator(end()); }

    /**
     * Returns a reverse iterator past the first mapped byte, if a valid memory
     * mapping exists, otherwise this function call is undefined behaviour.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] reverse_iterator rend() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rend() requires write access");
        return reverse_iterator(begin());
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept
    { return const_reverse_iterator(begin()); }
    [[nodiscard]] const_reverse_iterator crend() const noexcept
    { return const_reverse_iterator(begin()); }

    /**
     * Returns a reference to the `i`th byte from the first requested byte (as returned
     * by `data`). If this is invoked when no valid memory mapping has been created
     * prior to this call, undefined behaviour ensues.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] reference operator[](const size_type i) noexcept {
        static_assert(AccessMode == access_mode::write, "non-const operator[] requires write access");
        return data_[i];
    }
    [[nodiscard]] const_reference operator[](const size_type i) const noexcept { return data_[i]; }

#if __cplusplus >= 202002L
    /** Returns the mapped memory as a read-only std::span. */
    [[nodiscard]] std::span<const value_type> as_span() const noexcept {
        return {data(), length()};
    }

    /** Returns the mapped memory as a mutable std::span (write mode only). */
    template<access_mode A = AccessMode, std::enable_if_t<A == access_mode::write, int> = 0>
    [[nodiscard]] std::span<value_type> as_span() noexcept {
        return {data(), length()};
    }
#endif

    /**
     * Establishes a memory mapping with AccessMode. If the mapping is unsuccessful, the
     * reason is reported via `error` and the object remains in a state as if this
     * function hadn't been called.
     *
     * `path`, which must be a path to an existing file, is used to retrieve a file
     * handle (which is closed when the object destructs or `unmap` is called), which is
     * then used to memory map the requested region. Upon failure, `error` is set to
     * indicate the reason and the object remains in an unmapped state.
     *
     * `offset` is the number of bytes, relative to the start of the file, where the
     * mapping should begin. When specifying it, there is no need to worry about
     * providing a value that is aligned with the operating system's page allocation
     * granularity. This is adjusted by the implementation such that the first requested
     * byte (as returned by `data` or `begin`), so long as `offset` is valid, will be at
     * `offset` from the start of the file.
     *
     * `length` is the number of bytes to map. It may be `map_entire_file`, in which
     * case a mapping of the entire file is created.
     */
    void map(const std::filesystem::path& path, const size_type offset,
            const size_type length, std::error_code& error);

    /**
     * Overload for const char* to handle null pointers safely.
     * Constructing std::filesystem::path from nullptr is UB, so we check first.
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
     * Establishes a memory mapping with AccessMode. If the mapping is unsuccessful, the
     * reason is reported via `error` and the object remains in a state as if this
     * function hadn't been called.
     *
     * `path`, which must be a path to an existing file, is used to retrieve a file
     * handle (which is closed when the object destructs or `unmap` is called), which is
     * then used to memory map the requested region. Upon failure, `error` is set to
     * indicate the reason and the object remains in an unmapped state.
     *
     * The entire file is mapped.
     */
    void map(const std::filesystem::path& path, std::error_code& error)
    {
        map(path, 0, map_entire_file, error);
    }

    /**
     * Overload for const char* to handle null pointers safely.
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
     * Establishes a memory mapping with AccessMode. If the mapping is
     * unsuccessful, the reason is reported via `error` and the object remains in
     * a state as if this function hadn't been called.
     *
     * `handle`, which must be a valid file handle, which is used to memory map the
     * requested region. Upon failure, `error` is set to indicate the reason and the
     * object remains in an unmapped state.
     *
     * `offset` is the number of bytes, relative to the start of the file, where the
     * mapping should begin. When specifying it, there is no need to worry about
     * providing a value that is aligned with the operating system's page allocation
     * granularity. This is adjusted by the implementation such that the first requested
     * byte (as returned by `data` or `begin`), so long as `offset` is valid, will be at
     * `offset` from the start of the file.
     *
     * `length` is the number of bytes to map. It may be `map_entire_file`, in which
     * case a mapping of the entire file is created.
     */
    void map(const handle_type handle, const size_type offset,
            const size_type length, std::error_code& error);

    /**
     * Establishes a memory mapping with AccessMode. If the mapping is
     * unsuccessful, the reason is reported via `error` and the object remains in
     * a state as if this function hadn't been called.
     *
     * `handle`, which must be a valid file handle, which is used to memory map the
     * requested region. Upon failure, `error` is set to indicate the reason and the
     * object remains in an unmapped state.
     *
     * The entire file is mapped.
     */
    void map(const handle_type handle, std::error_code& error)
    {
        map(handle, 0, map_entire_file, error);
    }

    /**
     * If a valid memory mapping has been created prior to this call, this call
     * instructs the kernel to unmap the memory region and disassociate this object
     * from the file.
     *
     * The file handle associated with the file that is mapped is only closed if the
     * mapping was created using a file path. If, on the other hand, an existing
     * file handle was used to create the mapping, the file handle is not closed.
     */
    void unmap();

    void swap(basic_mmap& other) noexcept;

    /**
     * Flushes the memory mapped page to disk. Errors are reported via `error`.
     * Only available for write access mode.
     */
    void sync(std::error_code& error);

private:
    // Returns the actual start of the mapping (before any offset adjustment).
    // Used internally by unmap() and sync() for both read and write modes.
    // Accesses data_ directly to avoid triggering access mode checks.
    [[nodiscard]] pointer get_mapping_start() noexcept
    {
        return !data_ ? nullptr : data_ - mapping_offset();
    }

    [[nodiscard]] const_pointer get_mapping_start() const noexcept
    {
        return !data_ ? nullptr : data_ - mapping_offset();
    }

    /**
     * The destructor syncs changes to disk if `AccessMode` is `write`, but not
     * if it's `read`. Uses if constexpr to select behavior at compile time.
     */
    void conditional_sync();
};

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

/**
 * This is the basis for all read-only mmap objects and should be preferred over
 * directly using `basic_mmap`.
 */
template<typename ByteT>
using basic_mmap_source = basic_mmap<access_mode::read, ByteT>;

/**
 * This is the basis for all read-write mmap objects and should be preferred over
 * directly using `basic_mmap`.
 */
template<typename ByteT>
using basic_mmap_sink = basic_mmap<access_mode::write, ByteT>;

/**
 * These aliases cover the most common use cases, both representing a raw byte stream
 * (either with a char or an unsigned char/uint8_t).
 */
using mmap_source = basic_mmap_source<char>;
using ummap_source = basic_mmap_source<unsigned char>;
using bmmap_source = basic_mmap_source<std::byte>;

using mmap_sink = basic_mmap_sink<char>;
using ummap_sink = basic_mmap_sink<unsigned char>;
using bmmap_sink = basic_mmap_sink<std::byte>;

/**
 * Convenience factory method that constructs a mapping for any `basic_mmap` or
 * `basic_mmap` type.
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
 * Convenience factory method.
 *
 * MappingToken may be a String (`std::string`, `std::string_view`, `const char*`,
 * `std::filesystem::path`, `std::vector<char>`, or similar), or a
 * `mmap_source::handle_type`.
 */
template<typename MappingToken>
mmap_source make_mmap_source(const MappingToken& token, mmap_source::size_type offset,
        mmap_source::size_type length, std::error_code& error)
{
    return make_mmap<mmap_source>(token, offset, length, error);
}

template<typename MappingToken>
mmap_source make_mmap_source(const MappingToken& token, std::error_code& error)
{
    return make_mmap_source(token, 0, map_entire_file, error);
}

/**
 * Convenience factory method.
 *
 * MappingToken may be a String (`std::string`, `std::string_view`, `const char*`,
 * `std::filesystem::path`, `std::vector<char>`, or similar), or a
 * `mmap_sink::handle_type`.
 */
template<typename MappingToken>
mmap_sink make_mmap_sink(const MappingToken& token, mmap_sink::size_type offset,
        mmap_sink::size_type length, std::error_code& error)
{
    return make_mmap<mmap_sink>(token, offset, length, error);
}

template<typename MappingToken>
mmap_sink make_mmap_sink(const MappingToken& token, std::error_code& error)
{
    return make_mmap_sink(token, 0, map_entire_file, error);
}

} // namespace mio

#include "detail/mmap.ipp"

#endif // MIO_MMAP_HEADER
