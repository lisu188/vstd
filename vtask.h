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
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>

namespace vstd
{
class task_cancelled : public std::runtime_error
{
  public:
    task_cancelled() : std::runtime_error("task cancelled") {}
};

class scheduler_rejected : public std::runtime_error
{
  public:
    scheduler_rejected() : std::runtime_error("task scheduler rejected a continuation") {}
};

// An accepted dispatch owns its callback until it runs or is destroyed. Dropping it fails the suspended task.
class scheduler
{
  public:
    using function = std::move_only_function<void()>;
    using dispatcher = std::function<bool(function)>;

    scheduler()
        : dispatchFunction(
              [](function callback)
              {
                  callback();
                  return true;
              })
    {
    }
    explicit scheduler(dispatcher dispatch) : dispatchFunction(std::move(dispatch)) {}

    bool dispatch(function callback) const noexcept
    {
        try
        {
            return dispatchFunction && dispatchFunction(std::move(callback));
        }
        catch (...)
        {
            return false;
        }
    }

  private:
    dispatcher dispatchFunction;
};

namespace task_detail
{
class state_base;
inline thread_local state_base* currentTask = nullptr;

class state_base : public std::enable_shared_from_this<state_base>
{
  public:
    virtual ~state_base()
    {
        if (frame)
        {
            frame.destroy();
        }
    }

    void resume()
    {
        auto keep_alive = shared_from_this();
        {
            std::lock_guard lock(resumeMutex);
            if (executionFinished)
            {
                return;
            }
            if (running)
            {
                resumePending = true;
                return;
            }
            running = true;
        }
        for (;;)
        {
            auto previous = std::exchange(currentTask, this);
            const bool skip_body = !std::exchange(started, true) && stopping();
            if (!skip_body)
            {
                frame.resume();
            }
            std::unique_lock lock(resumeMutex);
            if (skip_body || frame.done())
            {
                auto completed_frame = std::exchange(frame, {});
                lock.unlock();
                completed_frame.destroy();
                currentTask = previous;
                lock.lock();
                running = false;
                executionFinished = true;
                lock.unlock();
                completeExecution();
                return;
            }
            currentTask = previous;
            if (!std::exchange(resumePending, false))
            {
                running = false;
                return;
            }
        }
    }

    void requestStop()
    {
        std::vector<scheduler::function> callbacks;
        {
            std::lock_guard lock(resultMutex);
            if (!settled)
            {
                resultError = std::make_exception_ptr(task_cancelled());
                settled = true;
                callbacks.swap(resultCallbacks);
            }
            cancellationRequested.store(true, std::memory_order_release);
        }
        stopSource.request_stop();
        notify(callbacks);
    }

    std::stop_token stopToken() const
    {
        return stopSource.get_token();
    }

    bool stopping() const
    {
        return cancellationRequested.load(std::memory_order_acquire);
    }

    bool ready() const
    {
        std::lock_guard lock(resultMutex);
        return settled;
    }

    bool finished() const
    {
        std::lock_guard lock(resultMutex);
        return completed;
    }

    bool join() const
    {
        if (currentTask == this)
        {
            return false;
        }
        std::unique_lock lock(resultMutex);
        resultCondition.wait(lock, [this] { return completed; });
        return true;
    }

    std::exception_ptr error() const
    {
        std::lock_guard lock(resultMutex);
        return resultError;
    }

    scheduler getScheduler() const
    {
        std::lock_guard lock(schedulerMutex);
        return continuationScheduler;
    }

    void setScheduler(scheduler next)
    {
        std::lock_guard lock(schedulerMutex);
        continuationScheduler = std::move(next);
    }

    void onReady(scheduler::function callback, bool execution = false)
    {
        {
            std::lock_guard lock(resultMutex);
            if (!(execution ? completed : settled))
            {
                (execution ? completionCallbacks : resultCallbacks).push_back(std::move(callback));
                return;
            }
        }
        callback();
    }

    void setError(std::exception_ptr error)
    {
        std::vector<scheduler::function> callbacks;
        {
            std::lock_guard lock(resultMutex);
            if (settled)
            {
                return;
            }
            resultError = error;
            settled = true;
            callbacks.swap(resultCallbacks);
        }
        notify(callbacks);
    }

    std::coroutine_handle<> frame;

  protected:
    void waitResult() const
    {
        if (currentTask == this)
        {
            throw std::logic_error("task cannot synchronously wait for its own result");
        }
        std::unique_lock lock(resultMutex);
        resultCondition.wait(lock, [this] { return settled; });
        if (resultError)
        {
            std::rethrow_exception(resultError);
        }
    }

