/*
 * MIT License
 *
 * Copyright (c) 2019 Andrzej Lis
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

#include "vfunctional.h"
#include "vthread.h"
#include "vutil.h"
#include <boost/range/adaptors.hpp>
#include <concepts>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace vstd
{
namespace detail
{
template <typename T>
concept void_type = std::same_as<T, void>;
template <typename T>
concept non_void_type = !void_type<T>;

template <typename G>
concept future_callable = requires {
    typename vstd::function_traits<G>::return_type;
    typename vstd::function_traits<G>::first_arg;
};

template <typename G>
concept returns_void = future_callable<G> && void_type<typename vstd::function_traits<G>::return_type>;

template <typename G>
concept returns_value = future_callable<G> && non_void_type<typename vstd::function_traits<G>::return_type>;

template <typename G>
concept takes_void = future_callable<G> && void_type<typename vstd::function_traits<G>::first_arg>;

template <typename G>
concept takes_value = future_callable<G> && non_void_type<typename vstd::function_traits<G>::first_arg>;

template <future_callable G>
    requires returns_void<G> && takes_void<G>
auto normalize(G f)
{
    return [f](void*) -> void*
    {
        f();
        return nullptr;
    };
}

template <future_callable G>
    requires returns_value<G> && takes_void<G>
auto normalize(G f)
{
    return [f](void*) { return f(); };
}

template <future_callable G>
    requires returns_void<G> && takes_value<G>
auto normalize(G f)
{
    using argument_type = typename vstd::function_traits<G>::first_arg;
    return [f](argument_type t) -> void*
    {
        f(t);
        return nullptr;
    };
}

template <future_callable G>
    requires returns_value<G> && takes_value<G>
auto normalize(G f)
{
    return f;
}

template <future_callable sig> struct normalized_function
{
    typedef std::function<typename function_traits<decltype(normalize(std::declval<sig>()))>::return_type(
        typename function_traits<decltype(normalize(std::declval<sig>()))>::first_arg)>
        type;
};

template <typename ret, typename arg> struct function_type
{
    typedef std::function<ret(arg)> type;
};

template <typename ret> struct function_type<ret, void>
{
    typedef std::function<ret()> type;
};

template <typename return_type, typename argument_type>
class ccall : public std::enable_shared_from_this<ccall<return_type, argument_type>>
{
    typedef typename function_type<return_type, argument_type>::type function_target;
    typedef typename normalized_function<function_target>::type normalized_target;
    typedef std::function<void(typename function_traits<normalized_target>::return_type)> on_result;
    using stored_return_type = std::conditional_t<void_type<return_type>, std::nullptr_t, return_type>;
    using stored_argument_type = std::conditional_t<void_type<argument_type>, std::nullptr_t, argument_type>;

    volatile bool completed = false;
    std::recursive_mutex mutex;
    std::condition_variable_any _condition;
    std::optional<stored_return_type> result;
    std::optional<stored_argument_type> argument;

  public:
    template <future_callable F, typename G> ccall(F func, G caller) : _target(normalize(func)), _caller(caller) {}

    template <typename X = argument_type>
        requires non_void_type<X>
    X getArgument()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        if (!argument.has_value())
        {
            throw std::logic_error("argument is not set");
        }
        return std::move(*argument);
    }

    template <typename X = argument_type>
        requires void_type<X>
    void getArgument()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
    }

    template <typename X = argument_type>
        requires non_void_type<X>
    void setArgument(X x)
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        argument.emplace(std::move(x));
    }

    template <typename X = argument_type>
        requires void_type<X>
    void setArgument(void*)
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        argument.emplace(nullptr);
    }

    template <typename X = return_type>
        requires non_void_type<X>
    void setResult(X t)
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        result.emplace(std::move(t));
        completed = true;
        if (_on_result)
        {
            _on_result(*result);
        }
        _condition.notify_all();
    }

    template <typename X = return_type>
        requires non_void_type<X>
    X getResult()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        _condition.wait(lock, [this]() { return completed && result.has_value(); });
        return std::move(*result);
    }

    template <typename X = return_type>
        requires void_type<X>
    void setResult()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        result.emplace(nullptr);
        completed = true;
        if (_on_result)
        {
            _on_result(nullptr);
        }
        _condition.notify_all();
    }

    template <typename X = return_type>
        requires void_type<X>
    void* getResult()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        _condition.wait(lock, [this]() { return completed && result.has_value(); });
        return nullptr;
    }

    void call()
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        auto self = this->shared_from_this();
        vstd::functional::call(_caller,
                               [self]()
                               {
                                   if constexpr (void_type<return_type>)
                                   {
                                       vstd::functional::call(self->_target, self->getNormalizedArgument());
                                       self->setResult();
                                   }
                                   else
                                   {
                                       self->setResult(
                                           vstd::functional::call(self->_target, self->getNormalizedArgument()));
                                   }
                               });
    }

    void onResult(on_result cb)
    {
        std::unique_lock<std::recursive_mutex> lock(mutex);
        if (completed)
        {
            cb(getResult());
        }
        else
        {
            _on_result = cb;
        }
    }

  private:
    template <typename X = argument_type>
        requires non_void_type<X>
    X getNormalizedArgument()
    {
        return getArgument();
    }

    template <typename X = argument_type>
        requires void_type<X>
    void* getNormalizedArgument()
    {
        return nullptr;
    }

    normalized_target _target;

    on_result _on_result;

    std::function<void(std::function<void()>)> _caller;
};
} // namespace detail

namespace detail
{
template <future_callable func> auto make_async(func f)
{
    return std::make_shared<
        ccall<typename function_traits<func>::return_type, typename function_traits<func>::first_arg>>(
        f, call_async<std::function<void()>>);
}

template <future_callable func> auto make_later(func f)
{
    return std::make_shared<
        ccall<typename function_traits<func>::return_type, typename function_traits<func>::first_arg>>(
        f, call_later<std::function<void()>>);
}

template <future_callable func> auto make_now(func f)
{
    return std::make_shared<
        ccall<typename function_traits<func>::return_type, typename function_traits<func>::first_arg>>(
        f, call_now<std::function<void()>>);
}
} // namespace detail

template <typename return_type, typename argument_type>
class future : public std::enable_shared_from_this<future<return_type, argument_type>>
{
  public:
    typedef typename detail::function_type<return_type, argument_type>::type function;

    explicit future(std::shared_ptr<detail::ccall<return_type, argument_type>> call, bool start = true) : _call(call)
    {
        if (start)
        {
            _call->call();
        }
    }

    auto get()
    {
        return _call->getResult();
    }

    template <typename G> auto thenLater(G g)
    {
        return chain_call(detail::make_later(g));
    }

    template <typename G> auto thenAsync(G g)
    {
        return chain_call(detail::make_async(g));
    }

  private:
    std::shared_ptr<detail::ccall<return_type, argument_type>> _call;

    template <typename _return_type, typename _first_arg>
    auto chain_call(std::shared_ptr<detail::ccall<_return_type, _first_arg>> _new_call)
    {
        auto _self = this->shared_from_this();
        _self->_call->onResult(
            [_new_call](auto arg)
            {
                _new_call->setArgument(arg);
                _new_call->call();
            });
        return std::make_shared<future<_return_type, _first_arg>>(_new_call, false);
    }
};

namespace detail
{
template <typename return_type, typename first_arg>
auto make_future(std::shared_ptr<detail::ccall<return_type, first_arg>> call)
{
    return std::make_shared<vstd::future<return_type, first_arg>>(call);
}
} // namespace detail

template <detail::future_callable Func> auto later(Func f)
{
    return detail::make_future(detail::make_later(vstd::make_function(f)));
}

template <detail::future_callable Func> auto async(Func f)
{
    return detail::make_future(detail::make_async(vstd::make_function(f)));
}

template <detail::future_callable Func> auto now(Func f)
{
    return detail::make_future(detail::make_now(vstd::make_function(f)));
}

template <detail::future_callable F, typename Arg = typename vstd::function_traits<F>::template arg<0>::type>
auto wrap_later(F f)
{
    return [f](Arg a) { return later(vstd::bind(f, a)); };
}

template <detail::future_callable F, typename Arg = typename vstd::function_traits<F>::template arg<0>::type>
auto wrap_async(F f)
{
    return [f](Arg a) { return async(vstd::bind(f, a)); };
}

template <typename Range> auto join(Range range)
{
    return async(
        [range]()
        { return collect(collect(range) | boost::adaptors::transformed([](auto future) { return future->get(); })); });
}
} // namespace vstd
