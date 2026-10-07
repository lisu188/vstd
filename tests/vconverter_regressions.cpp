// Copyright (c) 2026 Andrzej Lis. MIT License.
#include "vconverter.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>

namespace bp = boost::python;

struct Item
{
    static inline int destroyed = 0;
    ~Item()
    {
        ++destroyed;
    }
};

BOOST_PYTHON_MODULE(vstd_regression)
{
    bp::class_<Item, std::shared_ptr<Item>, boost::noncopyable>("Item", bp::no_init);
}

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void checkConversions()
{
    bp::object module = bp::import("vstd_regression");
    bp::object main_module = bp::import("__main__");
    bp::dict scope = bp::extract<bp::dict>(main_module.attr("__dict__"));
    vstd::function_converter<int> integer_registration;
    vstd::function_converter<std::shared_ptr<Item>> pointer_registration;
    vstd::function_converter<void> void_registration;
    vstd::function_converter<bool> bool_registration;

    bp::object number = bp::eval("1000", scope, scope);
    scope["number"] = number;
    bp::object callback = bp::eval("lambda: number", scope, scope);
    const auto callback_refs = Py_REFCNT(callback.ptr());
    const auto result_refs = Py_REFCNT(number.ptr());
    {
        auto converted = bp::extract<std::function<int()>>(callback)();
        for (int i = 0; i != 5; ++i)
        {
            require(converted() == 1000, "integer conversion");
        }
    }
    require(Py_REFCNT(callback.ptr()) == callback_refs, "converted callable reference must be released");
    require(Py_REFCNT(number.ptr()) == result_refs, "every callback result reference must be released");

    bp::object void_callback = bp::eval("lambda: None", scope, scope);
    const auto void_refs = Py_REFCNT(void_callback.ptr());
    {
        auto converted = bp::extract<std::function<void()>>(void_callback)();
        converted();
    }
    require(Py_REFCNT(void_callback.ptr()) == void_refs, "void callable reference must be released");
    bp::object bool_callback = bp::eval("lambda: True", scope, scope);
    const auto bool_refs = Py_REFCNT(bool_callback.ptr());
    {
        auto converted = bp::extract<std::function<bool()>>(bool_callback)();
        require(converted(), "boolean conversion");
    }
    require(Py_REFCNT(bool_callback.ptr()) == bool_refs, "boolean callable reference must be released");

    auto original = std::make_shared<Item>();
    bp::object wrapper(original);
    scope["item"] = wrapper;
    bp::object pointer_callback = bp::eval("lambda: item", scope, scope);
    const auto pointer_callback_refs = Py_REFCNT(pointer_callback.ptr());
    std::weak_ptr<Item> original_lifetime = original;
    std::shared_ptr<Item> result;
    {
        auto converted = bp::extract<std::function<std::shared_ptr<Item>()>>(pointer_callback)();
        result = converted();
    }
    require(result.get() == original.get(), "shared result addresses the original object");
    require(Py_REFCNT(pointer_callback.ptr()) == pointer_callback_refs, "shared callable reference released");
    scope.attr("pop")("item");
    wrapper = bp::object();
    original.reset();
    if (Item::destroyed != 0 || original_lifetime.expired())
    {
        std::fputs("shared callback result must retain the original object\n", stderr);
        std::fflush(stderr);
        // Do not invoke the invalid second deleter of a broken raw-pointer conversion.
        std::_Exit(1);
    }
    result.reset();
    require(Item::destroyed == 1 && original_lifetime.expired(),
            "shared callback object is destroyed exactly once after its final owner");

    scope["item"] = bp::object();
    auto converted_null = bp::extract<std::function<std::shared_ptr<Item>()>>(pointer_callback)();
    require(!converted_null(), "None converts to an empty shared pointer");
    scope.attr("pop")("item");
    scope.attr("pop")("number");
}
} // namespace

int main()
{
    if (PyImport_AppendInittab("vstd_regression", &PyInit_vstd_regression) == -1)
    {
        return 2;
    }
    Py_Initialize();
    int status = 0;
    try
    {
        checkConversions();
        std::puts("Python callback reference counts and shared ownership regressions passed");
    }
    catch (const bp::error_already_set&)
    {
        PyErr_Print();
        status = 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        status = 1;
    }
    Py_Finalize();
    return status;
}