    void notify(std::vector<scheduler::function>& callbacks)
    {
        resultCondition.notify_all();
        for (auto& callback : callbacks)
        {
            callback();
        }
    }

    mutable std::mutex resultMutex;
    mutable std::condition_variable resultCondition;
    bool settled = false;
    std::vector<scheduler::function> resultCallbacks;

  private:
    void completeExecution()
    {
        std::vector<scheduler::function> callbacks;
        {
            std::lock_guard lock(resultMutex);
            completed = true;
            callbacks.swap(completionCallbacks);
        }
        notify(callbacks);
    }

    std::exception_ptr resultError;
    std::vector<scheduler::function> completionCallbacks;
    bool completed = false;
    std::stop_source stopSource;
    std::atomic_bool cancellationRequested{false};
    mutable std::mutex schedulerMutex;
    scheduler continuationScheduler;
    std::mutex resumeMutex;
    bool running = false;
    bool resumePending = false;
    bool executionFinished = false;
    bool started = false;
};

template <typename T> class state final : public state_base
{
  public:
    static_assert(std::copy_constructible<T>, "task values must be copy constructible");

    void setValue(T result)
    {
        std::vector<scheduler::function> callbacks;
        {
            std::lock_guard lock(resultMutex);
            if (settled)
            {
                return;
            }
            value.emplace(std::move(result));
            settled = true;
            callbacks.swap(resultCallbacks);
        }
        notify(callbacks);
    }

    T get() const
    {
        waitResult();
        std::lock_guard lock(resultMutex);
        return *value;
    }

  private:
    std::optional<T> value;
};

template <> class state<void> final : public state_base
{
  public:
    void setValue()
    {
        std::vector<scheduler::function> callbacks;
        {
            std::lock_guard lock(resultMutex);
            if (settled)
            {
                return;
            }
            settled = true;
            callbacks.swap(resultCallbacks);
        }
        notify(callbacks);
    }
    void get() const
    {
        waitResult();
    }
};

class operation;

class resume_ticket
{
  public:
    resume_ticket(std::shared_ptr<state_base> state, std::shared_ptr<operation> operation)
        : suspended(std::move(state)), pending(std::move(operation))
    {
    }
    resume_ticket(const resume_ticket&) = delete;
    resume_ticket(resume_ticket&& other) noexcept
        : suspended(std::move(other.suspended)), pending(std::move(other.pending))
    {
    }
    ~resume_ticket();
    void operator()();

  private:
    std::shared_ptr<state_base> suspended;
    std::shared_ptr<operation> pending;
};

class operation : public std::enable_shared_from_this<operation>
{
  public:
    operation(std::shared_ptr<state_base> state, scheduler target)
        : token(state->stopToken()), owner(state), suspended(std::move(state)), target(std::move(target))
    {
    }

    void complete(std::exception_ptr error = {})
    {
        std::shared_ptr<state_base> state;
        {
            std::lock_guard lock(mutex);
            if (claimed)
            {
                return;
            }
            claimed = true;
            failure = error;
            state = std::move(suspended);
        }
        target.dispatch(resume_ticket(std::move(state), shared_from_this()));
    }

    void cancel()
    {
        std::shared_ptr<state_base> state;
        {
            std::lock_guard lock(mutex);
            if (executing || claimed)
            {
                return;
            }
            claimed = true;
            failure = std::make_exception_ptr(task_cancelled());
            state = std::move(suspended);
        }
        target.dispatch(resume_ticket(std::move(state), shared_from_this()));
    }

    bool beginWork()
    {
        std::lock_guard lock(mutex);
        if (claimed || stopping())
        {
            return false;
        }
        executing = true;
        return true;
    }

    void reject()
    {
        std::lock_guard lock(mutex);
        failure = std::make_exception_ptr(scheduler_rejected());
    }

    void check() const
    {
        std::lock_guard lock(mutex);
        if (failure)
        {
            std::rethrow_exception(failure);
        }
        if (stopping())
        {
            throw task_cancelled();
        }
    }

    void checkFailure() const
    {
        std::lock_guard lock(mutex);
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }

    std::stop_token token;

  private:
    bool stopping() const
    {
        auto state = owner.lock();
        return token.stop_requested() || (state && state->stopping());
    }

