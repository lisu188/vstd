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
#include "vtask.h"
#include "vthreadpool.h"

#ifdef VSTD_TEST_EVENT_LOOP
#define SDL_MAIN_HANDLED
#include "veventloop.h"
#endif

#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <queue>
#include <thread>

namespace
{
class manual_queue : public std::enable_shared_from_this<manual_queue>
{
  public:
    bool invoke(vstd::scheduler::function function)
    {
        std::lock_guard lock(mutex);
        queue.push(std::move(function));
        return true;
    }

    bool submit(vstd::scheduler::function function)
    {
        return invoke(std::move(function));
    }

    std::size_t run()
    {
        std::queue<vstd::scheduler::function> snapshot;
        {
            std::lock_guard lock(mutex);
            snapshot.swap(queue);
        }
        const auto count = snapshot.size();
        while (!snapshot.empty())
        {
            auto function = std::move(snapshot.front());
            snapshot.pop();
            function();
        }
        return count;
    }

    void discard()
    {
        std::queue<vstd::scheduler::function> discarded;
        {
            std::lock_guard lock(mutex);
            discarded.swap(queue);
        }
    }

  private:
    std::mutex mutex;
    std::queue<vstd::scheduler::function> queue;
};

template <typename Exception, typename Function> void expectThrow(Function&& function)
{
    bool caught = false;
    try
    {
        function();
    }
    catch (const Exception&)
    {
        caught = true;
    }
    assert(caught);
}

vstd::task<int> queuedValue(std::shared_ptr<manual_queue> queue, int value, int& calls)
{
    ++calls;
    co_await vstd::post(queue);
    ++calls;
    co_return value;
}

vstd::task<int> observe(vstd::task_handle<int> child, std::vector<int>& values)
{
    const int value = co_await child;
    values.push_back(value);
    co_return value;
}

vstd::task<int> nested(std::shared_ptr<manual_queue> queue, int& calls)
{
    co_return (co_await queuedValue(queue, 17, calls)) + 1;
}

vstd::task<void> rejected(vstd::scheduler target, bool& resumed)
{
    co_await vstd::post(target);
    resumed = true;
}

vstd::task<int> ownedWork(std::shared_ptr<manual_queue> pool, std::unique_ptr<int> value, int& calls)
{
    co_return co_await vstd::work(pool,
                                  [value = std::move(value), &calls]
                                  {
                                      ++calls;
                                      return *value;
                                  });
}

vstd::task<void> boundedWork(std::shared_ptr<vstd::thread_pool<1>> pool, std::promise<void>& started,
                             std::shared_future<void> release, std::atomic<bool>& completed)
{
    auto token = co_await vstd::thisTaskStopToken();
    co_await vstd::work(pool,
                        [&started, release, &completed, token]
                        {
                            started.set_value();
                            release.wait();
                            assert(token.stop_requested());
                            completed.store(true);
                        });
}

vstd::task<void> failAtOnce()
{
    throw std::runtime_error("immediate failure");
    co_return;
}

vstd::task<void> selfJoin(vstd::task_group& group, bool& rejected)
{
    assert(group.ownsCurrentTask() && !group.finished());
    rejected = !group.join();
    group.requestStop();
    co_return;
}

vstd::task<void> nestedSelfJoin(std::shared_ptr<manual_queue> queue, vstd::task_group& group,
                                const vstd::task_handle<void>& ancestor, bool& rejected)
{
    co_await vstd::post(queue);
    assert(group.ownsCurrentTask() && !group.finished());
    assert(!ancestor.join());
    expectThrow<std::logic_error>([&] { ancestor.get(); });
    co_await selfJoin(group, rejected);
}

vstd::task<void> nestedSelfJoinRoot(std::shared_ptr<manual_queue> queue, vstd::task_group& group,
                                    const vstd::task_handle<void>& ancestor, bool& rejected)
{
    co_await nestedSelfJoin(queue, group, ancestor, rejected);
}

vstd::task<void> workerSelfJoin(std::shared_ptr<vstd::thread_pool<1>> pool, vstd::task_group& group, bool& rejected)
{
    co_await vstd::work(pool,
                        [&]
                        {
                            assert(group.ownsCurrentTask() && !group.finished());
                            rejected = !group.join();
                            group.requestStop();
                        });
}

void testOwnedWorkerSelfJoinAndScopeCleanup()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    vstd::task_group group;
    bool rejected = false;
    auto handle = group.spawn(workerSelfJoin(pool, group, rejected));
    assert(handle.join() && rejected && group.finished());
    expectThrow<vstd::task_cancelled>([&] { handle.get(); });
    std::promise<void> unowned;
    pool->execute(
        [&]
        {
            assert(!group.ownsCurrentTask());
            unowned.set_value();
        });
    unowned.get_future().wait();
    pool->stop();
}

