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
#include "vfuture.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace vstd
{
std::function<void(std::function<void()>)> get_call_later_handler()
{
    return [](std::function<void()> function) { function(); };
}

std::function<void(std::function<void()>)> get_call_async_handler()
{
    return [](std::function<void()> function)
    {
        static auto pool = std::make_shared<vstd::thread_pool<2>>()->start();
        pool->execute(std::move(function));
    };
}

std::function<void(std::function<void()>)> get_call_now_handler()
{
    return [](std::function<void()> function) { function(); };
}

std::function<void(std::function<void()>)> get_call_later_block_handler()
{
    return [](std::function<void()> function) { function(); };
}

std::function<void(std::function<bool()>)> get_wait_until_handler()
{
    return [](std::function<bool()> predicate)
    {
        while (!predicate())
        {
            std::this_thread::yield();
        }
    };
}

std::function<void(std::function<bool()>, std::function<void()>)> get_call_when_handler()
{
    return [](std::function<bool()> predicate, std::function<void()> function)
    {
        if (predicate())
        {
            function();
        }
    };
}

std::function<void(int, std::function<void()>)> get_call_delayed_async_handler()
{
    return [](int, std::function<void()> function) { get_call_async_handler()(std::move(function)); };
}

std::function<void(int, std::function<void()>)> get_call_delayed_later_handler()
{
    return [](int, std::function<void()> function) { function(); };
}
} // namespace vstd

namespace
{
struct ThrowingCopy
{
    ThrowingCopy() = default;
    ThrowingCopy(const ThrowingCopy&)
    {
        throw std::runtime_error("result copy failure");
    }
    ThrowingCopy(ThrowingCopy&&) = default;
    ThrowingCopy& operator=(const ThrowingCopy&) = default;
    ThrowingCopy& operator=(ThrowingCopy&&) = default;
};

struct CountedThrowingCopy
{
    static inline int copies = 0;
    static inline int fail_at = 0;
    int value = 17;

    CountedThrowingCopy() = default;
    CountedThrowingCopy(const CountedThrowingCopy& other) : value(other.value)
    {
        if (++copies == fail_at)
        {
            throw std::runtime_error("aggregate copy failure");
        }
    }
    CountedThrowingCopy(CountedThrowingCopy&&) = default;
    CountedThrowingCopy& operator=(const CountedThrowingCopy&) = default;
    CountedThrowingCopy& operator=(CountedThrowingCopy&&) = default;
};

void testCancellationObservers()
{
    auto call = vstd::detail::make_now(vstd::make_function([]() { return 1; }));
    auto pending = std::make_shared<vstd::future<int, void>>(call, false);
    pending->onComplete([](int) {}, [](std::exception_ptr) { throw std::runtime_error("observer failure"); });
    auto child = pending->thenNow([](int value) { return value + 1; });
    bool observed = false;
    pending->onComplete([](int) {}, [&observed](std::exception_ptr) { observed = true; });

    bool cancellation_threw = false;
    try
    {
        pending->cancel();
    }
    catch (...)
    {
        cancellation_threw = true;
    }
    assert(!cancellation_threw);
    assert(observed);
    assert(pending->isReady() && pending->hasError());
    assert(child->isReady() && child->hasError());
    bool cancellation_rethrown = false;
    try
    {
        child->get();
    }
    catch (const vstd::future_cancelled&)
    {
        cancellation_rethrown = true;
    }
    assert(cancellation_rethrown);
}

void testResultCopyFailure()
{
    auto call = vstd::detail::make_now(vstd::make_function([]() { return ThrowingCopy{}; }));
    auto pending = std::make_shared<vstd::future<ThrowingCopy, void>>(call, false);
    pending->onComplete([](ThrowingCopy) {}, [](std::exception_ptr) { throw std::runtime_error("observer failure"); });
    auto child = pending->thenNow([](ThrowingCopy) { return 1; });
    int failures = 0;
    pending->onComplete([](ThrowingCopy) {}, [&failures](std::exception_ptr) { ++failures; });

    call->call();

    assert(pending->isReady() && pending->hasError());
    assert(child->isReady() && child->hasError());
    assert(failures == 1);
    pending->onComplete([](ThrowingCopy) {}, [&failures](std::exception_ptr) { ++failures; });
    assert(failures == 2);
    bool copy_failure_rethrown = false;
    try
    {
        child->get();
    }
    catch (const std::runtime_error& error)
    {
        copy_failure_rethrown = std::string(error.what()) == "result copy failure";
    }
    assert(copy_failure_rethrown);
}

void testAggregateCopyFailure()
{
    bool downstream_failure = false;
    for (int fail_at = 1; fail_at <= 12; ++fail_at)
    {
        auto call = vstd::detail::make_now(vstd::make_function([]() { return CountedThrowingCopy{}; }));
        auto pending = std::make_shared<vstd::future<CountedThrowingCopy, void>>(call, false);
        auto combined = vstd::when_all(std::vector{pending});
        auto child = combined->thenNow([](std::vector<CountedThrowingCopy> values) { return values.front().value; });

        CountedThrowingCopy::copies = 0;
        CountedThrowingCopy::fail_at = fail_at;
        call->setResult(CountedThrowingCopy{});
        CountedThrowingCopy::fail_at = 0;

        assert(pending->isReady());
        assert(combined->isReady());
        assert(child->isReady());
        downstream_failure = downstream_failure || (!pending->hasError() && combined->hasError());
        if (child->hasError())
        {
            bool copy_failure_rethrown = false;
            try
            {
                child->get();
            }
            catch (const std::runtime_error& error)
            {
                copy_failure_rethrown = std::string(error.what()) == "aggregate copy failure";
            }
            assert(copy_failure_rethrown);
        }
        else
        {
            assert(child->get() == 17);
        }
    }
    assert(downstream_failure);
}

void testReadyResultCopyFailure()
{
    auto ready = vstd::make_ready_future(CountedThrowingCopy{});
    bool succeeded = false;
    bool failed = false;
    bool escaped = false;
    CountedThrowingCopy::copies = 0;
    CountedThrowingCopy::fail_at = 1;
    try
    {
        ready->onComplete([&succeeded](CountedThrowingCopy) { succeeded = true; },
                          [&ready, &failed](std::exception_ptr error)
                          {
                              assert(ready->isReady() && !ready->hasError());
                              try
                              {
                                  std::rethrow_exception(error);
                              }
                              catch (const std::runtime_error& exception)
                              {
                                  failed = std::string(exception.what()) == "aggregate copy failure";
                              }
                          });
    }
    catch (...)
    {
        escaped = true;
    }
    CountedThrowingCopy::fail_at = 0;
    assert(!escaped && failed && !succeeded);
    assert(ready->get().value == 17);

    CountedThrowingCopy::copies = 0;
    CountedThrowingCopy::fail_at = 1;
    auto child = ready->thenNow([](CountedThrowingCopy value) { return value.value; });
    CountedThrowingCopy::fail_at = 0;
    assert(ready->isReady() && !ready->hasError());
    assert(child->isReady() && child->hasError());
}
} // namespace

