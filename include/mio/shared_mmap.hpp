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

#include "mio/mmap.hpp"

#include <cassert>
#include <system_error> // std::error_code
#include <memory> // std::shared_ptr

namespace mio {

/**
 * Exposes (nearly) the same interface as `basic_mmap`, but endows it with
 * `std::shared_ptr` semantics.
 *
 * This is not the default behaviour of `basic_mmap` to avoid allocating on the heap if
 * shared semantics are not required.
 */
template<
    access_mode AccessMode,
    typename ByteT
> class basic_shared_mmap
{
    using impl_type = basic_mmap<AccessMode, ByteT>;
    std::shared_ptr<impl_type> pimpl_;

public:
    using value_type = typename impl_type::value_type;
    using size_type = typename impl_type::size_type;
    using reference = typename impl_type::reference;
    using const_reference = typename impl_type::const_reference;
    using pointer = typename impl_type::pointer;
    using const_pointer = typename impl_type::const_pointer;
    using difference_type = typename impl_type::difference_type;
    using iterator = typename impl_type::iterator;
    using const_iterator = typename impl_type::const_iterator;
    using reverse_iterator = typename impl_type::reverse_iterator;
    using const_reverse_iterator = typename impl_type::const_reverse_iterator;
    using iterator_category = typename impl_type::iterator_category;
    using handle_type = typename impl_type::handle_type;
    using mmap_type = impl_type;

    basic_shared_mmap() = default;
    basic_shared_mmap(const basic_shared_mmap&) = default;
    basic_shared_mmap& operator=(const basic_shared_mmap&) = default;
    basic_shared_mmap(basic_shared_mmap&&) = default;
    basic_shared_mmap& operator=(basic_shared_mmap&&) = default;

    /** Takes ownership of an existing mmap object. */
    basic_shared_mmap(mmap_type&& mmap)
        : pimpl_(std::make_shared<mmap_type>(std::move(mmap)))
    {}

    /** Takes ownership of an existing mmap object. */
    basic_shared_mmap& operator=(mmap_type&& mmap)
    {
        pimpl_ = std::make_shared<mmap_type>(std::move(mmap));
        return *this;
    }

    /** Initializes this object with an already established shared mmap. */
    basic_shared_mmap(std::shared_ptr<mmap_type> mmap) : pimpl_(std::move(mmap)) {}

    /** Initializes this object with an already established shared mmap. */
    basic_shared_mmap& operator=(std::shared_ptr<mmap_type> mmap)
    {
        pimpl_ = std::move(mmap);
        return *this;
    }

#ifdef __cpp_exceptions
    /**
     * The same as invoking the `map` function, except any error that may occur
     * while establishing the mapping is wrapped in a `std::system_error` and is
     * thrown.
     */
    basic_shared_mmap(const std::filesystem::path& path, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        map(path, offset, length, error);
        if(error) { throw std::system_error(error); }
    }

    /**
     * Overload for const char* to prevent implicit conversion to handle_type (void* on Windows).
     */
    basic_shared_mmap(const char* path, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        if (!path) {
            error = std::make_error_code(std::errc::invalid_argument);
        } else {
            map(std::filesystem::path(path), offset, length, error);
        }
        if(error) { throw std::system_error(error); }
    }

    /**
     * The same as invoking the `map` function, except any error that may occur
     * while establishing the mapping is wrapped in a `std::system_error` and is
     * thrown.
     */
    basic_shared_mmap(const handle_type handle, const size_type offset = 0, const size_type length = map_entire_file)
    {
        std::error_code error;
        map(handle, offset, length, error);
        if(error) { throw std::system_error(error); }
    }
#endif // __cpp_exceptions

    /**
     * If this is a read-write mapping and the last reference to the mapping,
     * the destructor invokes sync. Regardless of the access mode, unmap is
     * invoked as a final step.
     */
    ~basic_shared_mmap() = default;

    /** Returns the underlying `std::shared_ptr` instance that holds the mmap. */
    [[nodiscard]] std::shared_ptr<mmap_type> get_shared_ptr() { return pimpl_; }

    /**
     * On UNIX systems 'file_handle' and 'mapping_handle' are the same. On Windows,
     * however, a mapped region of a file gets its own handle, which is returned by
     * 'mapping_handle'.
     */
    [[nodiscard]] handle_type file_handle() const noexcept
    {
        return pimpl_ ? pimpl_->file_handle() : invalid_handle;
    }

    [[nodiscard]] handle_type mapping_handle() const noexcept
    {
        return pimpl_ ? pimpl_->mapping_handle() : invalid_handle;
    }

    /** Returns whether a valid memory mapping has been created. */
    [[nodiscard]] bool is_open() const noexcept { return pimpl_ && pimpl_->is_open(); }

    /** Returns true if a mapping was established. */
    [[nodiscard]] bool is_mapped() const noexcept { return pimpl_ && pimpl_->is_mapped(); }

    /**
     * Returns true if no mapping was established, that is, conceptually the
     * same as though the length that was mapped was 0. This function is
     * provided so that this class has Container semantics.
     */
    [[nodiscard]] bool empty() const noexcept { return !pimpl_ || pimpl_->empty(); }

    /**
     * `size` and `length` both return the logical length, i.e. the number of bytes
     * user requested to be mapped, while `mapped_length` returns the actual number of
     * bytes that were mapped which is a multiple of the underlying operating system's
     * page allocation granularity.
     */
    [[nodiscard]] size_type size() const noexcept { return pimpl_ ? pimpl_->length() : 0; }
    [[nodiscard]] size_type length() const noexcept { return pimpl_ ? pimpl_->length() : 0; }
    [[nodiscard]] size_type mapped_length() const noexcept
    {
        return pimpl_ ? pimpl_->mapped_length() : 0;
    }

    /**
     * Returns a pointer to the first requested byte, or `nullptr` if no memory mapping
     * exists. Non-const version only available for write access mode.
     * Calling on an empty shared_mmap is undefined behaviour.
     */
    [[nodiscard]] pointer data() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const data() requires write access");
        assert(pimpl_ && "data() called on empty shared_mmap");
        return pimpl_->data();
    }
    [[nodiscard]] const_pointer data() const noexcept { return pimpl_ ? pimpl_->data() : nullptr; }

