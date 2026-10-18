// Copyright (c) 2026 Andrzej Lis
// SPDX-License-Identifier: MIT
#include <vstd.h>
#include <vthreadpool.h>

#include <chrono>
#include <future>
#include <memory>
#include <vector>

int main()
{
    if (vstd::stem("example.txt") != "example")
    {
        return 1;
    }
    const std::vector<int> values{2, 3};
    auto* copy = vstd::as_array(values);
    const bool copied = copy[0] == 2 && copy[1] == 3;
    std::destroy_n(copy, values.size());
    vstd::deallocate(copy, values.size());
    if (!copied)
    {
        return 2;
    }
    auto pool = std::make_shared<vstd::thread_pool<1>>()->start();
    std::promise<int> completed;
    auto result = completed.get_future();
    pool->execute([&completed]() { completed.set_value(42); });
    if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
    {
        return 3;
    }
    pool->stop();
    return result.get() == 42 ? 0 : 4;
}