    mutable std::mutex mutex;
    std::weak_ptr<state_base> owner;
    std::shared_ptr<state_base> suspended;
    scheduler target;
    std::exception_ptr failure;
    bool claimed = false;
    bool executing = false;
};

class work_ticket
{
  public:
    work_ticket(std::shared_ptr<operation> pending, scheduler::function function)
        : pending(std::move(pending)), function(std::move(function))
    {
    }
    work_ticket(const work_ticket&) = delete;
    work_ticket(work_ticket&&) noexcept = default;
    ~work_ticket()
    {
        if (function)
        {
            pending->complete(std::make_exception_ptr(scheduler_rejected()));
        }
    }
    void operator()()
    {
        auto callback = std::move(function);
        callback();
    }

  private:
    std::shared_ptr<operation> pending;
    scheduler::function function;
};

inline resume_ticket::~resume_ticket()
{
    if (suspended)
    {
        pending->reject();
        suspended->resume();
    }
}

inline void resume_ticket::operator()()
{
    auto state = std::move(suspended);
    state->resume();
}

using stop_callback = std::stop_callback<std::function<void()>>;

class suspension
{
  public:
    suspension() = default;
    suspension(suspension&&) = default;
    suspension(const suspension&) = delete;
    bool await_ready() const noexcept
    {
        return false;
    }
    void await_resume() const
    {
        pending->check();
    }

  protected:
    template <typename Promise> void prepare(std::coroutine_handle<Promise> frame, scheduler target)
    {
        pending = std::make_shared<operation>(frame.promise().getTaskState(), std::move(target));
        std::weak_ptr<operation> weak = pending;
        cancellation = std::make_unique<stop_callback>(pending->token,
                                                       [weak]
                                                       {
                                                           if (auto operation = weak.lock())
                                                           {
                                                               operation->cancel();
                                                           }
                                                       });
    }

    std::shared_ptr<operation> pending;
    std::unique_ptr<stop_callback> cancellation;
};

template <typename T> struct promise_result
{
    std::weak_ptr<state<T>> taskState;
    void return_value(T value)
    {
        taskState.lock()->setValue(std::move(value));
    }
};

template <> struct promise_result<void>
{
    std::weak_ptr<state<void>> taskState;
    void return_void()
    {
        taskState.lock()->setValue();
    }
};
} // namespace task_detail

template <typename T = void> class task;
class task_group;

template <typename T = void> class task_handle
{
  public:
    task_handle() = default;
    T get() const
    {
        return checked()->get();
    }
    bool ready() const
    {
        return checked()->ready();
    }
    bool finished() const
    {
        return checked()->finished();
    }
    bool join() const
    {
        return checked()->join();
    }
    void requestStop() const
    {
        checked()->requestStop();
    }
    std::stop_token stopToken() const
    {
        return checked()->stopToken();
    }
    explicit operator bool() const noexcept
    {
        return static_cast<bool>(state);
    }

    class awaiter : public task_detail::suspension
    {
      public:
        explicit awaiter(std::shared_ptr<task_detail::state<T>> state) : target(std::move(state)) {}
        bool await_ready() const
        {
            return target->ready();
        }
        template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
        {
            auto state = frame.promise().getTaskState();
            this->prepare(frame, state->getScheduler());
            std::weak_ptr<task_detail::operation> weak = this->pending;
            target->onReady(
                [weak]
                {
                    if (auto operation = weak.lock())
                    {
                        operation->complete();
                    }
                });
        }
        T await_resume() const
        {
            if (this->pending)
            {
                this->pending->check();
            }
            return target->get();
        }

      private:
        std::shared_ptr<task_detail::state<T>> target;
    };

    awaiter operator co_await() const
    {
        return awaiter(checked());
    }

    class completion_awaiter : public task_detail::suspension
    {
      public:
        explicit completion_awaiter(std::shared_ptr<task_detail::state<T>> target) : target(std::move(target)) {}
        bool await_ready() const
        {
            return target->finished();
        }
        template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
        {
            // Execution joins deliberately remain non-cancellable: settlement is not proof of teardown.
            auto state = frame.promise().getTaskState();
            this->pending = std::make_shared<task_detail::operation>(state, state->getScheduler());
            std::weak_ptr<task_detail::operation> weak = this->pending;
            target->onReady(
                [weak]
                {
                    if (auto operation = weak.lock())
                    {
                        operation->complete();
                    }
                },
                true);
        }
        void await_resume() const
        {
            if (this->pending)
            {
                this->pending->checkFailure();
            }
        }

      private:
        std::shared_ptr<task_detail::state<T>> target;
    };

    completion_awaiter waitFinished() const
    {
        return completion_awaiter(checked());
    }

  private:
    friend class task_group;
    friend class task<T>;
    template <typename U>
    friend task<std::conditional_t<std::is_void_v<U>, void, std::vector<U>>> whenAll(std::vector<task_handle<U>>);
    explicit task_handle(std::shared_ptr<task_detail::state<T>> state) : state(std::move(state)) {}
    std::shared_ptr<task_detail::state<T>> checked() const
    {
        if (!state)
        {
            throw std::logic_error("empty task handle");
        }
        return state;
    }
    std::shared_ptr<task_detail::state<T>> state;
};

template <typename T> class task
{
  public:
    struct promise_type : task_detail::promise_result<T>
    {
        task get_return_object()
        {
            auto state = std::make_shared<task_detail::state<T>>();
            this->taskState = state;
            state->frame = std::coroutine_handle<promise_type>::from_promise(*this);
            return task(std::move(state));
        }
        std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }
        std::suspend_always final_suspend() const noexcept
        {
            return {};
        }
        void unhandled_exception()
        {
            this->taskState.lock()->setError(std::current_exception());
        }
        std::shared_ptr<task_detail::state<T>> getTaskState() const
        {
            return this->taskState.lock();
        }
    };