    /**
     * Returns an iterator to the first requested byte, if a valid memory mapping
     * exists, otherwise this function call is undefined behaviour.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] iterator begin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const begin() requires write access");
        assert(pimpl_ && "begin() called on empty shared_mmap");
        return pimpl_->begin();
    }
    [[nodiscard]] const_iterator begin() const noexcept {
        assert(pimpl_ && "begin() called on empty shared_mmap");
        return pimpl_->begin();
    }
    [[nodiscard]] const_iterator cbegin() const noexcept {
        assert(pimpl_ && "cbegin() called on empty shared_mmap");
        return pimpl_->cbegin();
    }

    /**
     * Returns an iterator one past the last requested byte, if a valid memory mapping
     * exists, otherwise this function call is undefined behaviour.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] iterator end() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const end() requires write access");
        assert(pimpl_ && "end() called on empty shared_mmap");
        return pimpl_->end();
    }
    [[nodiscard]] const_iterator end() const noexcept {
        assert(pimpl_ && "end() called on empty shared_mmap");
        return pimpl_->end();
    }
    [[nodiscard]] const_iterator cend() const noexcept {
        assert(pimpl_ && "cend() called on empty shared_mmap");
        return pimpl_->cend();
    }

    /**
     * Returns a reverse iterator to the last memory mapped byte, if a valid
     * memory mapping exists, otherwise this function call is undefined
     * behaviour. Non-const version only available for write access mode.
     */
    [[nodiscard]] reverse_iterator rbegin() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rbegin() requires write access");
        assert(pimpl_ && "rbegin() called on empty shared_mmap");
        return pimpl_->rbegin();
    }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept {
        assert(pimpl_ && "rbegin() called on empty shared_mmap");
        return pimpl_->rbegin();
    }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept {
        assert(pimpl_ && "crbegin() called on empty shared_mmap");
        return pimpl_->crbegin();
    }

    /**
     * Returns a reverse iterator past the first mapped byte, if a valid memory
     * mapping exists, otherwise this function call is undefined behaviour.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] reverse_iterator rend() noexcept {
        static_assert(AccessMode == access_mode::write, "non-const rend() requires write access");
        assert(pimpl_ && "rend() called on empty shared_mmap");
        return pimpl_->rend();
    }
    [[nodiscard]] const_reverse_iterator rend() const noexcept {
        assert(pimpl_ && "rend() called on empty shared_mmap");
        return pimpl_->rend();
    }
    [[nodiscard]] const_reverse_iterator crend() const noexcept {
        assert(pimpl_ && "crend() called on empty shared_mmap");
        return pimpl_->crend();
    }

    /**
     * Returns a reference to the `i`th byte from the first requested byte (as returned
     * by `data`). If this is invoked when no valid memory mapping has been created
     * prior to this call, undefined behaviour ensues.
     * Non-const version only available for write access mode.
     */
    [[nodiscard]] reference operator[](const size_type i) noexcept {
        static_assert(AccessMode == access_mode::write, "non-const operator[] requires write access");
        assert(pimpl_ && "operator[] called on empty shared_mmap");
        return (*pimpl_)[i];
    }
    [[nodiscard]] const_reference operator[](const size_type i) const noexcept {
        assert(pimpl_ && "operator[] called on empty shared_mmap");
        return (*pimpl_)[i];
    }