vstd::task<void> synchronousChild(vstd::task_group& outer, vstd::task_group& inner, bool& observed)
{
    assert(outer.ownsCurrentTask() && inner.ownsCurrentTask());
    assert(!outer.join() && !inner.join());
    observed = true;
    co_return;
}

void spawnSynchronousChild(vstd::task_group& outer, bool& observed)
{
    vstd::task_group inner;
    auto child = inner.spawn(synchronousChild(outer, inner, observed));
    child.get();
    assert(child.finished() && inner.join() && !inner.ownsCurrentTask() && outer.ownsCurrentTask());
}

vstd::task<void> synchronousParent(vstd::task_group& outer, bool& observed)
{
    spawnSynchronousChild(outer, observed);
    co_return;
}

vstd::task<void> synchronousWorkerParent(std::shared_ptr<vstd::thread_pool<1>> pool, vstd::task_group& outer,
                                         bool& observed)
{
    co_await vstd::work(pool, [&] { spawnSynchronousChild(outer, observed); });
}

vstd::task<void> independentChild(std::shared_ptr<manual_queue> queue, vstd::task_group& outer, bool& owned)
{
    co_await vstd::post(queue);
    owned = outer.ownsCurrentTask();
}

vstd::task<void> startIndependentChild(std::shared_ptr<manual_queue> queue, vstd::task_group& outer,
                                       vstd::task_group& independent, vstd::task_handle<void>& child, bool& owned)
{
    child = independent.spawn(independentChild(queue, outer, owned));
    co_return;
}

void testSynchronousExecutionAncestryKeepsGroupsIndependent()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    for (const bool worker : {false, true})
    {
        vstd::task_group outer;
        bool observed = false;
        auto parent =
            outer.spawn(worker ? synchronousWorkerParent(pool, outer, observed) : synchronousParent(outer, observed));
        assert(parent.join() && observed && outer.finished());
        parent.get();
    }
    pool->stop();

    auto queue = std::make_shared<manual_queue>();
    vstd::task_group outer, independent;
    vstd::task_handle<void> child;
    bool owned = true;
    auto parent = outer.spawn(startIndependentChild(queue, outer, independent, child, owned));
    assert(parent.finished() && !child.ready());
    outer.requestStop();
    assert(!child.ready());
    queue->run();
    child.get();
    assert(child.finished() && !owned);
}

vstd::task<void> waitExecution(vstd::task_handle<int> child, bool& resumed)
{
    co_await child.waitFinished();
    resumed = true;
}

struct destruction_guard
{
    int& count;
    ~destruction_guard()
    {
        ++count;
    }
};

vstd::task<void> nestedRunningWork(std::shared_ptr<vstd::thread_pool<1>> pool, std::promise<void>& started,
                                   std::shared_future<void> release, std::atomic<bool>& completed, int& destroyed)
{
    destruction_guard guard{destroyed};
    co_await boundedWork(pool, started, release, completed);
}

vstd::task<int> workerAffinity(std::shared_ptr<vstd::thread_pool<1>> pool, std::thread::id main_thread)
{
    assert(std::this_thread::get_id() == main_thread);
    const int result = co_await vstd::work(pool,
                                           [main_thread]
                                           {
                                               assert(std::this_thread::get_id() != main_thread);
                                               return 12;
                                           });
    assert(std::this_thread::get_id() == main_thread);
    co_return result;
}

vstd::task<void> retained(std::shared_ptr<manual_queue> queue, std::shared_ptr<destruction_guard> guard)
{
    co_await vstd::post(queue);
    (void)guard;
}