int main()
{
    testCancellationObservers();
    testResultCopyFailure();
    testAggregateCopyFailure();
    testReadyResultCopyFailure();

    auto ready = vstd::make_ready_future(7);
    assert(ready->isReady());
    assert(ready->get() == 7);
    assert(ready->get() == 7);

    auto first = ready->thenNow([](int value) { return value + 1; });
    auto second = ready->thenNow([](int value) { return value + 2; });
    assert(first->get() == 8);
    assert(second->get() == 9);

    auto failed = vstd::async([]() -> int { throw std::runtime_error("future failure"); });
    assert(failed->waitFor(std::chrono::seconds(2)));
    assert(failed->hasError());
    bool rethrown = false;
    try
    {
        failed->get();
    }
    catch (const std::runtime_error& error)
    {
        rethrown = std::string(error.what()) == "future failure";
    }
    assert(rethrown);

    auto propagated = failed->thenNow([](int value) { return value + 1; });
    rethrown = false;
    try
    {
        propagated->get();
    }
    catch (const std::runtime_error& error)
    {
        rethrown = std::string(error.what()) == "future failure";
    }
    assert(rethrown);

    std::vector<std::shared_ptr<vstd::future<int, void>>> values = {
        vstd::make_ready_future(3), vstd::make_ready_future(5), vstd::make_ready_future(8)};
    auto combined = vstd::when_all(values);
    assert((combined->get() == std::vector<int>{3, 5, 8}));

    auto cancelledCall = vstd::detail::make_now(vstd::make_function([]() { return 1; }));
    auto cancelled = std::make_shared<vstd::future<int, void>>(cancelledCall, false);
    cancelled->cancel();
    bool cancellationRethrown = false;
    try
    {
        cancelled->get();
    }
    catch (const vstd::future_cancelled&)
    {
        cancellationRethrown = true;
    }
    assert(cancellationRethrown);

    std::atomic_bool survived = false;
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    pool->execute([]() { throw std::runtime_error("worker failure"); });
    pool->execute([&survived]() { survived.store(true, std::memory_order_relaxed); });
    for (int i = 0; i < 200 && !survived.load(std::memory_order_relaxed); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(survived.load(std::memory_order_relaxed));
    pool->stop();

    return 0;
}