#if __cplusplus >= 202002L
    /** Returns the mapped memory as a std::span. */
    [[nodiscard]] std::span<const value_type> as_span() const noexcept {
        return pimpl_ ? pimpl_->as_span() : std::span<const value_type>{};
    }

    /** Returns the mapped memory as a mutable std::span (write mode only). */
    [[nodiscard]] std::span<value_type> as_span() noexcept {
        static_assert(AccessMode == access_mode::write, "mutable as_span() requires write access");
        assert(pimpl_ && "as_span() called on empty shared_mmap");
        return pimpl_->as_span();
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
        const size_type length, std::error_code& error)
    {
        map_impl(path, offset, length, error);
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
        map_impl(path, 0, map_entire_file, error);
    }

    /**
     * Overload for const char* to handle null pointers safely and prevent
     * implicit conversion to handle_type (void* on Windows).
     */
    void map(const char* path, const size_type offset,
        const size_type length, std::error_code& error)
    {
        if (!path) {
            error = std::make_error_code(std::errc::invalid_argument);
            return;
        }
        map_impl(std::filesystem::path(path), offset, length, error);
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
        map_impl(std::filesystem::path(path), 0, map_entire_file, error);
    }

    /**
     * Establishes a memory mapping with AccessMode. If the mapping is unsuccessful, the
     * reason is reported via `error` and the object remains in a state as if this
     * function hadn't been called.
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
        const size_type length, std::error_code& error)
    {
        map_impl(handle, offset, length, error);
    }

    /**
     * Establishes a memory mapping with AccessMode. If the mapping is unsuccessful, the
     * reason is reported via `error` and the object remains in a state as if this
     * function hadn't been called.
     *
     * `handle`, which must be a valid file handle, which is used to memory map the
     * requested region. Upon failure, `error` is set to indicate the reason and the
     * object remains in an unmapped state.
     *
     * The entire file is mapped.
     */
    void map(const handle_type handle, std::error_code& error)
    {
        map_impl(handle, 0, map_entire_file, error);
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
    void unmap() { if(pimpl_) pimpl_->unmap(); }

    void swap(basic_shared_mmap& other) noexcept { pimpl_.swap(other.pimpl_); }

    /**
     * Flushes the memory mapped page to disk. Errors are reported via `error`.
     * Only available for write access mode.
     */
    void sync(std::error_code& error) {
        static_assert(AccessMode == access_mode::write, "sync() requires write access");
        if(pimpl_) pimpl_->sync(error);
    }

    /** All operators compare the underlying `basic_mmap`'s addresses. */

    [[nodiscard]] friend bool operator==(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return a.pimpl_ == b.pimpl_;
    }

    [[nodiscard]] friend bool operator!=(const basic_shared_mmap& a, const basic_shared_mmap& b) noexcept
    {
        return !(a == b);
    }

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
    template<typename MappingToken>
    void map_impl(const MappingToken& token, const size_type offset,
        const size_type length, std::error_code& error)
    {
        if(!pimpl_)
        {
            mmap_type mmap = make_mmap<mmap_type>(token, offset, length, error);
            if(error) { return; }
            pimpl_ = std::make_shared<mmap_type>(std::move(mmap));
        }
        else
        {
            pimpl_->map(token, offset, length, error);
        }
    }
};

/**
 * This is the basis for all read-only mmap objects and should be preferred over
 * directly using basic_shared_mmap.
 */
template<typename ByteT>
using basic_shared_mmap_source = basic_shared_mmap<access_mode::read, ByteT>;

/**
 * This is the basis for all read-write mmap objects and should be preferred over
 * directly using basic_shared_mmap.
 */
template<typename ByteT>
using basic_shared_mmap_sink = basic_shared_mmap<access_mode::write, ByteT>;

/**
 * These aliases cover the most common use cases, both representing a raw byte stream
 * (either with a char or an unsigned char/uint8_t).
 */
using shared_mmap_source = basic_shared_mmap_source<char>;
using shared_ummap_source = basic_shared_mmap_source<unsigned char>;
using shared_bmmap_source = basic_shared_mmap_source<std::byte>;

using shared_mmap_sink = basic_shared_mmap_sink<char>;
using shared_ummap_sink = basic_shared_mmap_sink<unsigned char>;
using shared_bmmap_sink = basic_shared_mmap_sink<std::byte>;

} // namespace mio

#endif // MIO_SHARED_MMAP_HEADER