    task(task&&) noexcept = default;
    task& operator=(task&&) noexcept = default;
    task(const task&) = delete;
    task& operator=(const task&) = delete;

    class awaiter : public task_detail::suspension
    {
      public:
        explicit awaiter(std::shared_ptr<task_detail::state<T>> child) : child(std::move(child)) {}
        bool await_ready() const noexcept
        {
            return false;
        }
        template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
        {
            auto parent = frame.promise().getTaskState();
            child->setScheduler(parent->getScheduler());
            this->pending = std::make_shared<task_detail::operation>(parent, parent->getScheduler());
            std::weak_ptr<task_detail::operation> weak_operation = this->pending;
            child->onReady(
                [weak_operation]
                {
                    if (auto operation = weak_operation.lock())
                    {
                        operation->complete();
                    }
                },
                true);
            std::weak_ptr<task_detail::state<T>> weak = child;
            cancellation = std::make_unique<task_detail::stop_callback>(parent->stopToken(),
                                                                        [weak]
                                                                        {
                                                                            if (auto child = weak.lock())
                                                                            {
                                                                                child->requestStop();
                                                                            }
                                                                        });
            child->resume();
        }
        T await_resume() const
        {
            this->pending->check();
            return child->get();
        }

      private:
        std::shared_ptr<task_detail::state<T>> child;
        std::unique_ptr<task_detail::stop_callback> cancellation;
    };

    awaiter operator co_await() &&
    {
        if (!state)
        {
            throw std::logic_error("cannot await an empty task");
        }
        return awaiter(std::move(state));
    }

  private:
    friend class task_group;
    explicit task(std::shared_ptr<task_detail::state<T>> state) : state(std::move(state)) {}
    std::shared_ptr<task_detail::state<T>> state;
};

class task_group
{
  public:
    task_group() = default;
    task_group(const task_group&) = delete;
    task_group& operator=(const task_group&) = delete;
    ~task_group()
    {
        requestStop();
    }

    template <typename T> task_handle<T> spawn(task<T> task, scheduler initial = {})
    {
        auto state = std::move(task.state);
        if (!state)
        {
            throw std::logic_error("cannot spawn an empty task");
        }
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                throw task_cancelled();
            }
            std::erase_if(tasks, [](const auto& task) { return task->finished(); });
            tasks.push_back(state);
        }
        state->setScheduler(std::move(initial));
        state->resume();
        return task_handle<T>(std::move(state));
    }

    void requestStop()
    {
        std::vector<std::shared_ptr<task_detail::state_base>> snapshot;
        {
            std::lock_guard lock(mutex);
            stopping = true;
            snapshot = tasks;
        }
        for (auto& task : snapshot)
        {
            if (!task->finished())
            {
                task->requestStop();
            }
        }
    }

    bool join() const
    {
        std::vector<std::shared_ptr<task_detail::state_base>> snapshot;
        {
            std::lock_guard lock(mutex);
            snapshot = tasks;
        }
        for (const auto& task : snapshot)
        {
            if (task.get() == task_detail::currentTask)
            {
                return false;
            }
        }
        for (const auto& task : snapshot)
        {
            task->join();
        }
        return true;
    }

    bool finished() const
    {
        std::lock_guard lock(mutex);
        return std::ranges::all_of(tasks, [](const auto& task) { return task->finished(); });
    }

    bool ownsCurrentTask() const
    {
        std::lock_guard lock(mutex);
        return std::ranges::any_of(tasks, [](const auto& task) { return task.get() == task_detail::currentTask; });
    }

  private:
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<task_detail::state_base>> tasks;
    bool stopping = false;
};

