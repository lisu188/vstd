#include "vmeta.h"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace
{
void check(bool condition, const char* expression)
{
    if (!condition) { throw std::runtime_error(expression); }
}
#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__)

template <typename Exception, typename Function> void throws(Function function)
{
    try { function(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}

struct Subject
{
    V_META(Subject, vstd::meta::empty,
           V_METHOD(Subject, text, const std::string&),
           V_METHOD(Subject, length, int, const std::string&),
           V_METHOD(Subject, bump, void, int&),
           V_METHOD(Subject, take, int, std::string&&),
           V_METHOD(Subject, payload, std::any),
           V_METHOD(Subject, unpack, int, std::any),
           V_METHOD(Subject, four, int, int, int, int, int),
           V_METHOD(Subject, seven, int, int, int, int, int, int, int, int),
           V_METHOD(Subject, mark),
           V_METHOD(Subject, overloaded, int, int),
           V_METHOD(Subject, overloaded, int, std::string),
           V_PROPERTY(Subject, int, number, getNumber, setNumber))
  public:
    std::string value = std::string(256, 'a');
    int number = 0;
    const std::string& text() const { return value; }
    int length(const std::string& text) const { return static_cast<int>(text.size()); }
    void bump(int& value) { ++value; }
    int take(std::string&& value) { return static_cast<int>(value.size()); }
    std::any payload() { return 42; }
    int unpack(std::any value) { return std::any_cast<int>(value); }
    int four(int a, int b, int c, int d) { return a + b + c + d; }
    int seven(int a, int b, int c, int d, int e, int f, int g) { return a + b + c + d + e + f + g; }
    void mark() { number = 9; }
    int overloaded(int value) { return value; }
    int overloaded(std::string value) { return static_cast<int>(value.size()); }
    int getNumber() const noexcept { return number; }
    void setNumber(int value) { number = value; }
};

struct Empty { V_META(Empty, vstd::meta::empty) };
struct Base
{
    V_META(Base, vstd::meta::empty, V_METHOD(Base, id, int))
  public:
    virtual ~Base() = default;
    int id() { return 1; }
};
struct Middle : Base { V_META(Middle, Base) };
struct Derived : Middle
{
    V_META(Derived, Middle, V_METHOD(Derived, id, int))
  public:
    int id() { return 3; }
};
struct Collision
{
    V_META(Collision, vstd::meta::empty, V_METHOD(Collision, f, int, int), V_METHOD(Collision, f, int, const int&))
  public:
    int f(int value) { return value; }
    int f(const int& value) { return value; }
};
namespace alpha { struct Entry { V_META(Entry, vstd::meta::empty) }; }
namespace beta { struct Entry { V_META(Entry, vstd::meta::empty) }; }
namespace gamma { struct Entry { V_META_NAMED(Entry, vstd::meta::empty, "gamma::Entry") }; }
struct Throwing
{
    inline static bool fail = false;
    int value = 7;
    Throwing() = default;
    Throwing(const Throwing& other) : value(other.value)
    {
        if (fail) { throw std::runtime_error("copy failed"); }
    }
    Throwing& operator=(const Throwing&) = default;
};
struct NonDefault
{
    int value;
    explicit NonDefault(int value) : value(value) {}
};

template <typename R> concept CanInvokeResult = requires(vstd::meta& meta, std::shared_ptr<Subject> object)
{
    meta.template invoke_method<R>("text", object);
};
template <typename R> concept CanCastTemporary = requires { vstd::any_cast<R>(std::any(1)); };
static_assert(!CanInvokeResult<const std::string&>);
static_assert(!CanCastTemporary<const int&>);
static_assert(!CanCastTemporary<int&>);
static_assert(!vstd::detail::MetaCallable<decltype([](Subject*, std::unique_ptr<int>) {}), Subject, void, std::unique_ptr<int>>);

template <int I> struct ConcurrentRoot
{
    V_META_NAMED(ConcurrentRoot, vstd::meta::empty, "ConcurrentRoot" + std::to_string(I))
};
template <int I> struct ConcurrentChild : ConcurrentRoot<I>
{
    using Super = ConcurrentRoot<I>;
    V_META_NAMED(ConcurrentChild, Super, "ConcurrentChild" + std::to_string(I))
};

void values()
{
    auto object = std::make_shared<Subject>();
    auto meta = object->meta();
    CHECK(Empty::static_meta());
    CHECK(meta->invoke_method<int>("four", object, 1, 2, 3, 4) == 10);
    CHECK(meta->invoke_method<int>("seven", object, 1, 2, 3, 4, 5, 6, 7) == 28);
    meta->invoke_method<void>("mark", object);
    CHECK(meta->get_property<Subject, int>("number", object) == 9);
    meta->set_property("number", object, 4);
    CHECK(object->number == 4);
    CHECK(meta->invoke_method<int>("overloaded", object, 5) == 5);
    CHECK(meta->invoke_method<int>("overloaded", object, std::string("abc")) == 3);
    CHECK(std::any_cast<int>(meta->invoke_method<std::any>("payload", object)) == 42);
    CHECK(meta->invoke_method<int>("unpack", object, std::any(42)) == 42);
    meta->set_dynamic_property("any", object, std::any(42));
    CHECK(std::any_cast<int>(meta->get_property<Subject, std::any>("any", object)) == 42);
    meta->set_dynamic_property("any", object, std::any(std::string("next")));
    CHECK(std::any_cast<std::string>(meta->get_property<Subject, std::any>("any", object)) == "next");
    CHECK(meta->get_property_type(object, "any") == typeid(std::any));
    meta->set_dynamic_property("nondefault", object, NonDefault(42));
    CHECK(meta->get_property<Subject, NonDefault>("nondefault", object).value == 42);
}

void references()
{
    auto object = std::make_shared<Subject>();
    auto meta = object->meta();
    auto text = meta->invoke_method<std::string>("text", object);
    object->value.clear();
    CHECK(text.size() == 256);
    vstd::register_any_type<std::string, std::string>();
    CHECK(meta->invoke_method<int>("length", object, text) == 256);
    vstd::register_any_type<std::string, const char*>();
    auto method = meta->find_method<Subject, std::string>("length", object);
    CHECK(std::any_cast<int>(method->invoke({object, "converted"})) == 9);
    int value = 5;
    meta->invoke_method<void>("bump", object, std::ref(value));
    CHECK(value == 6);
    throws<std::invalid_argument>([&] { meta->invoke_method<void>("bump", object, value); });
    CHECK(meta->invoke_method<int>("length", object, std::cref(text)) == 256);
    CHECK(meta->invoke_method<int>("take", object, std::string("move")) == 4);
    std::any owned = std::string(256, 'b');
    CHECK(&vstd::any_cast<const std::string&>(owned) == std::any_cast<std::string>(&owned));
    throws<std::bad_any_cast>([&] { vstd::any_cast<const long&>(owned); });
}

void dynamic_state()
{
    auto original = std::make_shared<Subject>();
    auto meta = original->meta();
    meta->set_dynamic_property("health", original, 100);
    meta->set_method<Subject, int>("counter", original, [count = 0](Subject*) mutable { return ++count; });
    CHECK(meta->invoke_method<int>("counter", original) == 1);
    auto copy = std::make_shared<Subject>(*original);
    meta->set_dynamic_property("health", copy, 25);
    CHECK(meta->get_property<Subject, int>("health", original) == 100);
    CHECK(meta->get_property<Subject, int>("health", copy) == 25);
    CHECK(meta->invoke_method<int>("counter", copy) == 2);
    CHECK(meta->invoke_method<int>("counter", original) == 2);
    *copy = *original;
    meta->set_dynamic_property("health", copy, 50);
    CHECK(meta->get_property<Subject, int>("health", original) == 100);
    meta->set_method<Subject, void>("member", original, &Subject::mark);
    meta->invoke_method<void>("member", original);
    CHECK(original->number == 9);
    auto member = meta->find_method<Subject>("member", original);
    throws<std::invalid_argument>([&] { member->invoke({}); });
    throws<std::invalid_argument>([&] { member->invoke({std::shared_ptr<Subject>()}); });
    throws<std::bad_any_cast>([&] { member->invoke({42}); });
    throws<std::invalid_argument>([&] { meta->set_dynamic_property("health", original, std::string("wrong")); });
    Throwing input;
    Throwing::fail = true;
    throws<std::runtime_error>([&] { meta->set_dynamic_property("failed", original, input); });
    Throwing::fail = false;
    CHECK(!meta->has_property("failed", original));
    meta->set_dynamic_property("failed", original, input);
    CHECK(meta->get_property<Subject, Throwing>("failed", original).value == 7);
}

void objects()
{
    std::shared_ptr<Base> object = std::make_shared<Derived>();
    CHECK(object->meta()->invoke_method<int>("id", object) == 3);
    CHECK(vstd::any_cast<std::shared_ptr<Derived>>(std::any(object)));
    auto derived = std::dynamic_pointer_cast<Derived>(object);
    CHECK(vstd::any_cast<std::shared_ptr<Base>>(std::any(derived)) == object);
    std::shared_ptr<Subject> null;
    auto meta = Subject::static_meta();
    throws<std::invalid_argument>([&] { meta->invoke_method<void>("mark", null); });
    throws<std::invalid_argument>([&] { meta->has_property("number", null); });
    throws<std::invalid_argument>([&] { meta->get_property<Subject, int>("number", null); });
    throws<std::invalid_argument>([&] { meta->set_property("number", null, 1); });
    throws<std::invalid_argument>([&] { meta->set_dynamic_property("number", null, 1); });
    throws<std::invalid_argument>([&] { meta->set_method<Subject, void>("mark", null, &Subject::mark); });
    throws<std::invalid_argument>([&] { meta->methods(null); });
    throws<std::invalid_argument>([&] { meta->properties(null); });
    throws<std::invalid_argument>([] { Collision::static_meta(); });
    CHECK(alpha::Entry::static_meta());
    throws<std::invalid_argument>([] { beta::Entry::static_meta(); });
    CHECK(gamma::Entry::static_meta()->name() == "gamma::Entry");
    CHECK(vstd::meta::index()->at("Entry") == alpha::Entry::static_meta());
}

template <std::size_t... I> void concurrent(std::index_sequence<I...>)
{
    std::atomic<bool> ready = false;
    std::atomic<int> failures = 0;
    auto work = [&]<std::size_t N>()
    {
        while (!ready.load(std::memory_order_acquire)) { std::this_thread::yield(); }
        try
        {
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                auto value = std::make_shared<ConcurrentChild<N>>();
                CHECK(value->meta());
                CHECK(vstd::any_cast<std::shared_ptr<ConcurrentRoot<N>>>(std::any(value)));
                vstd::register_any_type<long, int>();
                CHECK(vstd::any_cast<long>(std::any(42)) == 42);
                CHECK(!vstd::meta::index()->empty());
            }
        }
        catch (...) { ++failures; }
    };
    std::vector<std::thread> threads;
    (threads.emplace_back([&] { work.template operator()<I>(); }), ...);
    ready.store(true, std::memory_order_release);
    for (auto& thread : threads) { thread.join(); }
    CHECK(failures == 0);
}
}

int main()
{
    try
    {
        concurrent(std::make_index_sequence<8>());
        values();
        references();
        dynamic_state();
        objects();
        std::cout << "PASS: vmeta lifetime, any, inheritance, state, constraints, names, nulls and concurrent registration\n";
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
