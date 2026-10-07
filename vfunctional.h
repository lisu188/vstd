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
#include <concepts>
#include <functional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>

#include "vtraits.h"

namespace vstd
{
namespace functional
{
namespace detail
{
template <typename T> struct IsReferenceWrapper : std::false_type
{
};

template <typename T> struct IsReferenceWrapper<std::reference_wrapper<T>> : std::true_type
{
};

template <typename F, typename... Args> constexpr bool allowsMemberReferenceResult()
{
    if constexpr (std::is_member_pointer_v<F> && std::is_reference_v<std::invoke_result_t<F&, Args&...>>)
    {
        using Receiver = std::tuple_element_t<0, std::tuple<Args...>>;
        return std::is_pointer_v<Receiver> || IsReferenceWrapper<Receiver>::value;
    }
    return true;
}
} // namespace detail

template <typename F, typename... Args>
    requires std::invocable<F&, Args&...> && (detail::allowsMemberReferenceResult<F, Args...>())
decltype(auto) call(F f, Args... args)
{
    return std::invoke(f, args...);
}

template <typename Return, typename Container, typename Func> Return map(Container& container, Func f)
{
    Return ret;
    for (typename Container::value_type val : container)
    {
        ret.insert(f(val));
    }
    return ret;
}

template <typename Container, typename Func> void foreach (Container& container, Func f)
{
    for (auto val : container)
    {
        f(val);
    }
}

template <typename T, typename Container, typename Func> T sum(Container& container, Func f)
{
    T s = 0;
    for (auto val : container)
    {
        s += f(val);
    }
    return s;
}

template <typename T, typename Container> T sum(Container& container)
{
    T s = 0;
    for (auto val : container)
    {
        s += val;
    }
    return s;
}

template <typename Return, typename Container, typename Func> auto map_reduce(Container& container, Func f)
{
    std::unordered_map<Return, std::set<typename Container::value_type>> ret;
    for (auto val : container)
    {
        auto bucket = f(val);
        if (ret.find(bucket) == ret.end())
        {
            ret[bucket] = std::set<typename Container::value_type>();
        }
        ret[bucket].insert(val);
    }
    return ret;
}
} // namespace functional
} // namespace vstd
