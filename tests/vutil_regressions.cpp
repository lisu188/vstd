// Copyright (c) 2026 Andrzej Lis. MIT License.
#include "vhex.h"
#include "vutil.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct Value
{
    static inline std::set<const Value*> live;
    static inline int copies_remaining = -1;
    int value;

    explicit Value(int initial) : value(initial)
    {
        live.insert(this);
    }
    Value(const Value& other) : value(other.value)
    {
        if (copies_remaining == 0)
        {
            throw std::runtime_error("copy failed");
        }
        if (copies_remaining > 0)
        {
            --copies_remaining;
        }
        live.insert(this);
    }
    Value& operator=(const Value& other)
    {
        require(live.contains(this), "array assignment requires a constructed element");
        value = other.value;
        return *this;
    }
    ~Value()
    {
        live.erase(this);
    }
};

void checkArrayLifetime()
{
    std::vector<Value> source;
    source.reserve(3);
    source.emplace_back(11);
    source.emplace_back(22);
    source.emplace_back(33);
    auto* array = vstd::as_array(source);
    require(Value::live.size() == 6, "array elements must be copy constructed");
    require(array[0].value == 11 && array[2].value == 33, "array retains values");
    std::destroy_n(array, source.size());
    vstd::deallocate(array, source.size());
    require(Value::live.size() == 3, "array elements can be destroyed before releasing storage");

    Value::copies_remaining = 1;
    bool threw = false;
    try
    {
        auto* unexpected = vstd::as_array(source);
        std::destroy_n(unexpected, source.size());
        vstd::deallocate(unexpected, source.size());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    Value::copies_remaining = -1;
    require(threw && Value::live.size() == 3, "failed copying destroys partially constructed elements");
    require(vstd::as_array(std::vector<Value>{}) == nullptr, "empty arrays do not allocate storage");

    std::vector<std::string> strings{std::string(256, 'a'), std::string(256, 'b')};
    auto* copied_strings = vstd::as_array(strings);
    require(copied_strings[0] == strings[0] && copied_strings[1] == strings[1], "nontrivial string array");
    std::destroy_n(copied_strings, strings.size());
    vstd::deallocate(copied_strings, strings.size());
}

void checkBuilders()
{
    require(vstd::set(1, 2, 1, 3) == std::set<int>({1, 2, 3}), "variadic set deduplicates values");
    require(vstd::as_list(1, 2, 3) == std::list<int>({1, 2, 3}), "variadic list retains argument order");
    require(vstd::set(7) == std::set<int>({7}) && vstd::as_list(7).front() == 7,
            "single-argument builders remain supported");
}

std::string pointerStream(const void* pointer)
{
    std::ostringstream stream;
    stream << std::hex << pointer;
    return boost::to_upper_copy(stream.str());
}

void pointerTarget() {}

void checkPointerHex()
{
    int value = 42;
    require(vstd::to_hex(&value) == vstd::to_hex(reinterpret_cast<std::uintptr_t>(&value)),
            "raw pointer uses its integer representation");
    require(vstd::to_hex(static_cast<int*>(nullptr)) == "0", "null raw pointer");
    auto shared = std::make_shared<int>(42);
    require(vstd::to_hex(shared) == vstd::to_hex<int*>(shared.get()),
            "shared pointer retains legacy stream formatting");
    require(vstd::to_hex(shared) == pointerStream(shared.get()), "shared pointer uses native address formatting");
    auto character = std::make_shared<char>('a');
    require(vstd::to_hex(character) == pointerStream(character.get()),
            "shared character pointers format an address rather than read a C string");
    auto characters = std::make_shared<char[]>(2);
    characters[0] = 'a';
    characters[1] = '\0';
    require(vstd::to_hex(characters) == pointerStream(characters.get()), "shared character array formats an address");
    auto volatile_character = std::make_shared<volatile char>('v');
    require(vstd::to_hex(volatile_character) == pointerStream(const_cast<const char*>(volatile_character.get())),
            "shared volatile character formats an address");
    require(vstd::to_hex(std::shared_ptr<char>{}) == pointerStream(nullptr), "empty shared character pointer");
    require(vstd::to_hex(std::shared_ptr<int>{}) == pointerStream(nullptr), "empty shared object pointer");
    require(vstd::to_hex(std::shared_ptr<void>{}) == pointerStream(nullptr), "empty shared void pointer");
    std::shared_ptr<const void> erased = shared;
    require(vstd::to_hex(erased) == pointerStream(erased.get()), "shared void pointer formats an address");
    using Function = void();
    std::shared_ptr<Function> function(&pointerTarget, [](Function*) {});
    require(vstd::to_hex(function) == vstd::to_hex<Function*>(function.get()),
            "shared function pointer retains legacy stream formatting");
    require(vstd::to_hex(std::shared_ptr<Function>{}) == vstd::to_hex<Function*>(nullptr),
            "empty shared function pointer retains legacy stream formatting");
}

void checkWideHash()
{
    std::size_t expected = 0;
    for (int value = 1; value <= 12; ++value)
    {
        expected = expected * 31 + std::hash<int>{}(value);
    }
    require(vstd::hash_combine(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) == expected,
            "wide hashes use size_t arithmetic without signed constant overflow");
    require(vstd::hash_combine(1, 2, 3) ==
                31 * 31 * std::hash<int>{}(1) + 31 * std::hash<int>{}(2) + std::hash<int>{}(3),
            "existing narrow hash values are preserved");
}

static_assert(vstd::is_shared_ptr<std::shared_ptr<int>>::value);
static_assert(vstd::is_shared_ptr<const std::shared_ptr<int>&>::value);
static_assert(!vstd::is_shared_ptr<std::unique_ptr<int>>::value);
static_assert(!vstd::is_shared_ptr<std::weak_ptr<int>>::value);
static_assert(std::is_same_v<vstd::clear_type<const int&>::type, int>);
static_assert(vstd::is_same_clear<const std::string&, std::string>::value);
} // namespace

int main()
{
    try
    {
        checkArrayLifetime();
        require(Value::live.empty(), "all array test elements are destroyed");
        checkBuilders();
        checkPointerHex();
        checkWideHash();
        std::cout << "Array lifetime, builders, pointer hex, hash and type regressions passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
