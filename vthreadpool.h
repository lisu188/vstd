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

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace vstd
{
namespace detail
{
class ThreadPoolWorkerScope
{
  public:
    explicit ThreadPoolWorkerScope(const void* identity) : previousIdentity(activeIdentity)
    {
        activeIdentity = identity;
    }

    ~ThreadPoolWorkerScope()
    {
        activeIdentity = previousIdentity;
    }

    static bool belongsTo(const void* identity)
    {
        return activeIdentity == identity;
    }

  private:
    inline static thread_local const void* activeIdentity = nullptr;
    const void* previousIdentity;
};

class ThreadPoolQueue
{
  public:
    void push_task(std::function<void()> task)
    {
        std::unique_lock lock(_lock);
        if (_shutdown)
        {
            return;
        }
        _queue.push(std::move(task));
        _condition.notify_one();
    }

    bool pop_task(std::function<void()>& task, std::stop_token stop_token)
    {
        std::unique_lock lock(_lock);
        std::stop_callback on_stop(stop_token, [this]() { _condition.notify_all(); });
        _condition.wait(lock,
                        [this, &stop_token]() { return !_queue.empty() || _shutdown || stop_token.stop_requested(); });
        if (_queue.empty())
        {
            return false;
        }
        task = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    void shutdown()
    {
        std::unique_lock lock(_lock);
        _shutdown = true;
        _condition.notify_all();
    }

  private:
    std::queue<std::function<void()>> _queue;
    std::mutex _lock;
    std::condition_variable _condition;
    bool _shutdown = false;
};

class worker_thread
{
  public:
    template <typename thread_pool_type>
    void operator()(std::stop_token stop_token, std::shared_ptr<thread_pool_type> pool)
    {
        std::function<void()> task;
        while (pool->pop_task(task, stop_token))
        {
            try
            {
                task();
            }
            catch (...)
            {
            }
            task = {};
        }
    }
};
} // namespace detail

template <int _worker_count, typename worker_thread = detail::worker_thread>
class thread_pool : public std::enable_shared_from_this<thread_pool<_worker_count, worker_thread>>
{
  public:
    ~thread_pool()
    {
        stop();
    }

    template <typename F, typename... Args> void execute(F&& f, Args&&... args)
    {
        std::shared_ptr<detail::ThreadPoolQueue> queue;
        {
            std::unique_lock lock(_worker_lock);
            queue = _queue;
        }
        queue->push_task(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    std::shared_ptr<thread_pool> start()
    {
        auto owner = this->shared_from_this();
        std::unique_lock lock(_worker_lock);
        if (_started)
        {
            return owner;
        }
        if (_hasStarted)
        {
            _queue = std::make_shared<detail::ThreadPoolQueue>();
        }
        _hasStarted = true;
        _started = true;
        try
        {
            while (_workers.size() < _worker_count)
            {
                _workers.emplace_back(
                    [worker = worker_thread(), queue = _queue, identity = _identity](std::stop_token token) mutable
                    {
                        detail::ThreadPoolWorkerScope scope(identity.get());
                        worker(token, queue);
                    });
            }
        }
        catch (...)
        {
            _started = false;
            _queue->shutdown();
            auto workers = std::move(_workers);
            lock.unlock();
            stopWorkers(workers, detail::ThreadPoolWorkerScope::belongsTo(_identity.get()));
            throw;
        }
        return owner;
    }

    void stop()
    {
        const bool workerCaller = detail::ThreadPoolWorkerScope::belongsTo(_identity.get());
        std::vector<std::jthread> workers;
        {
            std::unique_lock lock(_worker_lock);
            if (!_started)
            {
                return;
            }
            _started = false;
            _queue->shutdown();
            workers = std::move(_workers);
        }
        stopWorkers(workers, workerCaller);
    }

  private:
    static void stopWorkers(std::vector<std::jthread>& workers, bool workerCaller)
    {
        for (auto& worker : workers)
        {
            worker.request_stop();
        }
        for (auto& worker : workers)
        {
            if (workerCaller)
            {
                worker.detach();
            }
            else if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    std::shared_ptr<const int> _identity = std::make_shared<const int>(0);
    std::shared_ptr<detail::ThreadPoolQueue> _queue = std::make_shared<detail::ThreadPoolQueue>();
    std::vector<std::jthread> _workers;
    std::recursive_mutex _worker_lock;
    bool _hasStarted = false;
    bool _started = false;
};
} // namespace vstd
