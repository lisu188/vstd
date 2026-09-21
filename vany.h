/*
 * MIT License
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

#include "vcast.h"
#include <any>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vstd
{
namespace detail
{
struct any_key_hash
{
    std::size_t operator()(const std::pair<std::type_index, std::type_index>& key) const noexcept
    {
        const auto seed = key.first.hash_code();
        return seed ^ (key.second.hash_code() + 0x9e3779b9 + (seed << 6) + (seed >> 2));
    }
};

class any_registry
{
  public:
    using converter = std::function<std::any(const std::any&)>;

  private:
    using key_type = std::pair<std::type_index, std::type_index>;
    std::unordered_map<key_type, converter, any_key_hash> _converters;
    mutable std::shared_mutex _mutex;

  public:
    void add(std::type_index target, std::type_index source, converter function)
    {
        std::unique_lock lock(_mutex);
        _converters.insert_or_assign({target, source}, std::move(function));
    }

    std::vector<converter> path(std::type_index target, std::type_index source) const
    {
        std::shared_lock lock(_mutex);
        if (auto found = _converters.find({target, source}); found != _converters.end())
        {
            return {found->second};
        }
        struct step
        {
            std::type_index type;
            std::vector<converter> functions;
        };
        std::vector<step> queue{{source, {}}};
        std::unordered_set<std::type_index> visited{source};
        for (std::size_t i = 0; i < queue.size(); ++i)
        {
            const auto current = queue[i];
            for (const auto& [key, function] : _converters)
            {
                if (key.second != current.type || !visited.insert(key.first).second)
                {
                    continue;
                }
                auto functions = current.functions;
                functions.push_back(function);
                if (key.first == target)
                {
                    return functions;
                }
                queue.push_back({key.first, std::move(functions)});
            }
        }
        throw std::bad_any_cast();
    }

    std::any convert(std::type_index target, const std::any& value) const
    {
        const auto functions = path(target, value.type());
        std::any converted = functions.front()(value);
        for (std::size_t i = 1; i < functions.size(); ++i)
        {
            converted = functions[i](converted);
        }
        return converted;
    }
};

template <typename T = void> any_registry& registry()
{
    static any_registry value;
    return value;
}
} // namespace detail

template <typename T>
    requires(!std::is_reference_v<T>)
T any_cast(const std::any& value)
{
    if constexpr (std::is_same_v<std::remove_cv_t<T>, std::any>)
    {
        return value;
    }
    else
    {
        if (auto exact = std::any_cast<std::remove_cv_t<T>>(&value))
        {
            return *exact;
        }
        return std::any_cast<T>(detail::registry().convert(typeid(T), value));
    }
}

template <typename T>
    requires(std::is_lvalue_reference_v<T>)
T any_cast(std::any& value)
{
    return std::any_cast<T>(value);
}

template <typename T>
    requires(std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>>)
T any_cast(const std::any& value)
{
    return std::any_cast<T>(value);
}

template <typename T>
    requires(std::is_reference_v<T>)
T any_cast(std::any&&) = delete;

template <typename T>
    requires(std::is_reference_v<T>)
T any_cast(const std::any&&) = delete;

template <typename T, typename U> void register_any_type()
{
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<U>, "any conversions must own their values");
    detail::registry().add(typeid(T), typeid(U), [](const std::any& value)
                           { return std::any(vstd::cast<T>(std::any_cast<const U&>(value))); });
}
} // namespace vstd