void testLazyAndMultipleAwaiters()
{
    auto queue = std::make_shared<manual_queue>();
    auto first_queue = std::make_shared<manual_queue>();
    auto second_queue = std::make_shared<manual_queue>();
    int calls = 0;
    auto lazy = queuedValue(queue, 23, calls);
    assert(calls == 0);
    vstd::task_group group;
    auto handle = group.spawn(std::move(lazy));
    assert(calls == 1 && !handle.ready());
    std::vector<int> first_values, second_values;
    auto first = group.spawn(observe(handle, first_values), vstd::loopScheduler(first_queue));
    auto second = group.spawn(observe(handle, second_values), vstd::loopScheduler(second_queue));
    assert(queue->run() == 1);
    assert(calls == 2 && handle.finished());
    assert(handle.get() == 23 && handle.get() == 23);
    assert(!first.ready() && !second.ready());
    assert(first_queue->run() == 1);
    assert(first_values == std::vector<int>{23} && second_values.empty());
    assert(second_queue->run() == 1);
    assert(first.get() == 23 && second.get() == 23);
    assert(group.join());

    auto child = group.spawn(nested(queue, calls));
    assert(!child.ready());
    queue->run();
    assert(child.get() == 18);
}

void testRejectionAndDroppedTickets()
{
    vstd::task_group group;
    bool resumed = false;
    auto handle = group.spawn(rejected(vstd::scheduler([](vstd::scheduler::function) { return false; }), resumed));
    expectThrow<vstd::scheduler_rejected>([&] { handle.get(); });
    assert(handle.finished() && !resumed);

    auto queue = std::make_shared<manual_queue>();
    auto dropped = group.spawn(rejected(vstd::loopScheduler(queue), resumed));
    assert(!dropped.ready());
    queue->discard();
    expectThrow<vstd::scheduler_rejected>([&] { dropped.get(); });
    assert(dropped.finished() && !resumed);

    int calls = 0;
    auto discarded_work = group.spawn(ownedWork(queue, std::make_unique<int>(7), calls));
    queue->discard();
    expectThrow<vstd::scheduler_rejected>([&] { discarded_work.get(); });
    assert(discarded_work.finished() && calls == 0);

    auto child = group.spawn(queuedValue(queue, 31, calls));
    auto execution =
        group.spawn(waitExecution(child, resumed), vstd::scheduler([](vstd::scheduler::function) { return false; }));
    queue->run();
    expectThrow<vstd::scheduler_rejected>([&] { execution.get(); });
    assert(execution.finished() && !resumed);
}

void testQueuedCancellationAndMoveOnlyWork()
{
    auto pool = std::make_shared<manual_queue>();
    auto queue = std::make_shared<manual_queue>();
    vstd::task_group group;
    int calls = 0;
    auto cancelled = group.spawn(ownedWork(pool, std::make_unique<int>(91), calls), vstd::loopScheduler(queue));
    cancelled.requestStop();
    assert(cancelled.ready() && !cancelled.finished());
    expectThrow<vstd::task_cancelled>([&] { cancelled.get(); });
    pool->run();
    assert(calls == 0);
    queue->run();
    assert(cancelled.finished());

    auto success = group.spawn(ownedWork(pool, std::make_unique<int>(42), calls), vstd::loopScheduler(queue));
    pool->run();
    assert(!success.ready());
    queue->run();
    assert(success.get() == 42 && calls == 1);

    auto pending = group.spawn(ownedWork(pool, std::make_unique<int>(13), calls));
    std::promise<void> stop_entered, stop_release;
    auto release = stop_release.get_future().share();
    std::stop_callback observer(pending.stopToken(),
                                [&]
                                {
                                    expectThrow<vstd::task_cancelled>([&] { pending.get(); });
                                    stop_entered.set_value();
                                    release.wait();
                                });
    std::jthread stop([pending] { pending.requestStop(); });
    stop_entered.get_future().wait();
    pool->run();
    assert(calls == 1);
    stop_release.set_value();
    stop.join();
    assert(pending.finished());
}

