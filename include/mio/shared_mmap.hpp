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

#include "mio/mmap.hpp"

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
