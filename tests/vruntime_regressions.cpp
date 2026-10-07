/*
 * MIT License
 *
 * Copyright (c) 2026 Andrzej Lis
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
#include "veventloop.h"

#include <cassert>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
using loop_type = vstd::event_loop<>;

void nestedPumpingExecutesAlreadyQueuedTasks()
{
    auto loop = std::make_shared<loop_type>();
    bool completed = false;
    bool observed = false;
    loop->invoke(
        [&]()
        {
            loop->runUntilIdle(3);
            observed = completed;
        });
    loop->invoke([&]() { completed = true; });
    loop->runUntilIdle();
    assert(observed);
}

void newlyPostedTasksWaitForNextBatch()
{
    auto loop = std::make_shared<loop_type>();
    std::vector<int> order;
    loop->invoke(
        [&]()
        {
            order.push_back(1);
            loop->runPostedTasks();
            loop->invoke([&]() { order.push_back(3); });
        });
    loop->invoke([&]() { order.push_back(2); });
    loop->runPostedTasks();
    assert((order == std::vector<int>{1, 2}));
    loop->runPostedTasks();
    assert((order == std::vector<int>{1, 2, 3}));
}

void nestedPumpingExecutesDueTimers()
{
    auto loop = std::make_shared<loop_type>();
    bool completed = false;
    bool observed = false;
    loop->delay(0,
                [&]()
                {
                    loop->runUntilIdle(3);
                    observed = completed;
                });
    loop->delay(0, [&]() { completed = true; });
    loop->runUntilIdle();
    assert(observed);
}

void nestedPredicatesSkipTheirOwnInFlightEvaluation()
{
    auto loop = std::make_shared<loop_type>();
    int predicateCalls = 0;
    int completed = 0;
    bool peerCompleted = false;
    loop->invoke_when(
        [&]()
        {
            ++predicateCalls;
            if (predicateCalls == 1)
            {
                loop->runUntilIdle(3);
            }
            return peerCompleted;
        },
        [&]() { ++completed; });
    loop->invoke_when([]() { return true; }, [&]() { peerCompleted = true; });
    loop->runUntilIdle();
    assert(predicateCalls == 1);
    assert(completed == 1);
}

void consumedConditionsAreNotEvaluatedFromOuterSnapshots()
{
    auto loop = std::make_shared<loop_type>();
    int peerPredicates = 0;
    int peerActions = 0;
    loop->invoke_when([]() { return true; }, [&]() { loop->runUntilIdle(3); });
    loop->invoke_when(
        [&]()
        {
            ++peerPredicates;
            return true;
        },
        [&]() { ++peerActions; });
    loop->runUntilIdle();
    assert(peerPredicates == 1);
    assert(peerActions == 1);
}

void conditionalCancellationDuringPredicateSuppressesAction()
{
    auto loop = std::make_shared<loop_type>();
    bool called = false;
    loop_type::connection pending;
    pending = loop->scheduleWhen(
        [&]()
        {
            pending.reset();
            return true;
        },
        [&]() { called = true; });
    loop->runUntilIdle();
    assert(!called);
    assert(loop->getConditionalTaskCount() == 0);
}

void conditionalPredicatesRetainTheirStateBetweenPasses()
{
    auto loop = std::make_shared<loop_type>();
    bool called = false;
    loop->invoke_when([attempts = 0]() mutable { return ++attempts == 2; }, [&]() { called = true; });
    loop->runReady();
    assert(!called);
    loop->runReady();
    assert(called);
}

void conditionalPredicatesRecoverAfterExceptionHandlerCopyFails()
{
    struct ThrowingCopyHandler
    {
        bool* throwOnCopy;
        int* copies;
        int* calls;

        ThrowingCopyHandler(bool& throwOnCopy, int& copies, int& calls)
            : throwOnCopy(&throwOnCopy), copies(&copies), calls(&calls)
        {
        }
        ThrowingCopyHandler(const ThrowingCopyHandler& other)
            : throwOnCopy(other.throwOnCopy), copies(other.copies), calls(other.calls)
        {
            ++*copies;
            if (*throwOnCopy)
            {
                throw std::runtime_error("exception handler copy failure");
            }
        }
        ThrowingCopyHandler(ThrowingCopyHandler&&) = default;
        void operator()(std::exception_ptr) const
        {
            ++*calls;
        }
    };

    auto loop = std::make_shared<loop_type>();
    bool throwOnCopy = false;
    int handlerCopies = 0;
    int handlerCalls = 0;
    loop->setExceptionHandler(ThrowingCopyHandler(throwOnCopy, handlerCopies, handlerCalls));
    handlerCopies = 0;
    throwOnCopy = true;
    int predicateCalls = 0;
    int actions = 0;
    loop->invoke_when(
        [&]()
        {
            if (++predicateCalls == 1)
            {
                throw std::runtime_error("predicate failure");
            }
            return true;
        },
        [&]() { ++actions; });
    bool escaped = false;
    try
    {
        loop->runReady();
    }
    catch (...)
    {
        escaped = true;
    }
    assert(predicateCalls == 1 && actions == 0);
    assert(loop->getConditionalTaskCount() == 1);
    throwOnCopy = false;
    assert(loop->runReady() == 1);
    assert(predicateCalls == 2 && actions == 1);
    assert(loop->getConditionalTaskCount() == 0);
    assert(!escaped && handlerCopies == 1 && handlerCalls == 0);

    loop->invoke([]() { throw std::runtime_error("task failure"); });
    assert(loop->runPostedTasks() == 1);
    assert(handlerCopies == 2 && handlerCalls == 1);
}

void rescheduledDeferredTasksStayWithinBatchBudget()
{
    auto loop = std::make_shared<loop_type>();
    int timerCalls = 0;
    int conditionCalls = 0;
    std::function<void()> timer;
    timer = [&]()
    {
        ++timerCalls;
        if (timerCalls < 128)
        {
            loop->delay(0, timer);
        }
    };
    std::function<void()> condition;
    condition = [&]()
    {
        ++conditionCalls;
        if (conditionCalls < 128)
        {
            loop->invoke_when([]() { return true; }, condition);
        }
    };
    loop->delay(0, timer);
    loop->invoke_when([]() { return true; }, condition);
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        loop->runReady();
        assert(timerCalls == iteration + 1);
        assert(conditionCalls == iteration + 1);
    }
}

void frameDisconnectSkipsSnapshottedCallback()
{
    auto loop = std::make_shared<loop_type>();
    loop->setFps(1000000);
    int calls = 0;
    loop_type::connection second;
    auto first = loop->connectFrameCallback([&](int) { second.reset(); });
    second = loop->connectFrameCallback([&](int) { ++calls; });
    assert(loop->runFrame());
    assert(calls == 0);
    assert(loop->getFrameCallbackCount() == 1);
}

void eventDisconnectSkipsSnapshottedCallback()
{
    auto loop = std::make_shared<loop_type>();
    const auto eventType = SDL_RegisterEvents(1);
    assert(eventType != static_cast<Uint32>(-1));
    int calls = 0;
    loop_type::connection second;
    auto first = loop->connectEventCallback(
        [&](SDL_Event* event)
        {
            if (event->type == eventType)
            {
                second.reset();
            }
            return false;
        });
    second = loop->connectEventCallback(
        [&](SDL_Event* event)
        {
            if (event->type == eventType)
            {
                ++calls;
            }
            return false;
        });
    SDL_Event event;
    SDL_zero(event);
    event.type = eventType;
    assert(SDL_PushEvent(&event) == 1);
    loop->runReady();
    assert(calls == 0);
    assert(loop->getEventCallbackCount() == 1);
}

void releasedConnectionsRemainRegistered()
{
    auto loop = std::make_shared<loop_type>();
    loop->setFps(1000000);
    int calls = 0;
    loop->connectFrameCallback([&](int) { ++calls; }).release();
    assert(loop->runFrame());
    assert(calls == 1);
}

void connectionDoesNotRetainDestroyedLoopCallbacks()
{
    auto loop = std::make_shared<loop_type>();
    auto lifetime = std::make_shared<int>(1);
    std::weak_ptr<int> weakLifetime = lifetime;
    auto connection = loop->connectFrameCallback([lifetime](int) {});
    lifetime.reset();
    loop.reset();
    assert(weakLifetime.expired());
    connection.reset();
}

void repostedTasksStayWithinBatchBudget()
{
    auto loop = std::make_shared<loop_type>();
    int calls = 0;
    std::function<void()> callback;
    callback = [&]()
    {
        ++calls;
        if (calls < 128)
        {
            loop->invoke(callback);
        }
    };
    loop->invoke(callback);
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        assert(loop->runPostedTasks() == 1);
        assert(calls == iteration + 1);
        assert(loop->getPendingTaskCount() == (iteration < 127 ? 1 : 0));
    }
}

void dispatchDoesNotCopyRegisteredCallables()
{
    struct CountedCallback
    {
        int* copies;
        int* calls;

        CountedCallback(int& copies, int& calls) : copies(&copies), calls(&calls) {}
        CountedCallback(const CountedCallback& other) : copies(other.copies), calls(other.calls)
        {
            ++*copies;
        }
        void operator()(int) const
        {
            ++*calls;
        }
    };

    auto loop = std::make_shared<loop_type>();
    loop->setFps(1000000);
    int copies = 0;
    int calls = 0;
    auto connection = loop->connectFrameCallback(CountedCallback(copies, calls));
    copies = 0;
    for (int frame = 0; frame < 32; ++frame)
    {
        assert(loop->runFrame());
    }
    assert(calls == 32);
    assert(copies == 0);
}
} // namespace

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    if (selected == "all" || selected == "nested")
    {
        nestedPumpingExecutesAlreadyQueuedTasks();
        newlyPostedTasksWaitForNextBatch();
    }
    if (selected == "all" || selected == "frame-disconnect")
    {
        frameDisconnectSkipsSnapshottedCallback();
    }
    if (selected == "all" || selected == "event-disconnect")
    {
        eventDisconnectSkipsSnapshottedCallback();
    }
    if (selected == "all" || selected == "nested-timers")
    {
        nestedPumpingExecutesDueTimers();
    }
    if (selected == "all" || selected == "nested-predicates")
    {
        nestedPredicatesSkipTheirOwnInFlightEvaluation();
    }
    if (selected == "all" || selected == "condition-snapshot")
    {
        consumedConditionsAreNotEvaluatedFromOuterSnapshots();
    }
    if (selected == "all" || selected == "condition-cancel")
    {
        conditionalCancellationDuringPredicateSuppressesAction();
    }
    if (selected == "all" || selected == "stateful-predicate")
    {
        conditionalPredicatesRetainTheirStateBetweenPasses();
    }
    if (selected == "all" || selected == "condition-exception")
    {
        conditionalPredicatesRecoverAfterExceptionHandlerCopyFails();
    }
    if (selected == "all")
    {
        releasedConnectionsRemainRegistered();
        connectionDoesNotRetainDestroyedLoopCallbacks();
        repostedTasksStayWithinBatchBudget();
        dispatchDoesNotCopyRegisteredCallables();
        rescheduledDeferredTasksStayWithinBatchBudget();
    }
}