void testFrameRetentionAndGroupStop()
{
    auto queue = std::make_shared<manual_queue>();
    int destroyed = 0;
    vstd::task_group group;
    {
        auto guard = std::make_shared<destruction_guard>(destroyed);
        auto handle = group.spawn(retained(queue, std::move(guard)));
    }
    assert(destroyed == 0);
    group.requestStop();
    assert(destroyed == 0);
    queue->run();
    assert(group.join() && destroyed == 1);
    expectThrow<vstd::task_cancelled>([&] { group.spawn(retained(queue, {})); });

    vstd::task_group self;
    bool rejected = false;
    auto handle = self.spawn(selfJoin(self, rejected));
    assert(rejected && handle.finished() && self.finished() && !self.ownsCurrentTask());

    vstd::task_group nested_group;
    vstd::task_handle<void> ancestor;
    bool nested_rejected = false;
    ancestor = nested_group.spawn(nestedSelfJoinRoot(queue, nested_group, ancestor, nested_rejected));
    assert(queue->run() == 1);
    for (int phase = 0; phase < 4 && !ancestor.finished(); ++phase)
    {
        queue->run();
    }
    assert(nested_rejected && ancestor.finished() && nested_group.finished() && !nested_group.ownsCurrentTask());
    expectThrow<vstd::task_cancelled>([&] { ancestor.get(); });
    expectThrow<vstd::task_cancelled>([&] { handle.get(); });
}

void testOrderedWhenAllAndFailureJoin()
{
    auto first_queue = std::make_shared<manual_queue>();
    auto second_queue = std::make_shared<manual_queue>();
    vstd::task_group group;
    int calls = 0;
    auto first = group.spawn(queuedValue(first_queue, 1, calls));
    auto second = group.spawn(queuedValue(second_queue, 2, calls));
    auto all = group.spawn(vstd::whenAll<int>({first, second}));
    second_queue->run();
    assert(!all.ready());
    first_queue->run();
    assert((all.get() == std::vector<int>{1, 2}));

    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    std::promise<void> started, release;
    std::atomic<bool> completed{false};
    auto running = group.spawn(boundedWork(pool, started, release.get_future().share(), completed));
    started.get_future().wait();
    auto failing = group.spawn(failAtOnce());
    auto joined = group.spawn(vstd::whenAll<void>({running, failing}));
    assert(running.ready() && !running.finished() && !joined.ready());
    assert(!completed.load());
    release.set_value();
    assert(joined.join());
    assert(completed.load() && running.finished());
    expectThrow<std::runtime_error>([&] { joined.get(); });
    try
    {
        joined.get();
    }
    catch (const std::runtime_error& error)
    {
        assert(std::string(error.what()) == "immediate failure");
    }
    pool->stop();
}

void testNestedCancellationAndWorkerAffinity()
{
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    vstd::task_group group;
    std::promise<void> started, release;
    std::atomic<bool> completed{false};
    int destroyed = 0;
    auto parent = group.spawn(nestedRunningWork(pool, started, release.get_future().share(), completed, destroyed));
    started.get_future().wait();
    parent.requestStop();
    assert(parent.ready() && !parent.finished() && destroyed == 0);
    release.set_value();
    assert(parent.join());
    assert(completed.load() && destroyed == 1);
    expectThrow<vstd::task_cancelled>([&] { parent.get(); });

    auto queue = std::make_shared<manual_queue>();
    auto affinity = group.spawn(workerAffinity(pool, std::this_thread::get_id()), vstd::loopScheduler(queue));
    pool->stop();
    assert(!affinity.ready());
    assert(queue->run() == 1);
    assert(affinity.get() == 12);

    auto rejected = group.spawn(workerAffinity(pool, std::this_thread::get_id()));
    expectThrow<vstd::scheduler_rejected>([&] { rejected.get(); });
    assert(rejected.finished());
}

void testWhenAllDrainsAfterSchedulerRejection()
{
    auto queue = std::make_shared<manual_queue>();
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    vstd::task_group group;
    std::promise<void> started, release;
    std::atomic<bool> completed{false};
    auto first = group.spawn(retained(queue, {}));
    auto second = group.spawn(boundedWork(pool, started, release.get_future().share(), completed));
    started.get_future().wait();
    auto joined = group.spawn(vstd::whenAll<void>({first, second}),
                              vstd::scheduler([](vstd::scheduler::function) { return false; }));
    queue->run();
    assert(second.ready() && !second.finished());
    assert(!joined.ready() && !joined.finished());
    release.set_value();
    assert(joined.join() && completed.load());
    expectThrow<vstd::scheduler_rejected>([&] { joined.get(); });
    assert(group.finished());
    pool->stop();
}

