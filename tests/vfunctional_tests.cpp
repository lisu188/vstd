#include "vfunctional.h"
#include "vtraits.h"
#include "vtuple.h"
#include "vutil.h"

#include <functional>
#include <iostream>
#include <memory>
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

int add(int left, int right)
{
    return left + right;
}

int& identity(int& value)
{
    return value;
}

const char& firstCharacter(const std::string& value)
{
    return value.front();
}

std::reference_wrapper<int>& wrapperIdentity(std::reference_wrapper<int>& value)
{
    return value;
}

struct ReferenceCallback
{
    std::string value = std::string(256, 'r');

    std::string& operator()() &
    {
        return value;
    }
};

struct MoveReferenceCallback
{
    std::unique_ptr<int> value = std::make_unique<int>(23);

    std::unique_ptr<int>&& operator()() &
    {
        return std::move(value);
    }
};

struct NoncopyableReferenceCallback
{
    std::unique_ptr<int> value = std::make_unique<int>(23);

    std::unique_ptr<int>& operator()() &
    {
        return value;
    }
};

struct ExplicitCopy
{
    int value = 29;

    ExplicitCopy() = default;
    explicit ExplicitCopy(const ExplicitCopy& other) : value(other.value) {}
};

struct ExplicitCopyCallback
{
    ExplicitCopy value;

    const ExplicitCopy& operator()() &
    {
        return value;
    }
};

struct ConstValue
{
    int value = 37;
};

struct ConstValueCallback
{
    const ConstValue operator()() &
    {
        return {};
    }
};

struct Counter
{
    int calls = 0;

    int operator()(int& value) &
    {
        ++calls;
        return ++value;
    }
};

struct Object
{
    int value = 4;

    int add(int increment)
    {
        return value += increment;
    }

    int& addReference(int increment)
    {
        return value += increment;
    }

    int& argumentReference(int& argument)
    {
        return ++argument;
    }
};

struct RvalueCallable
{
    void operator()() && {}
};

template <typename F, typename... Args>
concept SupportsCall = requires(F callback, Args... args) { vstd::functional::call(callback, args...); };

template <typename F, typename... Args>
concept HasCallResult = requires { typename vstd::functional::detail::CallResult<F, Args...>; };

static_assert(!SupportsCall<int>);
static_assert(!SupportsCall<RvalueCallable>);
static_assert(!SupportsCall<decltype(&add), int>);
static_assert(!HasCallResult<int>);
static_assert(!HasCallResult<RvalueCallable>);
static_assert(HasCallResult<decltype(&add), int, int>);
static_assert(std::is_same_v<decltype(vstd::functional::call(identity, std::ref(std::declval<int&>()))), int&>);
static_assert(std::is_same_v<decltype(vstd::functional::call(identity, 1)), int>);
static_assert(std::is_same_v<decltype(vstd::functional::call(firstCharacter, std::string{})), char>);
static_assert(std::is_same_v<decltype(vstd::functional::call(ReferenceCallback{})), std::string>);
static_assert(
    std::is_same_v<decltype(vstd::functional::call(std::ref(std::declval<ReferenceCallback&>()))), std::string&>);
static_assert(
    std::is_same_v<decltype(vstd::functional::call(std::declval<MoveReferenceCallback>())), std::unique_ptr<int>>);
static_assert(!SupportsCall<NoncopyableReferenceCallback>);
static_assert(SupportsCall<std::reference_wrapper<NoncopyableReferenceCallback>>);
static_assert(std::is_same_v<decltype(vstd::functional::call(ExplicitCopyCallback{})), ExplicitCopy>);
static_assert(std::is_same_v<decltype(vstd::functional::call(ConstValueCallback{})), const ConstValue>);
static_assert(
    std::is_same_v<decltype(vstd::functional::call(std::ref(std::declval<ConstValueCallback&>()))), const ConstValue>);
static_assert(std::is_same_v<decltype(vstd::functional::call(wrapperIdentity, std::ref(std::declval<int&>()))),
                             std::reference_wrapper<int>>);
static_assert(std::is_void_v<decltype(vstd::functional::call([]() {}))>);
static_assert(!SupportsCall<decltype(&Object::value), Object>);
static_assert(SupportsCall<decltype(&Object::value), Object*>);
static_assert(SupportsCall<decltype(&Object::value), std::reference_wrapper<Object>>);
static_assert(!SupportsCall<decltype(&Object::value), std::shared_ptr<Object>>);
static_assert(!SupportsCall<decltype(&Object::addReference), Object, int>);
static_assert(SupportsCall<decltype(&Object::addReference), Object*, int>);
static_assert(SupportsCall<decltype(&Object::addReference), std::reference_wrapper<Object>, int>);
static_assert(!SupportsCall<decltype(&Object::addReference), std::shared_ptr<Object>, int>);
static_assert(std::is_same_v<decltype(vstd::functional::call(&Object::addReference, std::declval<Object*>(), 1)), int>);
static_assert(
    std::is_same_v<decltype(vstd::functional::call(&Object::argumentReference, std::declval<Object*>(), 1)), int>);
static_assert(std::is_same_v<decltype(vstd::functional::call(&Object::argumentReference, std::declval<Object*>(),
                                                             std::ref(std::declval<int&>()))),
                             int&>);

