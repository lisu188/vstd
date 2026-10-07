#include "vstring.h"

#include <climits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void checkReplacement()
{
    require(vstd::replace("red red", "red", "blue") == "blue blue", "replace every original match");
    require(vstd::replace("unchanged", "missing", "value") == "unchanged", "no matching text");
    require(vstd::replace("text", "", "prefix") == "text", "empty search leaves text unchanged");
    require(vstd::replace("aaaa", "aa", "aa") == "aaaa", "identity replacement terminates");
    require(vstd::replace("a\"b\"", "\"", "\\\"") == "a\\\"b\\\"", "escape each original quote once");
    require(vstd::replace("aaaa", "aa", "a") == "aa", "skip replacement text and newly formed matches");
    require(vstd::replace("aaaa", "aa", "") == "", "remove adjacent matches");
    require(vstd::replace("aaaaa", "aa", "b") == "bba", "non-overlapping matches");
    require(vstd::replace(std::string(20000, 'a'), "a", "b") == std::string(20000, 'b'), "many matches do not recurse");
    const std::string binary("a\0a", 3);
    require(vstd::replace(binary, std::string("\0", 1), "--") == "a--a", "embedded nulls are preserved");
}

void checkStringPredicates()
{
    require(vstd::is_empty(""), "empty text");
    require(vstd::is_empty(" \t\r\n"), "whitespace-only text");
    require(!vstd::is_empty(" a "), "non-empty trimmed text");
    require(vstd::ends_with("report.txt", ".txt"), "matching suffix");
    require(vstd::ends_with("", ""), "empty suffix on empty text");
    require(vstd::ends_with("text", ""), "empty suffix");
    require(!vstd::ends_with("txt", "report.txt"), "longer suffix");
    require(!vstd::ends_with("report.TXT", ".txt"), "case-sensitive suffix");
    require(vstd::ends_with(std::string("a\0b", 3), std::string("\0b", 2)), "binary suffix");
    require(vstd::camel("hello world") == "Hello World ", "preserve title-case spacing");
    require(vstd::camel("word") == "Word", "single word");
}

void checkIntegerContract()
{
    require(vstd::to_int("42") == std::pair{42, true}, "decimal value");
    require(vstd::to_int("0042") == std::pair{42, true}, "leading zeroes are decimal");
    require(vstd::to_int("42x") == std::pair{42, false}, "partial value is returned with failure");
    require(vstd::to_int(std::to_string(INT_MAX)) == std::pair{INT_MAX, true}, "maximum int");
    require(vstd::to_int(std::to_string(INT_MAX) + "0") == std::pair{0, false}, "overflow");
    for (const std::string text : {"", "-1", "+1", " 1", "x", "\xff"})
    {
        require(vstd::to_int(text) == std::pair{0, false}, "reject empty, signed, spaced and non-digit inputs");
    }
    require(vstd::to_int(std::string("42\0x", 4)) == std::pair{42, false}, "embedded null is not a full parse");
}

class StringValue : public vstd::stringable
{
    bool& destroyed;

  public:
    explicit StringValue(bool& flag) : destroyed(flag) {}
    ~StringValue() override
    {
        destroyed = true;
    }
    std::string to_string() override
    {
        return "value";
    }
};

void checkStringableLifetime()
{
    static_assert(std::has_virtual_destructor_v<vstd::stringable>);
    bool destroyed = false;
    {
        std::unique_ptr<vstd::stringable> value = std::make_unique<StringValue>(destroyed);
        require(value->to_string() == "value", "polymorphic string conversion");
    }
    require(destroyed, "deleting through stringable destroys the derived object");
    std::shared_ptr<vstd::stringable> value = std::make_shared<StringValue>(destroyed);
    require(vstd::str(value) == "value", "shared stringable conversion");
}
} // namespace

int main()
{
    try
    {
        checkReplacement();
        checkStringPredicates();
        checkIntegerContract();
        checkStringableLifetime();
        std::cout << "string utility contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
