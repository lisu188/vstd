// Copyright (c) 2026 Andrzej Lis. MIT License.
#include "vcache.h"

#include <climits>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
int ttl = 5;
int value_calls = 0;
int ttl_calls = 0;
bool fail_value = false;
bool fail_ttl = false;

int next_value()
{
    ++value_calls;
    if (fail_value)
    {
        throw std::runtime_error("value load failed");
    }
    return value_calls;
}

int next_ttl()
{
    ++ttl_calls;
    if (fail_ttl)
    {
        throw std::runtime_error("TTL load failed");
    }
    return ttl;
}

void reset(int lifetime = 5)
{
    ttl = lifetime;
    value_calls = ttl_calls = 0;
    fail_value = fail_ttl = false;
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Callback> void require_failure(Callback callback)
{
    bool threw = false;
    try
    {
        callback();
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    require(threw, "provider failures must propagate");
}

struct function_cache
{
    vstd::cache<std::string, int, next_value, next_ttl> values;
    int get(std::string key, int time)
    {
        return values.get(std::move(key), time);
    }
};

struct callback_cache
{
    vstd::cache2<std::string, int, next_ttl> values;
    int get(std::string key, int time)
    {
        return values.get(std::move(key), time, next_value);
    }
};

template <typename Cache> void check_inclusive_boundary()
{
    reset();
    Cache cache;
    require(cache.get("alpha", 10) == 1, "load a missing key");
    require(cache.get("alpha", 14) == 1, "reuse a value before its deadline");
    require(cache.get("alpha", 15) == 1, "preserve the inclusive legacy deadline");
    require(value_calls == 1 && ttl_calls == 1, "hits do not invoke either provider");
    require(cache.get("alpha", 16) == 2, "refresh after the deadline");
    require(value_calls == 2 && ttl_calls == 2, "sample TTL once for each loaded value");
}

template <typename Cache> void check_nonpositive_ttl()
{
    reset(0);
    Cache zero;
    require(zero.get("alpha", 7) == 1 && zero.get("alpha", 7) == 1, "zero TTL reuses the same tick");
    require(zero.get("alpha", 8) == 2, "zero TTL refreshes on the next tick");

    for (int negative : {-1, INT_MIN})
    {
        reset(negative);
        Cache uncached;
        require(uncached.get("alpha", INT_MIN) == 1, "negative TTL returns the loaded value");
        require(uncached.get("alpha", INT_MIN) == 2, "negative TTL does not store a same-tick value");
        require(uncached.get("alpha", INT_MIN + 1) == 3, "negative TTL remains uncached across ticks");
        require(value_calls == 3 && ttl_calls == 3, "negative TTL samples each successful load");
    }
}

template <typename Cache> void check_integer_limits()
{
    reset();
    Cache high;
    require(high.get("alpha", INT_MAX - 2) == 1, "load near the largest signed tick");
    require(high.get("alpha", INT_MAX) == 1, "TTL extending past INT_MAX does not overflow or expire early");
    require(high.get("alpha", INT_MIN) == 2, "timer wrap or reset invalidates the previous epoch");
    require(high.get("alpha", INT_MIN + 5) == 2, "inclusive deadline works near INT_MIN");
    require(high.get("alpha", INT_MIN + 6) == 3, "expiry works near INT_MIN");

    reset(INT_MAX);
    Cache longest;
    require(longest.get("alpha", INT_MIN) == 1, "load the largest TTL at the smallest tick");
    require(longest.get("alpha", -1) == 1, "largest TTL retains its exact inclusive boundary");
    require(longest.get("alpha", 0) == 2, "elapsed time beyond INT_MAX expires without signed subtraction");

    reset(0);
    Cache full_range;
    require(full_range.get("alpha", INT_MIN) == 1, "load at the minimum tick");
    require(full_range.get("alpha", INT_MAX) == 2, "compare the complete unsigned elapsed range safely");

    reset();
    Cache negative_ticks;
    require(negative_ticks.get("alpha", -10) == 1 && negative_ticks.get("alpha", -5) == 1,
            "negative tick epochs retain an inclusive boundary");
    require(negative_ticks.get("alpha", -4) == 2, "negative tick epochs expire normally");
}

template <typename Cache> void check_rollback_and_key_isolation()
{
    reset();
    Cache rollback;
    require(rollback.get("alpha", 10) == 1, "load before a timer reset");
    require(rollback.get("alpha", 9) == 2, "backward time refreshes instead of reusing a future value");
    require(rollback.get("alpha", 14) == 2 && rollback.get("alpha", 15) == 3,
            "a reset value starts its own inclusive lifetime");

    reset();
    Cache independent;
    require(independent.get("alpha", 0) == 1 && independent.get("beta", 1) == 2, "keys load independently");
    require(independent.get("alpha", 6) == 3 && independent.get("beta", 6) == 2,
            "refreshing one key preserves another key's inclusive deadline");
}

template <typename Cache> void check_provider_failures_and_ttl_sampling()
{
    reset();
    Cache cache;
    fail_value = true;
    require_failure([&] { cache.get("alpha", 0); });
    require(ttl_calls == 0, "a failed value load does not sample TTL");
    fail_value = false;
    require(cache.get("alpha", 0) == 2 && cache.get("alpha", 5) == 2, "a failed initial load can recover");
    fail_value = true;
    require_failure([&] { cache.get("alpha", 6); });
    fail_value = false;
    require(cache.get("alpha", 6) == 4, "a failed expired load can recover");

    reset();
    Cache ttl_failure;
    fail_ttl = true;
    require_failure([&] { ttl_failure.get("alpha", 0); });
    fail_ttl = false;
    require(ttl_failure.get("alpha", 0) == 2, "a failed TTL provider does not publish a value");
    ttl = -1;
    require(ttl_failure.get("alpha", 5) == 2 && ttl_calls == 2, "a hit retains the TTL sampled during its load");
    require(ttl_failure.get("alpha", 6) == 3 && ttl_failure.get("alpha", 6) == 4,
            "a new negative TTL takes effect at the next refresh");
}

template <typename Cache> void check_adapter()
{
    check_inclusive_boundary<Cache>();
    check_nonpositive_ttl<Cache>();
    check_integer_limits<Cache>();
    check_rollback_and_key_isolation<Cache>();
    check_provider_failures_and_ttl_sampling<Cache>();
}
} // namespace

int main()
{
    try
    {
        check_adapter<function_cache>();
        check_adapter<callback_cache>();
        std::cout << "cache/cache2 deterministic expiry regressions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
