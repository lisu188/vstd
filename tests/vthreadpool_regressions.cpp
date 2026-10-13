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
#include "vthreadpool.h"

#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <string_view>

namespace
{
using namespace std::chrono_literals;

struct CustomWorker
{
    template <typename Queue> void operator()(std::stop_token token, std::shared_ptr<Queue> queue)
    {
        vstd::detail::worker_thread{}(token, std::move(queue));
    }
};

void idlePoolReleasesOwnership()
{
    std::weak_ptr<vstd::thread_pool<2>> weak;
    {
        auto pool = std::make_shared<vstd::thread_pool<2>>()->start();
        weak = pool;
        std::promise<void> completed;
        auto result = completed.get_future();
        pool->execute([&]() { completed.set_value(); });
        assert(result.wait_for(5s) == std::future_status::ready);
    }
    assert(weak.expired());
}

void taskCanStopItsOwnPool()
{
    auto pool = std::make_shared<vstd::thread_pool<2>>()->start();
    std::promise<void> stopped;
    auto result = stopped.get_future();
    pool->execute(
        [&]()
        {
            pool->stop();
            stopped.set_value();
        });
    assert(result.wait_for(5s) == std::future_status::ready);
}

void workerStopReturnsBeforeDependentPeerFinishes()
{
    auto pool = std::make_shared<vstd::thread_pool<2>>()->start();
    std::promise<void> peerStarted;
    auto started = peerStarted.get_future().share();
    std::promise<void> stopReturned;
    auto stopped = stopReturned.get_future().share();
    std::promise<void> peerFinished;
    auto finished = peerFinished.get_future();
    pool->execute(
        [&]()
        {
            peerStarted.set_value();
            assert(stopped.wait_for(5s) == std::future_status::ready);
            peerFinished.set_value();
        });
    pool->execute(
        [&]()
        {
            assert(started.wait_for(5s) == std::future_status::ready);
            pool->stop();
            stopReturned.set_value();
        });
    assert(finished.wait_for(5s) == std::future_status::ready);
}

void retiredWorkerCanStopRestartedPool()
{
    auto pool = std::make_shared<vstd::thread_pool<2>>()->start();
    std::promise<void> oldStopped;
    auto stopped = oldStopped.get_future();
    std::promise<void> restarted;
    auto ready = restarted.get_future().share();
    std::promise<void> newStarted;
    auto started = newStarted.get_future().share();
    std::promise<void> retiredStopReturned;
    auto returned = retiredStopReturned.get_future().share();
    std::promise<void> oldFinished;
    auto oldResult = oldFinished.get_future();
    std::promise<void> newFinished;
    auto newResult = newFinished.get_future();
    pool->execute(
        [&]()
        {
            pool->stop();
            oldStopped.set_value();
            assert(ready.wait_for(5s) == std::future_status::ready);
            assert(started.wait_for(5s) == std::future_status::ready);
            pool->stop();
            retiredStopReturned.set_value();
            oldFinished.set_value();
        });
    assert(stopped.wait_for(5s) == std::future_status::ready);
    pool->start();
    pool->execute(
        [&]()
        {
            newStarted.set_value();
            assert(returned.wait_for(5s) == std::future_status::ready);
            newFinished.set_value();
        });
    restarted.set_value();
    assert(oldResult.wait_for(5s) == std::future_status::ready);
    assert(newResult.wait_for(5s) == std::future_status::ready);
}

void poolCanBeDestroyedByItsLastTaskOwner()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    std::weak_ptr<vstd::thread_pool<1>> weak = pool;
    std::promise<void> release;
    auto ready = release.get_future().share();
    std::promise<void> completed;
    auto result = completed.get_future();
    pool->execute(
        [owner = pool, ready, &completed]() mutable
        {
            assert(ready.wait_for(5s) == std::future_status::ready);
            owner.reset();
            completed.set_value();
        });
    pool.reset();
    release.set_value();
    assert(result.wait_for(5s) == std::future_status::ready);
    assert(weak.expired());
}

void completedTaskReleasesCapturedOwners()
{
    struct TaskOwner
    {
        std::shared_ptr<vstd::thread_pool<1>> pool;
        std::promise<void> released;

        ~TaskOwner()
        {
            pool.reset();
            released.set_value();
        }
    };

    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    std::weak_ptr<vstd::thread_pool<1>> weak = pool;
    auto owner = std::make_shared<TaskOwner>();
    owner->pool = pool;
    auto released = owner->released.get_future();
    std::promise<void> start;
    auto ready = start.get_future().share();
    pool->execute([owner, ready]() { assert(ready.wait_for(5s) == std::future_status::ready); });
    owner.reset();
    pool.reset();
    start.set_value();
    assert(released.wait_for(5s) == std::future_status::ready);
    assert(weak.expired());
}

void stoppedWorkersDoNotConsumeRestartedQueue()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    std::promise<void> oldStopped;
    auto stopped = oldStopped.get_future();
    std::promise<void> finishOld;
    auto finish = finishOld.get_future().share();
    std::promise<void> oldFinished;
    auto finished = oldFinished.get_future();
    pool->execute(
        [&]()
        {
            pool->stop();
            oldStopped.set_value();
            assert(finish.wait_for(5s) == std::future_status::ready);
            oldFinished.set_value();
        });
    assert(stopped.wait_for(5s) == std::future_status::ready);
    pool->start();
    std::promise<void> newCompleted;
    auto completed = newCompleted.get_future();
    pool->execute([&]() { newCompleted.set_value(); });
    assert(completed.wait_for(5s) == std::future_status::ready);
    finishOld.set_value();
    assert(finished.wait_for(5s) == std::future_status::ready);
    pool->stop();
}

void queuedTasksDrainAndRestart()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>();
    int completed = 0;
    pool->execute([&]() { ++completed; });
    pool->start();
    pool->execute([&]() { ++completed; });
    pool->stop();
    assert(completed == 2);
    pool->execute([&]() { completed = -100; });
    pool->start();
    pool->execute([&]() { ++completed; });
    pool->stop();
    assert(completed == 3);
}

void genericCustomWorkerRemainsSupported()
{
    auto pool = std::make_shared<vstd::thread_pool<1, CustomWorker>>()->start();
    std::promise<void> completed;
    auto result = completed.get_future();
    pool->execute(
        [&]()
        {
            pool->stop();
            completed.set_value();
        });
    assert(result.wait_for(5s) == std::future_status::ready);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    if (selected == "all" || selected == "ownership")
    {
        idlePoolReleasesOwnership();
        poolCanBeDestroyedByItsLastTaskOwner();
        completedTaskReleasesCapturedOwners();
    }
    if (selected == "all" || selected == "self-stop")
    {
        taskCanStopItsOwnPool();
        stoppedWorkersDoNotConsumeRestartedQueue();
    }
    if (selected == "all" || selected == "restart")
    {
        queuedTasksDrainAndRestart();
    }
    if (selected == "all" || selected == "dependent-peer")
    {
        workerStopReturnsBeforeDependentPeerFinishes();
    }
    if (selected == "all" || selected == "retired-worker")
    {
        retiredWorkerCanStopRestartedPool();
    }
    if (selected == "all" || selected == "custom-worker")
    {
        genericCustomWorkerRemainsSupported();
    }
}