void testCompletionCancellationRaces()
{
    for (int iteration = 0; iteration < 200; ++iteration)
    {
        auto queue = std::make_shared<manual_queue>();
        vstd::task_group group;
        int calls = 0;
        auto handle = group.spawn(queuedValue(queue, 3, calls));
        std::jthread cancellation([handle] { handle.requestStop(); });
        queue->run();
        cancellation.join();
        queue->run();
        assert(handle.join() && calls >= 1 && calls <= 2);
        try
        {
            assert(handle.get() == 3);
        }
        catch (const vstd::task_cancelled&)
        {
        }
    }
}

#ifdef VSTD_TEST_EVENT_LOOP
vstd::task<void> eventFlow(std::shared_ptr<vstd::event_loop<>> loop, std::vector<int>& phases, bool& condition)
{
    co_await vstd::post(loop);
    phases.push_back(1);
    co_await vstd::until(loop, [&condition] { return condition; });
    phases.push_back(2);
    co_await vstd::sleepFor(loop, std::chrono::milliseconds(0));
    phases.push_back(3);
    co_await vstd::nextFrame(loop);
    phases.push_back(4);
    co_await vstd::post(loop);
    phases.push_back(5);
}

vstd::task<void> waitFrame(std::shared_ptr<vstd::event_loop<>> loop)
{
    co_await vstd::nextFrame(loop);
}

vstd::task<void> waitConditionFailure(std::shared_ptr<vstd::event_loop<>> loop)
{
    co_await vstd::until(loop, []() -> bool { throw std::runtime_error("predicate failure"); });
}

vstd::task<void> conditionSelfJoin(std::shared_ptr<vstd::event_loop<>> loop, vstd::task_group& group, bool& rejected)
{
    co_await vstd::until(loop,
                         [&]
                         {
                             assert(group.ownsCurrentTask() && !group.finished());
                             rejected = !group.join();
                             group.requestStop();
                             return true;
                         });
}

vstd::task<void> frameSelfJoin(std::shared_ptr<vstd::event_loop<>> loop, vstd::task_group& group, bool& rejected)
{
    co_await vstd::nextFrame(loop);
    assert(group.ownsCurrentTask() && !group.finished());
    rejected = !group.join();
    group.requestStop();
}

void testOwnedConditionAndFrameSelfJoin()
{
    auto loop = std::make_shared<vstd::event_loop<>>();
    for (const bool frame : {false, true})
    {
        vstd::task_group group;
        bool rejected = false;
        auto handle =
            group.spawn(frame ? frameSelfJoin(loop, group, rejected) : conditionSelfJoin(loop, group, rejected),
                        vstd::loopScheduler(loop));
        for (int phase = 0; phase < 8 && !handle.finished(); ++phase)
        {
            loop->runFrame();
        }
        assert(rejected && handle.finished() && group.finished() && !group.ownsCurrentTask());
        assert(loop->getConditionalTaskCount() == 0 && loop->getFrameCallbackCount() == 0);
        expectThrow<vstd::task_cancelled>([&] { handle.get(); });
    }
}

vstd::task<void> waitLongDelay(std::shared_ptr<vstd::event_loop<>> loop)
{
    co_await vstd::sleepFor(loop, std::chrono::hours(24 * 365));
}

vstd::task<void> retainedPredicate(std::shared_ptr<vstd::event_loop<>> loop, std::promise<void>& entered,
                                   std::shared_future<void> release, int& destroyed)
{
    destruction_guard guard{destroyed};
    co_await vstd::until(loop,
                         [&]
                         {
                             entered.set_value();
                             release.wait();
                             assert(destroyed == 0);
                             return true;
                         });
}

void testPredicateCancellationRetainsFrame()
{
    auto loop = std::make_shared<vstd::event_loop<>>();
    vstd::task_group group;
    std::promise<void> entered, release;
    int destroyed = 0;
    auto handle = group.spawn(retainedPredicate(loop, entered, release.get_future().share(), destroyed));
    auto entered_future = entered.get_future();
    std::jthread cancel(
        [handle, &destroyed, &release, &entered_future]
        {
            entered_future.wait();
            handle.requestStop();
            assert(!handle.finished() && destroyed == 0);
            release.set_value();
        });
    loop->runReady();
    cancel.join();
    assert(handle.join() && destroyed == 1);
    expectThrow<vstd::task_cancelled>([&] { handle.get(); });
}