class stop_token_awaiter
{
  public:
    bool await_ready() const noexcept
    {
        return false;
    }
    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> frame) noexcept
    {
        token = frame.promise().getTaskState()->stopToken();
        return false;
    }
    std::stop_token await_resume() const noexcept
    {
        return token;
    }

  private:
    std::stop_token token;
};

inline stop_token_awaiter thisTaskStopToken()
{
    return {};
}

class post_awaiter : public task_detail::suspension
{
  public:
    explicit post_awaiter(scheduler target) : target(std::move(target)) {}
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
    {
        frame.promise().getTaskState()->setScheduler(target);
        this->prepare(frame, target);
        this->pending->complete();
    }

  private:
    scheduler target;
};

inline post_awaiter post(scheduler target)
{
    return post_awaiter(std::move(target));
}

template <typename Loop> scheduler loopScheduler(std::shared_ptr<Loop> loop)
{
    return scheduler([loop = std::move(loop)](scheduler::function function)
                     { return loop->invoke(std::move(function)); });
}

template <typename Pool> scheduler workerScheduler(std::shared_ptr<Pool> pool)
{
    return scheduler([pool = std::move(pool)](scheduler::function function)
                     { return pool->submit(std::move(function)); });
}

template <typename Loop> post_awaiter post(std::shared_ptr<Loop> loop)
{
    return post(loopScheduler(std::move(loop)));
}
template <typename Pool> post_awaiter work(std::shared_ptr<Pool> pool)
{
    return post(workerScheduler(std::move(pool)));
}

template <typename Pool, typename Function> class work_awaiter : public task_detail::suspension
{
    using result_type = std::invoke_result_t<Function&>;
    static_assert(!std::is_reference_v<result_type>, "worker results must own their value");
    using storage_type = std::conditional_t<std::is_void_v<result_type>, bool, result_type>;

  public:
    work_awaiter(std::shared_ptr<Pool> pool, Function function) : pool(std::move(pool)), function(std::move(function))
    {
    }
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
    {
        this->prepare(frame, frame.promise().getTaskState()->getScheduler());
        auto pending = this->pending;
        auto result = this->result;
        if (!pool->submit(task_detail::work_ticket(pending,
                                                   [pending, result, function = std::move(function)]() mutable
                                                   {
                                                       if (!pending->beginWork())
                                                       {
                                                           return;
                                                       }
                                                       try
                                                       {
                                                           if constexpr (std::is_void_v<result_type>)
                                                           {
                                                               function();
                                                               result->emplace(true);
                                                           }
                                                           else
                                                           {
                                                               result->emplace(function());
                                                           }
                                                           pending->complete();
                                                       }
                                                       catch (...)
                                                       {
                                                           pending->complete(std::current_exception());
                                                       }
                                                   })))
        {
            pending->complete(std::make_exception_ptr(scheduler_rejected()));
        }
    }
    result_type await_resume()
    {
        this->pending->check();
        if constexpr (!std::is_void_v<result_type>)
        {
            return std::move(**result);
        }
    }

  private:
    std::shared_ptr<Pool> pool;
    Function function;
    std::shared_ptr<std::optional<storage_type>> result = std::make_shared<std::optional<storage_type>>();
};

template <typename Pool, typename Function> auto work(std::shared_ptr<Pool> pool, Function&& function)
{
    return work_awaiter<Pool, std::decay_t<Function>>(std::move(pool), std::forward<Function>(function));
}

template <typename Loop> class delay_awaiter : public task_detail::suspension
{
  public:
    delay_awaiter(std::shared_ptr<Loop> loop, std::chrono::milliseconds delay) : loop(std::move(loop)), delay(delay) {}
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
    {
        this->prepare(frame, frame.promise().getTaskState()->getScheduler());
        std::weak_ptr<task_detail::operation> weak = this->pending;
        const auto milliseconds =
            std::clamp<std::chrono::milliseconds::rep>(delay.count(), 0, std::numeric_limits<int>::max());
        connection = loop->scheduleAfter(static_cast<int>(milliseconds),
                                         [weak]
                                         {
                                             if (auto operation = weak.lock())
                                             {
                                                 operation->complete();
                                             }
                                         });
    }

  private:
    std::shared_ptr<Loop> loop;
    std::chrono::milliseconds delay;
    typename Loop::connection connection;
};

