#include "vbind.h"

#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct ReferenceSource
{
    static inline std::set<const int*> live_values;
    static inline int copies = 0;
    int value = 7;

    ReferenceSource()
    {
        live_values.insert(&value);
    }

    ReferenceSource(const ReferenceSource& other) : value(other.value)
    {
        ++copies;
        live_values.insert(&value);
    }

    ~ReferenceSource()
    {
        live_values.erase(&value);
    }

    int& operator()() &
    {
        return ++value;
    }
};

struct MutableSource
{
    int value = 0;

    int operator()()
    {
        return ++value;
    }
};

int& identity(int& value)
{
    return value;
}

struct ExplicitCopy
{
    int value = 31;

    ExplicitCopy() = default;
    explicit ExplicitCopy(const ExplicitCopy& other) : value(other.value) {}
};

template <typename Binder>
concept SupportsNullaryBinder = requires(Binder& binder) { binder(); };

using UnaryBinder = decltype(vstd::partial::bind([](int value) { return value; }));
static_assert(!SupportsNullaryBinder<UnaryBinder>);
static_assert(!std::is_invocable_v<UnaryBinder&>);
static_assert(!std::is_invocable_v<UnaryBinder&, const char*>);
static_assert(std::is_invocable_v<UnaryBinder&, int>);
static_assert(std::is_invocable_v<UnaryBinder&, int&>);

using ReferenceBinder = decltype(vstd::partial::bind(ReferenceSource{}));
static_assert(std::is_same_v<decltype(std::declval<ReferenceBinder&>()()), int&>);
using IdentityBinder = decltype(vstd::partial::bind(identity, 3));
static_assert(std::is_same_v<decltype(std::declval<IdentityBinder&>()()), int&>);
using UniqueValueBinder = decltype(vstd::partial::bind([]() { return std::make_unique<int>(1); }));
using NoncopyableResultBinder =
    decltype(vstd::partial::bind([](const std::unique_ptr<int>& value) -> const std::unique_ptr<int>& { return value; },
                                 std::declval<UniqueValueBinder>()));
static_assert(!SupportsNullaryBinder<NoncopyableResultBinder>);

void checkNestedReferenceLifetime()
{
    auto inner = vstd::partial::bind(ReferenceSource{});
    auto outer = vstd::partial::bind(
        [](int& value)
        {
            require(ReferenceSource::live_values.contains(&value), "nested reference source stays alive during use");
            return value;
        },
        inner);
    const int initial_copies = ReferenceSource::copies;
    require(outer() == 8, "nested binder returns its first result");
    require(outer() == 9, "nested binder retains state between calls");
    require(ReferenceSource::copies == initial_copies, "expansion does not copy the stored nested callable");
    require(inner() == 8, "binding owns a separate copy of an lvalue nested binder");

    auto reference_outer = vstd::partial::bind(identity, vstd::partial::bind(ReferenceSource{}));
    int& reference = reference_outer();
    require(ReferenceSource::live_values.contains(&reference), "nested persistent reference can escape its invocation");
    require(&reference_outer() == &reference && reference == 9, "nested persistent reference retains identity");
}

void checkNestedValueResultLifetime()
{
    auto value_source = vstd::partial::bind([]() { return std::string(256, 't'); });
    auto outer =
        vstd::partial::bind([](const std::string& value) -> const char& { return value.front(); }, value_source);
    static_assert(std::is_same_v<decltype(outer()), char>);
    auto character = outer();
    require(character == 't', "reference into a nested temporary is materialized before destruction");

    auto twice_nested = vstd::partial::bind([](const char& value) -> const char& { return value; }, outer);
    static_assert(std::is_same_v<decltype(twice_nested()), char>);
    require(twice_nested() == 't', "materialization propagates through multiple value-producing expansions");

    auto explicit_copy = vstd::partial::bind([](const ExplicitCopy& value) -> const ExplicitCopy& { return value; },
                                             vstd::partial::bind([]() { return ExplicitCopy{}; }));
    static_assert(std::is_same_v<decltype(explicit_copy()), ExplicitCopy>);
    require(explicit_copy().value == 31, "nested temporary materialization supports explicit copy constructors");
}

void checkBoundArgumentLifetime()
{
    auto bound = vstd::partial::bind(identity, 3);
    int& first = bound();
    first = 11;
    require(&bound() == &first && bound() == 11, "bound argument references address persistent closure storage");

    int borrowed = 4;
    auto referenced = vstd::partial::bind(identity, std::ref(borrowed));
    require(&referenced() == &borrowed, "explicit reference wrapper retains caller storage");
}

void checkCallTimeArguments()
{
    auto identity_binder = vstd::partial::bind(identity);
    int value = 12;
    require(&identity_binder(value) == &value, "call-time lvalue arguments are forwarded without copying");

    auto moved_argument = vstd::partial::bind([](std::unique_ptr<int> pointer) { return *pointer; });
    require(moved_argument(std::make_unique<int>(14)) == 14, "call-time move-only arguments are forwarded");

    auto stored_callable = vstd::partial::bind([pointer = std::make_unique<int>(15)]() { return *pointer; });
    require(stored_callable() == 15, "binder owns a move-only callable");

    auto nested =
        vstd::partial::bind([](int left, int right) { return left + right; }, vstd::partial::bind(MutableSource{}));
    require(nested(10) == 11 && nested(10) == 12, "nested expansion and call-time arguments compose");
}
} // namespace

int main()
{
    try
    {
        checkNestedReferenceLifetime();
        checkNestedValueResultLifetime();
        checkBoundArgumentLifetime();
        checkCallTimeArguments();
        require(ReferenceSource::live_values.empty(), "all binder-owned sources are destroyed after their owners");
        std::cout << "Partial binder lifetime checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
