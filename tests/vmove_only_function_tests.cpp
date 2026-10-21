/*
 * Copyright (c) 2026 Andrzej Lis
 * SPDX-License-Identifier: MIT
 */
#include "vmove_only_function.h"
#include "vtask.h"
#include "vthreadpool.h"

#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
using Callback = vstd::move_only_function<int()>;
#if defined(VSTD_FORCE_PORTABLE_MOVE_ONLY_FUNCTION) && defined(__cpp_lib_move_only_function)
static_assert(!std::is_same_v<Callback, std::move_only_function<int()>>);
#endif
static_assert(!std::is_copy_constructible_v<Callback>);
static_assert(!std::is_copy_assignable_v<Callback>);
static_assert(std::is_nothrow_move_constructible_v<Callback>);
static_assert(std::is_nothrow_move_assignable_v<Callback>);
static_assert(!std::is_constructible_v<Callback, int>);
static_assert(!std::is_constructible_v<vstd::move_only_function<const int&()>, int (*)()>);

struct Lifetime
{
    explicit Lifetime(int& destroyed) : destroyed(destroyed) {}
    ~Lifetime()
    {
        ++destroyed;
    }
    int& destroyed;
};

void moveOnlyOwnershipAndRepeatedCalls()
{
    int destroyed = 0;
    Callback original = [owner = std::make_unique<Lifetime>(destroyed), count = 0]() mutable { return ++count; };
    assert(original() == 1);
    Callback moved = std::move(original);
    assert(!original);
    assert(moved() == 2);
    Callback assigned;
    assigned = std::move(moved);
    assert(!moved);
    assert(assigned() == 3);
    assigned = nullptr;
    assert(!assigned);
    assert(destroyed == 1);

    Callback replacement = [owner = std::make_unique<Lifetime>(destroyed)] { return 4; };
    replacement = [] { return 5; };
    assert(destroyed == 2);
    assert(replacement() == 5);
}

void nullTargetsAndSwap()
{
    Callback empty;
    Callback null = nullptr;
    int (*function)() = nullptr;
    Callback nullFunction = function;
    assert(empty == nullptr);
    assert(nullptr == null);
    assert(!nullFunction);
    Callback filled = [] { return 7; };
    swap(empty, filled);
    assert(empty() == 7);
    assert(!filled);
}

void forwardingResultsAndExceptions()
{
    vstd::move_only_function<std::unique_ptr<int>(std::unique_ptr<int>)> pass = [](std::unique_ptr<int> value)
    { return value; };
    assert(*pass(std::make_unique<int>(8)) == 8);
    int value = 9;
    vstd::move_only_function<int&(int&)> reference = [](int& input) -> int& { return input; };
    reference(value) = 10;
    assert(value == 10);
    vstd::move_only_function<void()> discardResult = [] { return 11; };
    discardResult();
    vstd::move_only_function<vstd::task<int>()> coroutine = []() -> vstd::task<int> { co_return 14; };
    vstd::task_group group;
    assert(group.spawn(coroutine()).get() == 14);
    Callback throws = []() -> int { throw std::runtime_error("callback failure"); };
    try
    {
        throws();
        assert(false);
    }
    catch (const std::runtime_error&)
    {
    }
}

void schedulerOwnsAndDropsCallbacks()
{
    int destroyed = 0;
    std::vector<vstd::scheduler::function> pending;
    vstd::scheduler scheduler(
        [&](vstd::scheduler::function callback)
        {
            pending.push_back(std::move(callback));
            return true;
        });
    assert(scheduler.dispatch([owner = std::make_unique<Lifetime>(destroyed)] {}));
    assert(destroyed == 0);
    pending.clear();
    assert(destroyed == 1);
    int calls = 0;
    assert(scheduler.dispatch([owner = std::make_unique<int>(12), &calls] { calls += *owner; }));
    pending.front()();
    assert(calls == 12);
}

void threadPoolRunsMoveOnlyCallback()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    std::promise<int> completed;
    auto result = completed.get_future();
    assert(pool->submit([owner = std::make_unique<int>(13), completed = std::move(completed)]() mutable
                        { completed.set_value(*owner); }));
    assert(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(result.get() == 13);
    pool->stop();
}
} // namespace

int main()
{
    moveOnlyOwnershipAndRepeatedCalls();
    nullTargetsAndSwap();
    forwardingResultsAndExceptions();
    schedulerOwnsAndDropsCallbacks();
    threadPoolRunsMoveOnlyCallback();
}
