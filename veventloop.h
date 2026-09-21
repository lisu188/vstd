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

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#elif __has_include(<SDL.h>)
#include <SDL.h>
#endif

#ifdef SDL_h_

#include "vfuture.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace vstd
{
template <typename T = void> class event_loop : public std::enable_shared_from_this<event_loop<T>>
{
    using clock = std::chrono::steady_clock;

    struct delayed_task
    {
        clock::time_point due;
        std::size_t id;
        std::function<void()> function;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct delay_compare
    {
        bool operator()(const delayed_task& a, const delayed_task& b) const
        {
            return a.due > b.due;
        }
    };

    struct conditional_task
    {
        std::size_t id;
        std::function<bool()> predicate;
        std::function<void()> function;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

  public:
    class connection
    {
      public:
        connection() = default;
        explicit connection(std::function<void()> disconnect) : disconnectFunction(std::move(disconnect)) {}
        connection(const connection&) = delete;
        connection& operator=(const connection&) = delete;
        connection(connection&& other) noexcept : disconnectFunction(std::move(other.disconnectFunction)) {}
        connection& operator=(connection&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                disconnectFunction = std::move(other.disconnectFunction);
            }
            return *this;
        }
        ~connection()
        {
            reset();
        }

        void reset()
        {
            if (disconnectFunction)
            {
                auto disconnect = std::move(disconnectFunction);
                disconnect();
            }
        }

        void release()
        {
            disconnectFunction = {};
        }

        explicit operator bool() const
        {
            return static_cast<bool>(disconnectFunction);
        }

      private:
        std::function<void()> disconnectFunction;
    };

    static std::shared_ptr<event_loop> instance()
    {
        static std::shared_ptr<event_loop> loop = std::make_shared<event_loop>();
        return loop;
    }

    bool invoke(const std::function<void()>& function)
    {
        try
        {
            {
                std::lock_guard lock(taskMutex);
                taskQueue.push(function);
            }
            wake();
            return true;
        }
        catch (...)
        {
            reportException(std::current_exception());
            return false;
        }
    }

    void invoke_when(const std::function<bool()>& predicate, const std::function<void()>& function)
    {
        scheduleWhen(predicate, function).release();
    }

    connection scheduleWhen(const std::function<bool()>& predicate, const std::function<void()>& function)
    {
        const auto id = nextId.fetch_add(1, std::memory_order_relaxed);
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        {
            std::lock_guard lock(conditionalMutex);
            conditionalQueue.push_back({id, predicate, function, cancelled});
        }
        wake();
        return connection([cancelled]() { cancelled->store(true, std::memory_order_relaxed); });
    }

    void await(const std::function<void()>& function)
    {
        if (isMainThread())
        {
            function();
            return;
        }

        std::mutex waitMutex;
        std::condition_variable waitCondition;
        std::unique_lock waitLock(waitMutex);
        bool completed = false;
        std::exception_ptr error;
        if (!invoke(
                [&]()
                {
                    try
                    {
                        function();
                    }
                    catch (...)
                    {
                        error = std::current_exception();
                    }
                    {
                        std::lock_guard completionLock(waitMutex);
                        completed = true;
                    }
                    waitCondition.notify_all();
                }))
        {
            throw std::runtime_error("failed to schedule event-loop task");
        }
        waitCondition.wait(waitLock, [&]() { return completed; });
        if (error)
        {
            std::rethrow_exception(error);
        }
    }

    void delay(int milliseconds, const std::function<void()>& function)
    {
        scheduleAfter(milliseconds, function).release();
    }

    connection scheduleAfter(int milliseconds, const std::function<void()>& function)
    {
        const auto id = nextId.fetch_add(1, std::memory_order_relaxed);
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        const auto due = clock::now() + std::chrono::milliseconds(std::max(milliseconds, 0));
        {
            std::lock_guard lock(delayMutex);
            delayQueue.push({due, id, function, cancelled});
        }
        wake();
        return connection([cancelled]() { cancelled->store(true, std::memory_order_relaxed); });
    }

    connection connectFrameCallback(const std::function<void(int)>& function)
    {
        const auto id = nextId.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(callbackMutex);
            frameCallbackList.emplace_back(id, function);
        }
        std::weak_ptr<event_loop> weakLoop = this->weak_from_this();
        return connection(
            [weakLoop, id]()
            {
                if (auto loop = weakLoop.lock())
                {
                    loop->removeFrameCallback(id);
                }
            });
    }

    void registerFrameCallback(const std::function<void(int)>& function)
    {
        connectFrameCallback(function).release();
    }

    connection connectEventCallback(const std::function<bool(SDL_Event*)>& function)
    {
        const auto id = nextId.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(callbackMutex);
            eventCallbackList.emplace_back(id, function);
        }
        std::weak_ptr<event_loop> weakLoop = this->weak_from_this();
        return connection(
            [weakLoop, id]()
            {
                if (auto loop = weakLoop.lock())
                {
                    loop->removeEventCallback(id);
                }
            });
    }

    void registerEventCallback(const std::function<bool(SDL_Event*)>& function)
    {
        connectEventCallback(function).release();
    }

    std::size_t runReady()
    {
        std::size_t processed = 0;
        processed += drainPostedTasks();
        processed += pollEvents();
        processed += processConditions();
        processed += processDelays();
        return processed;
    }

    std::size_t runUntilIdle(std::size_t maxIterations = 1000)
    {
        std::size_t total = 0;
        for (std::size_t iteration = 0; iteration < maxIterations; ++iteration)
        {
            const auto processed = runReady();
            total += processed;
            if (processed == 0)
            {
                break;
            }
        }
        return total;
    }

    bool runFrame()
    {
        runReady();
        if (quitRequested())
        {
            return false;
        }

        const int frameTime = static_cast<int>(SDL_GetTicks());
        std::vector<std::function<void(int)>> callbacks;
        {
            std::lock_guard lock(callbackMutex);
            callbacks.reserve(frameCallbackList.size());
            for (const auto& [id, callback] : frameCallbackList)
            {
                callbacks.push_back(callback);
            }
        }
        for (auto& callback : callbacks)
        {
            safeInvoke([&]() { callback(frameTime); });
        }

        const auto now = clock::now();
        const auto desiredFrameTime = std::chrono::milliseconds(1000 / getFps());
        const auto actualFrameTime = now - lastFrameTime;
        if (actualFrameTime < desiredFrameTime)
        {
            std::this_thread::sleep_for(desiredFrameTime - actualFrameTime);
        }
        lastFrameTime = clock::now();
        return !quitRequested();
    }

    bool run()
    {
        return runFrame();
    }

    bool hasReadyWork() const
    {
        {
            std::lock_guard lock(taskMutex);
            if (!taskQueue.empty())
            {
                return true;
            }
        }
        {
            std::lock_guard lock(delayMutex);
            if (!delayQueue.empty() && delayQueue.top().due <= clock::now())
            {
                return true;
            }
        }
        return false;
    }

    std::size_t getPendingTaskCount() const
    {
        std::lock_guard lock(taskMutex);
        return taskQueue.size();
    }

    std::size_t getConditionalTaskCount() const
    {
        std::lock_guard lock(conditionalMutex);
        return conditionalQueue.size();
    }

    std::size_t getDelayedTaskCount() const
    {
        std::lock_guard lock(delayMutex);
        return delayQueue.size();
    }

    std::size_t getFrameCallbackCount() const
    {
        std::lock_guard lock(callbackMutex);
        return frameCallbackList.size();
    }

    std::size_t getEventCallbackCount() const
    {
        std::lock_guard lock(callbackMutex);
        return eventCallbackList.size();
    }

    bool quitRequested() const
    {
        return quit.load(std::memory_order_relaxed);
    }

    void resetQuit()
    {
        quit.store(false, std::memory_order_relaxed);
    }

    void bindToCurrentThread()
    {
        mainThreadId = std::this_thread::get_id();
    }

    bool isMainThread() const
    {
        return std::this_thread::get_id() == mainThreadId;
    }

    void setExceptionHandler(std::function<void(std::exception_ptr)> handler)
    {
        std::lock_guard lock(exceptionMutex);
        exceptionHandler = std::move(handler);
    }

    event_loop()
    {
        mainThreadId = std::this_thread::get_id();
        lastFrameTime = clock::now();
        ownsEventsSubsystem = SDL_WasInit(SDL_INIT_EVENTS) == 0;
        if (ownsEventsSubsystem && SDL_InitSubSystem(SDL_INIT_EVENTS) != 0)
        {
            throw std::runtime_error(SDL_GetError());
        }
        callFunctionEvent = SDL_RegisterEvents(1);
    }

    ~event_loop()
    {
        if (ownsEventsSubsystem)
        {
            SDL_QuitSubSystem(SDL_INIT_EVENTS);
        }
    }

    int getFps() const
    {
        return fps.load(std::memory_order_relaxed);
    }

    void setFps(int value)
    {
        if (value <= 0)
        {
            throw std::invalid_argument("event-loop fps must be positive");
        }
        fps.store(value, std::memory_order_relaxed);
    }

  private:
    void wake()
    {
        if (callFunctionEvent == static_cast<Uint32>(-1) || wakePending.exchange(true, std::memory_order_relaxed))
        {
            return;
        }
        SDL_Event event;
        SDL_zero(event);
        event.type = callFunctionEvent;
        if (SDL_PushEvent(&event) <= 0)
        {
            wakePending.store(false, std::memory_order_relaxed);
        }
    }

    std::size_t drainPostedTasks()
    {
        std::queue<std::function<void()>> ready;
        {
            std::lock_guard lock(taskMutex);
            ready.swap(taskQueue);
        }
        wakePending.store(false, std::memory_order_relaxed);

        std::size_t processed = 0;
        while (!ready.empty())
        {
            auto function = std::move(ready.front());
            ready.pop();
            safeInvoke(function);
            ++processed;
        }
        return processed;
    }

    std::size_t pollEvents()
    {
        std::size_t processed = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ++processed;
            if (event.type == SDL_QUIT)
            {
                quit.store(true, std::memory_order_relaxed);
                continue;
            }
            if (event.type == callFunctionEvent)
            {
                continue;
            }

            std::vector<std::function<bool(SDL_Event*)>> callbacks;
            {
                std::lock_guard lock(callbackMutex);
                callbacks.reserve(eventCallbackList.size());
                for (const auto& [id, callback] : eventCallbackList)
                {
                    callbacks.push_back(callback);
                }
            }
            for (auto& callback : callbacks)
            {
                bool handled = false;
                safeInvoke([&]() { handled = callback(&event); });
                if (handled)
                {
                    break;
                }
            }
        }
        return processed;
    }

    std::size_t processConditions()
    {
        std::vector<conditional_task> conditions;
        {
            std::lock_guard lock(conditionalMutex);
            conditions.assign(conditionalQueue.begin(), conditionalQueue.end());
        }

        std::size_t processed = 0;
        for (auto& conditionTask : conditions)
        {
            if (conditionTask.cancelled->load(std::memory_order_relaxed))
            {
                removeCondition(conditionTask.id);
                continue;
            }

            bool ready = false;
            safeInvoke([&]() { ready = conditionTask.predicate(); });
            if (!ready)
            {
                continue;
            }

            if (removeCondition(conditionTask.id))
            {
                safeInvoke(conditionTask.function);
                ++processed;
            }
        }
        return processed;
    }

    std::size_t processDelays()
    {
        std::vector<delayed_task> ready;
        const auto now = clock::now();
        {
            std::lock_guard lock(delayMutex);
            while (!delayQueue.empty() && delayQueue.top().due <= now)
            {
                ready.push_back(delayQueue.top());
                delayQueue.pop();
            }
        }

        std::size_t processed = 0;
        for (auto& delayed : ready)
        {
            if (!delayed.cancelled->load(std::memory_order_relaxed))
            {
                safeInvoke(delayed.function);
                ++processed;
            }
        }
        return processed;
    }

    template <typename F> void safeInvoke(F&& function)
    {
        try
        {
            function();
        }
        catch (...)
        {
            reportException(std::current_exception());
        }
    }

    void reportException(std::exception_ptr error)
    {
        std::function<void(std::exception_ptr)> handler;
        {
            std::lock_guard lock(exceptionMutex);
            handler = exceptionHandler;
        }
        if (handler)
        {
            try
            {
                handler(error);
            }
            catch (...)
            {
            }
        }
    }

    bool removeCondition(std::size_t id)
    {
        std::lock_guard lock(conditionalMutex);
        for (auto iterator = conditionalQueue.begin(); iterator != conditionalQueue.end(); ++iterator)
        {
            if (iterator->id == id)
            {
                conditionalQueue.erase(iterator);
                return true;
            }
        }
        return false;
    }

    void removeFrameCallback(std::size_t id)
    {
        std::lock_guard lock(callbackMutex);
        frameCallbackList.remove_if([id](const auto& item) { return item.first == id; });
    }

    void removeEventCallback(std::size_t id)
    {
        std::lock_guard lock(callbackMutex);
        eventCallbackList.remove_if([id](const auto& item) { return item.first == id; });
    }

    clock::time_point lastFrameTime;
    Uint32 callFunctionEvent = static_cast<Uint32>(-1);
    std::thread::id mainThreadId;
    bool ownsEventsSubsystem = false;
    std::atomic_bool wakePending{false};
    std::atomic_bool quit{false};
    std::atomic_int fps{100};
    std::atomic_size_t nextId{1};

    mutable std::mutex taskMutex;
    std::queue<std::function<void()>> taskQueue;

    mutable std::mutex delayMutex;
    std::priority_queue<delayed_task, std::vector<delayed_task>, delay_compare> delayQueue;

    mutable std::mutex conditionalMutex;
    std::list<conditional_task> conditionalQueue;

    mutable std::mutex callbackMutex;
    std::list<std::pair<std::size_t, std::function<void(int)>>> frameCallbackList;
    std::list<std::pair<std::size_t, std::function<bool(SDL_Event*)>>> eventCallbackList;

    mutable std::mutex exceptionMutex;
    std::function<void(std::exception_ptr)> exceptionHandler;
};
} // namespace vstd

#endif
