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
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace vstd
{
class future_cancelled : public std::runtime_error
{
  public:
    future_cancelled() : std::runtime_error("future cancelled") {}
};

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

enum class future_state
{
    pending,
    value,
    exception,
    cancelled
};

template <typename return_type, typename argument_type>
class ccall : public std::enable_shared_from_this<ccall<return_type, argument_type>>
{
    typedef typename function_type<return_type, argument_type>::type function_target;
    typedef typename normalized_function<function_target>::type normalized_target;
    using normalized_return_type = typename function_traits<normalized_target>::return_type;
    typedef std::function<void(normalized_return_type)> on_result;
    typedef std::function<void(std::exception_ptr)> on_error;
    using stored_return_type = std::conditional_t<void_type<return_type>, std::nullptr_t, return_type>;
    using stored_argument_type = std::conditional_t<void_type<argument_type>, std::nullptr_t, argument_type>;

    struct continuation
    {
        on_result success;
        on_error failure;
    };

  public:
    template <future_callable F, typename G> ccall(F func, G caller) : _target(normalize(func)), _caller(caller) {}

    template <typename X = argument_type>
        requires non_void_type<X>
    X getArgument()
    {
        std::unique_lock lock(mutex);
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
    }

    template <typename X = argument_type>
        requires non_void_type<X>
    void setArgument(X x)
    {
        std::unique_lock lock(mutex);
        argument.emplace(std::move(x));
    }

    template <typename X = argument_type>
        requires void_type<X>
    void setArgument(void*)
    {
        std::unique_lock lock(mutex);
        argument.emplace(nullptr);
    }

    template <typename X = return_type>
        requires non_void_type<X>
    void setResult(X value)
    {
        std::vector<continuation> callbacks;
        std::optional<X> callbackValue;
        {
            std::unique_lock lock(mutex);
            if (state != future_state::pending)
            {
                return;
            }
            result.emplace(std::move(value));
            state = future_state::value;
            callbackValue = *result;
            callbacks.swap(continuations);
        }
        condition.notify_all();
        for (auto& callback : callbacks)
        {
            if (callback.success)
            {
                callback.success(*callbackValue);
            }
        }
    }

    template <typename X = return_type>
        requires void_type<X>
    void setResult()
    {
        std::vector<continuation> callbacks;
        {
            std::unique_lock lock(mutex);
            if (state != future_state::pending)
            {
                return;
            }
            result.emplace(nullptr);
            state = future_state::value;
            callbacks.swap(continuations);
        }
        condition.notify_all();
        for (auto& callback : callbacks)
        {
            if (callback.success)
            {
                callback.success(nullptr);
            }
        }
    }

    void setException(std::exception_ptr error)
    {
        if (!error)
        {
            error = std::make_exception_ptr(std::runtime_error("future failed without an exception"));
        }
        std::vector<continuation> callbacks;
        {
            std::unique_lock lock(mutex);
            if (state != future_state::pending)
            {
                return;
            }
            exception = error;
            state = future_state::exception;
            callbacks.swap(continuations);
        }
        condition.notify_all();
        for (auto& callback : callbacks)
        {
            if (callback.failure)
            {
                callback.failure(error);
            }
        }
    }

    void cancel()
    {
        auto error = std::make_exception_ptr(future_cancelled());
        std::vector<continuation> callbacks;
        {
            std::unique_lock lock(mutex);
            if (state != future_state::pending)
            {
                return;
            }
            exception = error;
            state = future_state::cancelled;
            callbacks.swap(continuations);
        }
        condition.notify_all();
        for (auto& callback : callbacks)
        {
            if (callback.failure)
            {
                callback.failure(error);
            }
        }
    }

    template <typename X = return_type>
        requires non_void_type<X>
    X getResult()
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this]() { return state != future_state::pending; });
        rethrowIfFailed();
        return *result;
    }

    template <typename X = return_type>
        requires void_type<X>
    void* getResult()
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this]() { return state != future_state::pending; });
        rethrowIfFailed();
        return nullptr;
    }

    void wait()
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this]() { return state != future_state::pending; });
    }

    template <typename Rep, typename Period> bool waitFor(const std::chrono::duration<Rep, Period>& timeout)
    {
        std::unique_lock lock(mutex);
        return condition.wait_for(lock, timeout, [this]() { return state != future_state::pending; });
    }

    bool isReady() const
    {
        std::unique_lock lock(mutex);
        return state != future_state::pending;
    }

    bool hasError() const
    {
        std::unique_lock lock(mutex);
        return state == future_state::exception || state == future_state::cancelled;
    }

    void call()
    {
        {
            std::unique_lock lock(mutex);
            if (started || state != future_state::pending)
            {
                return;
            }
            started = true;
        }

        auto self = this->shared_from_this();
        try
        {
            vstd::functional::call(
                _caller,
                [self]()
                {
                    try
                    {
                        if constexpr (void_type<return_type>)
                        {
                            vstd::functional::call(self->_target, self->getNormalizedArgument());
                            self->setResult();
                        }
                        else
                        {
                            self->setResult(vstd::functional::call(self->_target, self->getNormalizedArgument()));
                        }
                    }
                    catch (...)
                    {
                        self->setException(std::current_exception());
                    }
                });
        }
        catch (...)
        {
            setException(std::current_exception());
        }
    }

    void onResult(on_result callback)
    {
        onSettled(std::move(callback), [](std::exception_ptr) {});
    }

    void onSettled(on_result success, on_error failure)
    {
        future_state settledState;
        std::optional<stored_return_type> settledResult;
        std::exception_ptr settledException;
        {
            std::unique_lock lock(mutex);
            if (state == future_state::pending)
            {
                continuations.push_back({std::move(success), std::move(failure)});
                return;
            }
            settledState = state;
            settledException = exception;
            if constexpr (non_void_type<return_type>)
            {
                settledResult = result;
            }
        }

        if (settledState == future_state::value)
        {
            if (success)
            {
                if constexpr (void_type<return_type>)
                {
                    success(nullptr);
                }
                else
                {
                    success(*settledResult);
                }
            }
        }
        else if (failure)
        {
            failure(settledException);
        }
    }

  private:
    void rethrowIfFailed() const
    {
        if (state == future_state::exception || state == future_state::cancelled)
        {
            std::rethrow_exception(exception);
        }
    }

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

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::optional<stored_return_type> result;
    std::optional<stored_argument_type> argument;
    std::exception_ptr exception;
    future_state state = future_state::pending;
    bool started = false;
    std::vector<continuation> continuations;
    normalized_target _target;
    std::function<void(std::function<void()>)> _caller;
};

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

    void wait()
    {
        _call->wait();
    }

    template <typename Rep, typename Period> bool waitFor(const std::chrono::duration<Rep, Period>& timeout)
    {
        return _call->waitFor(timeout);
    }

    bool isReady() const
    {
        return _call->isReady();
    }

    bool hasError() const
    {
        return _call->hasError();
    }

    void cancel()
    {
        _call->cancel();
    }

    template <typename G> auto thenLater(G g)
    {
        return chainCall(detail::make_later(g));
    }

    template <typename G> auto thenAsync(G g)
    {
        return chainCall(detail::make_async(g));
    }

    template <typename G> auto thenNow(G g)
    {
        return chainCall(detail::make_now(g));
    }

    template <typename Success, typename Failure> void onComplete(Success success, Failure failure)
    {
        _call->onSettled(
            [success = std::move(success)](auto value) mutable
            {
                if constexpr (detail::void_type<return_type>)
                {
                    success();
                }
                else
                {
                    success(value);
                }
            },
            std::move(failure));
    }

  private:
    std::shared_ptr<detail::ccall<return_type, argument_type>> _call;

    template <typename new_return_type, typename new_argument_type>
    auto chainCall(std::shared_ptr<detail::ccall<new_return_type, new_argument_type>> newCall)
    {
        _call->onSettled(
            [newCall](auto argument)
            {
                newCall->setArgument(argument);
                newCall->call();
            },
            [newCall](std::exception_ptr error) { newCall->setException(error); });
        return std::make_shared<future<new_return_type, new_argument_type>>(newCall, false);
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

template <typename T> auto make_ready_future(T value)
{
    return now([value = std::move(value)]() { return value; });
}

inline auto make_ready_future()
{
    return now([]() {});
}

template <typename T> auto make_exceptional_future(std::exception_ptr error)
{
    auto call = detail::make_now(vstd::make_function([]() -> T { throw std::logic_error("unreachable future body"); }));
    call->setException(error);
    return std::make_shared<future<T, void>>(call, false);
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

template <typename T, typename Arg>
auto when_all(const std::vector<std::shared_ptr<future<T, Arg>>>& futures)
    requires detail::non_void_type<T>
{
    using result_type = std::vector<T>;
    auto completion = detail::make_now(vstd::make_function([]() -> result_type { return {}; }));
    auto result = std::make_shared<future<result_type, void>>(completion, false);

    struct state_type
    {
        std::mutex mutex;
        std::size_t remaining = 0;
        bool settled = false;
        std::vector<std::optional<T>> values;
        std::shared_ptr<detail::ccall<result_type, void>> completion;
    };

    auto state = std::make_shared<state_type>();
    state->remaining = futures.size();
    state->values.resize(futures.size());
    state->completion = completion;

    if (futures.empty())
    {
        completion->setResult({});
        return result;
    }

    for (std::size_t index = 0; index < futures.size(); ++index)
    {
        auto current = futures[index];
        if (!current)
        {
            completion->setException(std::make_exception_ptr(std::invalid_argument("when_all received null future")));
            return result;
        }
        current->onComplete(
            [state, index](T value)
            {
                std::optional<result_type> ready;
                {
                    std::unique_lock lock(state->mutex);
                    if (state->settled)
                    {
                        return;
                    }
                    state->values[index] = std::move(value);
                    if (--state->remaining == 0)
                    {
                        state->settled = true;
                        result_type values;
                        values.reserve(state->values.size());
                        for (auto& item : state->values)
                        {
                            values.push_back(*item);
                        }
                        ready = std::move(values);
                    }
                }
                if (ready)
                {
                    state->completion->setResult(std::move(*ready));
                }
            },
            [state](std::exception_ptr error)
            {
                bool fail = false;
                {
                    std::unique_lock lock(state->mutex);
                    if (!state->settled)
                    {
                        state->settled = true;
                        fail = true;
                    }
                }
                if (fail)
                {
                    state->completion->setException(error);
                }
            });
    }
    return result;
}

template <typename Arg> auto when_all(const std::vector<std::shared_ptr<future<void, Arg>>>& futures)
{
    auto completion = detail::make_now(vstd::make_function([]() {}));
    auto result = std::make_shared<future<void, void>>(completion, false);

    struct state_type
    {
        std::mutex mutex;
        std::size_t remaining = 0;
        bool settled = false;
        std::shared_ptr<detail::ccall<void, void>> completion;
    };

    auto state = std::make_shared<state_type>();
    state->remaining = futures.size();
    state->completion = completion;

    if (futures.empty())
    {
        completion->setResult();
        return result;
    }

    for (const auto& current : futures)
    {
        if (!current)
        {
            completion->setException(std::make_exception_ptr(std::invalid_argument("when_all received null future")));
            return result;
        }
        current->onComplete(
            [state]()
            {
                bool ready = false;
                {
                    std::unique_lock lock(state->mutex);
                    if (state->settled)
                    {
                        return;
                    }
                    if (--state->remaining == 0)
                    {
                        state->settled = true;
                        ready = true;
                    }
                }
                if (ready)
                {
                    state->completion->setResult();
                }
            },
            [state](std::exception_ptr error)
            {
                bool fail = false;
                {
                    std::unique_lock lock(state->mutex);
                    if (!state->settled)
                    {
                        state->settled = true;
                        fail = true;
                    }
                }
                if (fail)
                {
                    state->completion->setException(error);
                }
            });
    }
    return result;
}

template <typename Range> auto join(Range range)
{
    return async(
        [range]()
        { return collect(collect(range) | boost::adaptors::transformed([](auto future) { return future->get(); })); });
}
} // namespace vstd
