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

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace vstd
{
namespace partial
{
struct pb_tag
{
};

template <typename T> using is_prebinder = std::is_base_of<pb_tag, typename std::remove_reference<T>::type>;

template <int N, int... S> struct seq : seq<N - 1, N, S...>
{
};
template <int... S> struct seq<0, S...>
{
    typedef seq type;
};

template <typename T> T& dispatchee(T& t, std::false_type)
{
    return t;
}

template <typename T> auto dispatchee(T& t, std::true_type) -> decltype(t())
{
    return t();
}

template <typename T> auto expand(T& t) -> decltype(dispatchee(t, is_prebinder<T>()))
{
    return dispatchee(t, is_prebinder<T>());
}

template <typename T> using expand_type = decltype(expand(std::declval<T&>()));

namespace detail
{
template <bool Invocable, bool HasTemporary, typename F, typename... Args> struct InvocationResult
{
    static constexpr bool materializes = false;
    static constexpr bool can_return = false;
};

template <bool HasTemporary, typename F, typename... Args> struct InvocationResult<true, HasTemporary, F, Args...>
{
    using invocation_type = std::invoke_result_t<F, Args...>;
    static constexpr bool materializes = std::is_reference_v<invocation_type> && HasTemporary;
    using type = std::conditional_t<materializes, std::remove_cvref_t<invocation_type>, invocation_type>;
    static constexpr bool can_return = !materializes || std::is_constructible_v<type, invocation_type>;
};
} // namespace detail

template <typename f, typename... ltypes> struct prebinder : public pb_tag
{
    std::tuple<f, ltypes...> closure;
    typedef typename seq<sizeof...(ltypes)>::type sequence;

    prebinder(f F, ltypes... largs) : closure(std::move(F), std::move(largs)...) {}

    template <typename... rtypes>
    using invocation_result = std::invoke_result_t<f&, expand_type<ltypes>..., rtypes&&...>;

    template <typename... rtypes>
    using result_traits = detail::InvocationResult<std::is_invocable_v<f&, expand_type<ltypes>..., rtypes&&...>,
                                                   (!std::is_reference_v<expand_type<ltypes>> || ...), f&,
                                                   expand_type<ltypes>..., rtypes&&...>;

    template <typename... rtypes> static constexpr bool materializes_result = result_traits<rtypes...>::materializes;

    template <typename... rtypes> using result_type = typename result_traits<rtypes...>::type;

    template <int... S, typename... rtypes>
        requires(result_traits<rtypes...>::can_return)
    result_type<rtypes...> apply(seq<0, S...>, rtypes&&... rargs)
    {
        if constexpr (materializes_result<rtypes...>)
        {
            return result_type<rtypes...>(
                std::invoke(std::get<0>(closure), expand(std::get<S>(closure))..., std::forward<rtypes>(rargs)...));
        }
        else
        {
            return std::invoke(std::get<0>(closure), expand(std::get<S>(closure))..., std::forward<rtypes>(rargs)...);
        }
    }

    template <typename... rtypes>
        requires(result_traits<rtypes...>::can_return)
    result_type<rtypes...> operator()(rtypes&&... rargs)
    {
        return apply(sequence(), std::forward<rtypes>(rargs)...);
    }
};

template <typename f, typename... ltypes> prebinder<f, ltypes...> bind(f F, ltypes... largs)
{
    return prebinder<f, ltypes...>(std::move(F), std::move(largs)...);
}
} // namespace partial
} // namespace vstd