static_assert(std::is_same_v<vstd::tuple_element<0, int, const std::string&, void>::type, int>);
static_assert(std::is_same_v<vstd::tuple_element<1, int, const std::string&, void>::type, const std::string&>);
static_assert(std::is_same_v<vstd::tuple_element<2, int, const std::string&, void>::type, void>);
static_assert(vstd::tuple_size<>::size == 0);
static_assert(vstd::tuple_size<void>::size == 0);
static_assert(vstd::tuple_size<int>::size == 1);
static_assert(vstd::tuple_size<int, void>::size == 1);
static_assert(vstd::tuple_size<void, int>::size == 2);
static_assert(vstd::tuple_size<int, void, int>::size == 3);
using NullaryCallback = decltype([]() { return 7; });
using UnaryCallback = decltype([](int value) { return value + 1; });
static_assert(std::is_same_v<vstd::function_traits<NullaryCallback>::first_arg, void>);
static_assert(std::is_same_v<vstd::function_traits<UnaryCallback>::first_arg, int>);

void checkCallableKinds()
{
    require(vstd::functional::call(add, 2, 3) == 5, "function pointer invocation");
    require(vstd::functional::call([](auto left, auto right) { return left + right; }, 4, 5) == 9,
            "generic lambda invocation");

    auto mutable_callback = [value = 1](int increment) mutable { return value += increment; };
    require(vstd::functional::call(mutable_callback, 2) == 3, "mutable lambda invocation");
    require(vstd::functional::call(mutable_callback, 2) == 3, "mutable callback is still copied");

    int calls = 0;
    vstd::functional::call([&calls]() { ++calls; });
    require(calls == 1, "void return invocation");
}

void checkCopyAndReferenceSemantics()
{
    Counter callback;
    int value = 10;
    require(vstd::functional::call(callback, value) == 11, "copied arguments are invoked as lvalues");
    require(callback.calls == 0, "callback invocation leaves the original copy unchanged");
    require(value == 10, "argument mutation leaves the original copy unchanged");

    require(vstd::functional::call(std::ref(callback), std::ref(value)) == 11, "explicit reference invocation");
    require(callback.calls == 1 && value == 11, "reference wrappers retain caller-owned state");

    int& reference = vstd::functional::call(identity, std::ref(value));
    require(&reference == &value, "reference return retains identity");
    reference = 17;
    require(value == 17, "reference return permits mutation");
}

void checkMemberPointers()
{
    Object object;
    require(vstd::functional::call(&Object::add, object, 3) == 7,
            "member function with value return can use a copied object");
    require(object.value == 4, "member function invocation leaves the original object copy unchanged");
    require(vstd::functional::call(&Object::add, &object, 3) == 7, "member function pointer with object pointer");
    require(vstd::functional::call(&Object::add, std::ref(object), 2) == 9,
            "member function pointer with reference wrapper");
    int& member = vstd::functional::call(&Object::value, &object);
    require(&member == &object.value, "member data pointer preserves reference return");
    member = 12;
    require(object.value == 12, "member data pointer permits mutation");
    int increment = 2;
    int& returned_member = vstd::functional::call(&Object::addReference, std::ref(object), std::ref(increment));
    require(&returned_member == &object.value && returned_member == 14,
            "member function reference result retains caller-owned object state");

    int argument = 20;
    require(vstd::functional::call(&Object::argumentReference, &object, argument) == 21,
            "member reference into a copied argument is materialized");
    require(argument == 20, "member reference materialization retains by-value argument semantics");
    int& borrowed_argument = vstd::functional::call(&Object::argumentReference, &object, std::ref(argument));
    require(&borrowed_argument == &argument && argument == 21,
            "member reference into a borrowed argument retains identity");
}

void checkReferenceResultLifetime()
{
    std::string argument(256, 'a');
    auto first = vstd::functional::call(firstCharacter, argument);
    require(first == 'a', "reference into a copied argument is materialized before destruction");

    ReferenceCallback callback;
    auto result = vstd::functional::call(callback);
    require(result == callback.value, "reference into a copied callable is materialized before destruction");
    std::string& borrowed = vstd::functional::call(std::ref(callback));
    require(&borrowed == &callback.value, "explicitly borrowed callable retains reference identity");

    int value = 7;
    auto wrapper = vstd::functional::call(wrapperIdentity, std::ref(value));
    require(&wrapper.get() == &value, "reference to a local wrapper is materialized as a borrowed handle");

    auto moved_result = vstd::functional::call(MoveReferenceCallback{});
    require(*moved_result == 23, "rvalue reference into copied callable state is moved into an owning result");

    auto explicit_result = vstd::functional::call(ExplicitCopyCallback{});
    require(explicit_result.value == 29, "materialized reference results support explicit copy constructors");

    ConstValueCallback const_callback;
    require(vstd::functional::call(const_callback).value == 37, "const value results retain their value");
    require(vstd::functional::call(std::ref(const_callback)).value == 37,
            "borrowed const value results retain their value");
}

void checkFunctionTraitCompatibility()
{
    auto nullary = vstd::make_function(NullaryCallback{});
    auto unary = vstd::make_function(UnaryCallback{});
    require(nullary() == 7, "nullary function wrapping retains the void first-argument sentinel");
    require(unary(4) == 5, "unary function wrapping retains its first-argument type");
}
} // namespace

int main()
{
    try
    {
        checkCallableKinds();
        checkCopyAndReferenceSemantics();
        checkMemberPointers();
        checkReferenceResultLifetime();
        checkFunctionTraitCompatibility();
        std::cout << "Functional invocation and tuple compatibility checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
