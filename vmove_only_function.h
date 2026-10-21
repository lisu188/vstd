/*
 * Copyright (c) 2026 Andrzej Lis
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace vstd
{
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L &&                                \
    !defined(VSTD_FORCE_PORTABLE_MOVE_ONLY_FUNCTION)
template <typename Signature> using move_only_function = std::move_only_function<Signature>;
#else
// libc++ does not provide std::move_only_function yet. Only unqualified signatures are used by vstd.
template <typename Signature> class move_only_function;

template <typename Result, typename... Args> class move_only_function<Result(Args...)>
{
    struct callable_base
    {
        virtual ~callable_base() = default;
        virtual Result invoke(Args&&... args) = 0;
    };

    template <typename Function> struct callable final : callable_base
    {
        template <typename Value> explicit callable(Value&& value) : function(std::forward<Value>(value)) {}

        Result invoke(Args&&... args) override
        {
            return std::invoke_r<Result>(function, std::forward<Args>(args)...);
        }

        Function function;
    };

  public:
    move_only_function() noexcept = default;
    move_only_function(std::nullptr_t) noexcept {}
    move_only_function(move_only_function&&) noexcept = default;
    move_only_function& operator=(move_only_function&&) noexcept = default;
    move_only_function(const move_only_function&) = delete;
    move_only_function& operator=(const move_only_function&) = delete;

    template <typename Function, typename Stored = std::decay_t<Function>>
        requires(!std::is_same_v<Stored, move_only_function> && std::is_constructible_v<Stored, Function> &&
                 std::is_invocable_r_v<Result, Stored&, Args...> &&
                 (!std::is_reference_v<Result> || std::is_reference_v<std::invoke_result_t<Stored&, Args...>>))
    move_only_function(Function&& function)
    {
        if constexpr (std::is_pointer_v<Stored> || std::is_member_pointer_v<Stored>)
        {
            if (function == nullptr)
            {
                return;
            }
        }
        target = std::make_unique<callable<Stored>>(std::forward<Function>(function));
    }

    template <typename Function>
        requires(!std::is_same_v<std::decay_t<Function>, move_only_function> &&
                 std::is_constructible_v<move_only_function, Function>)
    move_only_function& operator=(Function&& function)
    {
        move_only_function(std::forward<Function>(function)).swap(*this);
        return *this;
    }

    move_only_function& operator=(std::nullptr_t) noexcept
    {
        target.reset();
        return *this;
    }

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(target);
    }

    Result operator()(Args... args)
    {
        assert(target);
        return target->invoke(std::forward<Args>(args)...);
    }

    void swap(move_only_function& other) noexcept
    {
        target.swap(other.target);
    }

    friend void swap(move_only_function& left, move_only_function& right) noexcept
    {
        left.swap(right);
    }

    friend bool operator==(const move_only_function& function, std::nullptr_t) noexcept
    {
        return !function;
    }

  private:
    std::unique_ptr<callable_base> target;
};
#endif
} // namespace vstd