template <typename Loop, typename Rep, typename Period>
auto sleepFor(std::shared_ptr<Loop> loop, std::chrono::duration<Rep, Period> delay)
{
    return delay_awaiter<Loop>(std::move(loop), std::chrono::duration_cast<std::chrono::milliseconds>(delay));
}

template <typename Loop, typename Predicate> class condition_awaiter : public task_detail::suspension
{
  public:
    condition_awaiter(std::shared_ptr<Loop> loop, Predicate predicate)
        : loop(std::move(loop)), predicate(std::make_shared<Predicate>(std::move(predicate)))
    {
    }
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
    {
        this->prepare(frame, frame.promise().getTaskState()->getScheduler());
        std::weak_ptr<task_detail::operation> weak = this->pending;
        connection = loop->scheduleWhen(
            [weak, predicate = predicate]
            {
                if (auto operation = weak.lock())
                {
                    try
                    {
                        return (*predicate)();
                    }
                    catch (...)
                    {
                        operation->complete(std::current_exception());
                    }
                }
                return true;
            },
            [weak]
            {
                if (auto operation = weak.lock())
                {
                    operation->complete();
                }
            });
    }

  private:
    std::shared_ptr<Loop> loop;
    std::shared_ptr<Predicate> predicate;
    typename Loop::connection connection;
};

template <typename Loop, typename Predicate> auto until(std::shared_ptr<Loop> loop, Predicate&& predicate)
{
    return condition_awaiter<Loop, std::decay_t<Predicate>>(std::move(loop), std::forward<Predicate>(predicate));
}

template <typename Loop> class frame_awaiter : public task_detail::suspension
{
  public:
    explicit frame_awaiter(std::shared_ptr<Loop> loop) : loop(std::move(loop)) {}
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> frame)
    {
        this->prepare(frame, frame.promise().getTaskState()->getScheduler());
        std::weak_ptr<task_detail::operation> weak = this->pending;
        connection = loop->connectFrameCallback(
            [weak](int)
            {
                if (auto operation = weak.lock())
                {
                    operation->complete();
                }
            });
    }

  private:
    std::shared_ptr<Loop> loop;
    typename Loop::connection connection;
};

template <typename Loop> auto nextFrame(std::shared_ptr<Loop> loop)
{
    return frame_awaiter<Loop>(std::move(loop));
}

template <typename T>
task<std::conditional_t<std::is_void_v<T>, void, std::vector<T>>> whenAll(std::vector<task_handle<T>> handles)
{
    struct join_state
    {
        std::mutex mutex;
        std::exception_ptr error;
        std::vector<std::weak_ptr<task_detail::state<T>>> children;

        void fail(std::exception_ptr failure)
        {
            if (!failure)
            {
                return;
            }
            {
                std::lock_guard lock(mutex);
                if (error)
                {
                    return;
                }
                error = failure;
            }
            for (const auto& child : children)
            {
                if (auto state = child.lock(); state && !state->finished())
                {
                    state->requestStop();
                }
            }
        }
    };
    auto joined = std::make_shared<join_state>();
    for (const auto& handle : handles)
    {
        joined->children.push_back(handle.checked());
    }
    for (const auto& handle : handles)
    {
        std::weak_ptr<task_detail::state<T>> weak = handle.checked();
        handle.checked()->onReady(
            [weak, joined]
            {
                if (auto child = weak.lock())
                {
                    joined->fail(child->error());
                }
            });
    }
    auto token = co_await thisTaskStopToken();
    task_detail::stop_callback cancellation(token,
                                            [joined] { joined->fail(std::make_exception_ptr(task_cancelled())); });
    for (const auto& handle : handles)
    {
        try
        {
            co_await handle.waitFinished();
        }
        catch (...)
        {
            joined->fail(std::current_exception());
        }
    }
    {
        std::lock_guard lock(joined->mutex);
        if (joined->error)
        {
            std::rethrow_exception(joined->error);
        }
    }
    if constexpr (std::is_void_v<T>)
    {
        co_return;
    }
    else
    {
        std::vector<T> results;
        results.reserve(handles.size());
        for (const auto& handle : handles)
        {
            results.push_back(handle.get());
        }
        co_return results;
    }
}
} // namespace vstd
