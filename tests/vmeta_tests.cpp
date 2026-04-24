#include "vstd.h"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace
{
template <typename ExceptionType, typename Function> void expect_throw(Function function)
{
    bool thrown = false;
    try
    {
        function();
    }
    catch (const ExceptionType&)
    {
        thrown = true;
    }
    assert(thrown);
}

class MetaTestBase
{
    V_META(MetaTestBase, vstd::meta::empty, V_PROPERTY(MetaTestBase, std::string, name, getName, setName),
           V_METHOD(MetaTestBase, baseNumber, int), V_METHOD(MetaTestBase, add, int, int, int),
           V_METHOD(MetaTestBase, mark, void, int))

  public:
    std::string getName()
    {
        return name;
    }

    void setName(std::string value)
    {
        name = std::move(value);
    }

    int baseNumber()
    {
        return 10;
    }

    int add(int left, int right)
    {
        return left + right;
    }

    void mark(int value)
    {
        last_mark = value;
    }

    int lastMark() const
    {
        return last_mark;
    }

  private:
    std::string name;
    int last_mark = 0;
};

class MetaTestDerived : public MetaTestBase
{
    V_META(MetaTestDerived, MetaTestBase, V_PROPERTY(MetaTestDerived, int, level, getLevel, setLevel),
           V_METHOD(MetaTestDerived, baseNumber, int), V_METHOD(MetaTestDerived, add, int, int),
           V_METHOD(MetaTestDerived, scale, int, int), V_METHOD(MetaTestDerived, scale, std::string, std::string))

  public:
    int getLevel()
    {
        return level;
    }

    void setLevel(int value)
    {
        level = value;
    }

    int baseNumber()
    {
        return 20;
    }

    int add(int value)
    {
        return value * 2;
    }

    int scale(int value)
    {
        return value * 10;
    }

    std::string scale(std::string value)
    {
        return value + value;
    }

  private:
    int level = 0;
};
} // namespace

int main()
{
    auto object = std::make_shared<MetaTestDerived>();
    auto meta = MetaTestDerived::static_meta();

    meta->set_property<MetaTestDerived, std::string>("name", object, "base-name");
    assert((meta->get_property<MetaTestDerived, std::string>("name", object) == "base-name"));

    meta->set_property<MetaTestDerived, int>("level", object, 7);
    assert((meta->get_property<MetaTestDerived, int>("level", object) == 7));

    expect_throw<std::out_of_range>([&]() { meta->get_property<MetaTestDerived, int>("missing", object); });
    assert(!meta->has_property("dynamic_tag", object));
    meta->set_dynamic_property<MetaTestDerived, std::string>("dynamic_tag", object, "dynamic");
    assert(meta->has_property("dynamic_tag", object));
    assert((meta->get_property<MetaTestDerived, std::string>("dynamic_tag", object) == "dynamic"));

    assert(meta->invoke_method<int>("baseNumber", object) == 20);
    assert(meta->invoke_method<int>("add", object, 2, 3) == 5);
    assert(meta->invoke_method<int>("add", object, 4) == 8);
    assert(meta->invoke_method<int>("scale", object, 3) == 30);
    assert(meta->invoke_method<std::string>("scale", object, std::string("x")) == "xx");

    meta->invoke_method<void>("mark", object, 9);
    assert(object->lastMark() == 9);

    auto inherited_add = meta->find_method<MetaTestDerived, int, int>("add", object);
    assert(inherited_add);
    assert(inherited_add->argument_types() ==
           std::vector<std::type_index>({std::type_index(typeid(int)), std::type_index(typeid(int))}));

    expect_throw<std::out_of_range>([&]() { meta->invoke_method<int>("missing", object); });
    expect_throw<std::invalid_argument>([&]() { meta->invoke_method<std::string>("baseNumber", object); });

    meta->set_method<MetaTestDerived, int>("baseNumber", object, [](MetaTestDerived*) { return 99; });
    assert(meta->invoke_method<int>("baseNumber", object) == 99);

    int base_number_count = 0;
    int scale_count = 0;
    meta->for_all_methods(object,
                          [&](const std::shared_ptr<vstd::method>& method)
                          {
                              if (method->signature() == vstd::method_signature::from<>("baseNumber"))
                              {
                                  ++base_number_count;
                              }
                              if (method->name() == "scale")
                              {
                                  ++scale_count;
                              }
                          });
    assert(base_number_count == 1);
    assert(scale_count == 2);

    return 0;
}
