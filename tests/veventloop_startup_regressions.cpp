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

#include <atomic>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
SDL_malloc_func originalMalloc;
SDL_calloc_func originalCalloc;
SDL_realloc_func originalRealloc;
SDL_free_func originalFree;
thread_local bool pauseTimerAllocation = false;
std::atomic_bool initializationPaused = false;
std::atomic_bool releaseInitialization = false;

void pauseInitialization(std::size_t bytes)
{
    // A fresh Windows timer initialization allocates this hint's name after marking ticks started
    // and before recording their frequency. Event-queue allocations must remain unblocked.
    if (pauseTimerAllocation && bytes == sizeof(SDL_HINT_TIMER_RESOLUTION))
    {
        pauseTimerAllocation = false;
        initializationPaused.store(true, std::memory_order_release);
        while (!releaseInitialization.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }
}

void* SDLCALL allocate(std::size_t bytes)
{
    pauseInitialization(bytes);
    return originalMalloc(bytes);
}

void* SDLCALL allocateZero(std::size_t count, std::size_t bytes)
{
    pauseInitialization(count * bytes);
    return originalCalloc(count, bytes);
}

void* SDLCALL resize(void* pointer, std::size_t bytes)
{
    pauseInitialization(bytes);
    return originalRealloc(pointer, bytes);
}

void SDLCALL release(void* pointer)
{
    originalFree(pointer);
}

unsigned long runReady(vstd::event_loop<>* loop)
{
#if defined(_WIN32) && defined(_MSC_VER)
    __try
    {
        loop->runReady();
        return 0;
    }
    __except (GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH)
    {
        return GetExceptionCode();
    }
#else
    loop->runReady();
    return 0;
#endif
}
} // namespace

int main()
{
    // This executable must be a separate CTest process: SDL timer state cannot be made fresh by
    // another event-loop test that has already initialized it.
    SDL_GetMemoryFunctions(&originalMalloc, &originalCalloc, &originalRealloc, &originalFree);
    if (SDL_SetMemoryFunctions(allocate, allocateZero, resize, release) != 0)
    {
        std::fprintf(stderr, "Cannot install fresh-process SDL allocation fixture: %s\n", SDL_GetError());
        return 1;
    }
    auto loop = std::make_shared<vstd::event_loop<>>();
    std::atomic_bool workerFinished = false;
    int callbacks = 0;
    std::thread worker(
        [&]
        {
            pauseTimerAllocation = true;
            loop->invoke([&] { ++callbacks; });
            pauseTimerAllocation = false;
            workerFinished.store(true, std::memory_order_release);
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!initializationPaused.load(std::memory_order_acquire) && !workerFinished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    const auto fault = initializationPaused.load(std::memory_order_acquire) ? runReady(loop.get()) : 0;
    releaseInitialization.store(true, std::memory_order_release);
    worker.join();
    loop->runReady();
    loop.reset();
    SDL_Quit();
    if (fault != 0 || initializationPaused.load(std::memory_order_acquire) || callbacks != 1)
    {
        std::fprintf(stderr, "First worker post: timer initialized on worker=%d, fault=0x%08lx, callbacks=%d\n",
                     initializationPaused.load(std::memory_order_acquire), fault, callbacks);
        return 1;
    }
    std::puts("First worker post and owner polling use initialized SDL ticks.");
}
