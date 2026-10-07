/*
 * MIT License
 *
 * Copyright (c) 2019-2026 Andrzej Lis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include "vdefines.h"
#include "vhash.h"
#include <boost/algorithm/string.hpp>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

namespace vstd
{
template <typename U> std::string to_hex(U object)
{
    std::stringstream stream;
    stream << std::hex << object;
    return boost::to_upper_copy<std::string>(stream.str());
}

template <typename U> std::string to_hex(U* object)
{
    return to_hex(reinterpret_cast<std::uintptr_t>(object));
}

template <typename U> std::string to_hex(std::shared_ptr<U> object)
{
    if constexpr (std::is_function_v<U>)
    {
        return to_hex<U*>(object.get());
    }
    else
    {
        return to_hex<const void*>(const_cast<const void*>(static_cast<const volatile void*>(object.get())));
    }
}

template <typename... Args> std::string to_hex_hash(Args... args)
{
    return to_hex<std::size_t>(hash_combine(args...));
}
} // namespace vstd
