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
