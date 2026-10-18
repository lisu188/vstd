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

#include <functional>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace vstd
{
namespace detail
{
template <typename Key, typename Value> class cache_storage
{
    struct entry
    {
        Value value;
        int inserted_at;
        int ttl;

        bool valid_at(int time) const noexcept
        {
            // Ordered signed ticks have an exact unsigned elapsed difference, even
            // across zero. Avoid forming an overflowing signed expiry deadline.
            using unsigned_tick = std::make_unsigned_t<int>;
            return time >= inserted_at && static_cast<unsigned_tick>(time) - static_cast<unsigned_tick>(inserted_at) <=
                                              static_cast<unsigned_tick>(ttl);
        }
    };

    std::unordered_map<Key, entry> _values;

  public:
    template <typename Loader> Value get(Key key, int time, Loader&& load, int (*get_ttl)())
    {
        auto it = _values.find(key);
        if (it != _values.end())
        {
            if (it->second.valid_at(time))
            {
                return it->second.value;
            }
            _values.erase(it);
        }

        Value value = std::invoke(std::forward<Loader>(load));
        const int ttl = get_ttl();
        if (ttl >= 0)
        {
            _values.emplace(std::move(key), entry{value, time, ttl});
        }
        return value;
    }
};
} // namespace detail

// Integer timestamps and TTLs use caller-defined matching units. A cached value
// remains valid at the inclusive TTL boundary; zero TTL reuses the same tick.
// Negative TTLs bypass storage. A backward tick invalidates that key's value.
template <typename key, typename val, val (*get_next)(), int (*get_ttl)()> class cache
{
    detail::cache_storage<key, val> _storage;

  public:
    val get(key k, int time)
    {
        return _storage.get(std::move(k), time, get_next, get_ttl);
    }
};

template <typename key, typename val, int (*get_ttl)()> class cache2
{
    detail::cache_storage<key, val> _storage;

  public:
    val get(key k, int time, std::function<val()> cb)
    {
        return _storage.get(std::move(k), time, std::move(cb), get_ttl);
    }
};
} // namespace vstd