void testEventLoopPhasesAndCleanup()
{
    auto loop = std::make_shared<vstd::event_loop<>>();
    loop->setFps(1000000);
    vstd::task_group group;
    std::vector<int> phases;
    bool condition = false;
    auto flow = group.spawn(eventFlow(loop, phases, condition));
    loop->runReady();
    assert(phases == std::vector<int>{1});
    condition = true;
    loop->runReady();
    assert(phases == std::vector<int>{1});
    loop->runReady();
    assert((phases == std::vector<int>{1, 2}));
    loop->runReady();
    assert((phases == std::vector<int>{1, 2, 3}));
    assert(loop->getFrameCallbackCount() == 1);
    loop->runFrame();
    assert((phases == std::vector<int>{1, 2, 3}));
    loop->runPostedTasks();
    assert((phases == std::vector<int>{1, 2, 3, 4}));
    assert(loop->getFrameCallbackCount() == 0);
    loop->runPostedTasks();
    assert((phases == std::vector<int>{1, 2, 3, 4, 5}));
    flow.get();

    auto frame = group.spawn(waitFrame(loop), vstd::loopScheduler(loop));
    frame.requestStop();
    loop->runPostedTasks();
    assert(frame.finished() && loop->getFrameCallbackCount() == 0);

    auto delay = group.spawn(waitLongDelay(loop));
    loop->runReady();
    assert(!delay.ready());
    delay.requestStop();
    assert(delay.finished() && loop->getDelayedTaskCount() == 0);

    for (int iteration = 0; iteration < 128; ++iteration)
    {
        auto cancelled_delay = group.spawn(waitLongDelay(loop));
        assert(loop->getDelayedTaskCount() == 1);
        cancelled_delay.requestStop();
        assert(cancelled_delay.finished() && loop->getDelayedTaskCount() == 0);
    }

    {
        vstd::event_loop<> stack_loop;
        auto owner = std::make_shared<int>(42);
        std::weak_ptr<int> retained_owner = owner;
        auto timer = stack_loop.scheduleAfter(1000000000, [owner] {});
        owner.reset();
        assert(!retained_owner.expired());
        timer.reset();
        assert(retained_owner.expired() && stack_loop.getDelayedTaskCount() == 0);
    }

    auto failure = group.spawn(waitConditionFailure(loop));
    loop->runReady();
    expectThrow<std::runtime_error>([&] { failure.get(); });
    assert(failure.finished() && loop->getConditionalTaskCount() == 0);

    int owned = 0;
    loop->invoke([value = std::make_unique<int>(7), &owned] { owned += *value; });
    auto timer = loop->scheduleAfter(0, [value = std::make_unique<int>(11), &owned] { owned += *value; });
    auto conditional =
        loop->scheduleWhen([] { return true; }, [value = std::make_unique<int>(13), &owned] { owned += *value; });
    loop->runReady();
    assert(owned == 31);

    vstd::event_loop<>::connection second;
    auto first = loop->connectFrameCallback(
        [&second, &owned](int)
        {
            second.reset();
            owned += 1;
        });
    second = loop->connectFrameCallback([&owned](int) { owned += 2; });
    loop->runFrame();
    assert(owned == 32);
    loop->runFrame();
    assert(owned == 33);
}
#endif
} // namespace

int main()
{
    static_assert(!std::copy_constructible<vstd::task<int>>);
    static_assert(std::copy_constructible<vstd::task_handle<int>>);
    testLazyAndMultipleAwaiters();
    testRejectionAndDroppedTickets();
    testQueuedCancellationAndMoveOnlyWork();
    testFrameRetentionAndGroupStop();
    testOwnedWorkerSelfJoinAndScopeCleanup();
    testSynchronousExecutionAncestryKeepsGroupsIndependent();
    testOrderedWhenAllAndFailureJoin();
    testNestedCancellationAndWorkerAffinity();
    testWhenAllDrainsAfterSchedulerRejection();
    testCompletionCancellationRaces();
#ifdef VSTD_TEST_EVENT_LOOP
    testEventLoopPhasesAndCleanup();
    testPredicateCancellationRetainsFrame();
    testOwnedConditionAndFrameSelfJoin();
#endif
    std::cout << "task regression checks passed\n";
}
