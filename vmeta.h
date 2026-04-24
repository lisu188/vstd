/*
 * MIT License
 *
 * Copyright (c) 2021 Andrzej Lis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once

#include "vany.h"
#include <any>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#define V_VOID std::type_index(typeid(void))

#define V_STRING(X) #X

namespace vstd
{
struct method_signature
{
    std::string name;
    std::vector<std::type_index> argument_types;

    method_signature() = default;

    method_signature(std::string method_name, std::vector<std::type_index> method_argument_types)
        : name(std::move(method_name)), argument_types(std::move(method_argument_types))
    {
    }

    template <typename... ArgumentTypes> static method_signature from(const std::string& method_name)
    {
        return method_signature(method_name, {std::type_index(typeid(ArgumentTypes))...});
    }

    bool operator==(const method_signature& other) const
    {
        return name == other.name && argument_types == other.argument_types;
    }
};

struct method_signature_hash
{
    std::size_t operator()(const method_signature& signature) const
    {
        std::size_t seed = std::hash<std::string>()(signature.name);
        for (const auto& type : signature.argument_types)
        {
            seed ^= type.hash_code() + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class method
{
  public:
    virtual ~method() = default;

    virtual std::string name() const = 0;

    virtual method_signature signature() const = 0;

    virtual std::any invoke(const std::vector<std::any>& args) const = 0;

    virtual std::type_index object_type() const = 0;

    virtual std::type_index return_type() const = 0;

    virtual std::vector<std::type_index> argument_types() const = 0;
};

class property
{
  public:
    virtual ~property() = default;

    virtual std::string name() const = 0;

    virtual std::any get(const std::any& object) const = 0;

    virtual void set(const std::any& object, const std::any& value) = 0;

    virtual std::type_index object_type() const = 0;

    virtual std::type_index value_type() const = 0;
};

using property_map = std::unordered_map<std::string, std::shared_ptr<property>>;
using method_map = std::unordered_map<method_signature, std::shared_ptr<method>, method_signature_hash>;

namespace detail
{
template <typename Actual, typename Expected>
concept MetaReturnCompatible = (std::same_as<Expected, void> && std::same_as<Actual, void>) ||
                               (!std::same_as<Expected, void> && std::convertible_to<Actual, Expected>);

template <typename Function, typename ObjectType, typename ReturnType, typename... ArgumentTypes>
concept MetaCallable = std::invocable<Function, ObjectType*, ArgumentTypes...> &&
                       MetaReturnCompatible<std::invoke_result_t<Function, ObjectType*, ArgumentTypes...>, ReturnType>;

template <typename Method, typename ObjectType, typename ReturnType, typename... ArgumentTypes>
concept MetaMemberMethod = std::is_member_function_pointer_v<std::decay_t<Method>> &&
                           MetaCallable<Method, ObjectType, ReturnType, ArgumentTypes...>;

template <typename Getter, typename ObjectType, typename PropertyType>
concept MetaGetter = std::invocable<Getter, ObjectType*> &&
                     MetaReturnCompatible<std::invoke_result_t<Getter, ObjectType*>, PropertyType>;

template <typename Setter, typename ObjectType, typename PropertyType>
concept MetaSetter = std::invocable<Setter, ObjectType*, PropertyType> &&
                     std::same_as<std::invoke_result_t<Setter, ObjectType*, PropertyType>, void>;

template <typename ObjectType, typename ReturnType, typename... ArgumentTypes> class method_impl : public method
{
    std::string _name;
    std::function<ReturnType(ObjectType*, ArgumentTypes...)> _func;

    static void validate_arg_count(const std::vector<std::any>& args)
    {
        constexpr std::size_t expected_args = sizeof...(ArgumentTypes) + 1;
        if (args.size() != expected_args)
        {
            throw std::invalid_argument("vstd::meta: method argument count mismatch");
        }
    }

    static ObjectType* object_from_args(const std::vector<std::any>& args)
    {
        auto object = vstd::any_cast<std::shared_ptr<ObjectType>>(args[0]);
        if (!object)
        {
            throw std::invalid_argument("vstd::meta: method object argument is null");
        }
        return object.get();
    }

    template <std::size_t... Indexes>
    std::any invoke_impl(const std::vector<std::any>& args, std::index_sequence<Indexes...>) const
    {
        ObjectType* object = object_from_args(args);
        if constexpr (std::same_as<ReturnType, void>)
        {
            std::invoke(_func, object, vstd::any_cast<ArgumentTypes>(args[Indexes + 1])...);
            return std::any();
        }
        else
        {
            return std::any(std::invoke(_func, object, vstd::any_cast<ArgumentTypes>(args[Indexes + 1])...));
        }
    }

  public:
    template <typename Method>
        requires MetaMemberMethod<Method, ObjectType, ReturnType, ArgumentTypes...>
    method_impl(std::string name, Method method) : _name(std::move(name))
    {
        _func = [method](ObjectType* object, ArgumentTypes... args) -> ReturnType
        {
            if constexpr (std::same_as<ReturnType, void>)
            {
                std::invoke(method, object, args...);
            }
            else
            {
                return std::invoke(method, object, args...);
            }
        };
    }

    template <typename Function>
        requires(!std::is_member_function_pointer_v<std::decay_t<Function>> &&
                 MetaCallable<Function, ObjectType, ReturnType, ArgumentTypes...>)
    method_impl(std::string name, Function function, bool) : _name(std::move(name))
    {
        _func = [function = std::move(function)](ObjectType* object, ArgumentTypes... args) -> ReturnType
        {
            if constexpr (std::same_as<ReturnType, void>)
            {
                std::invoke(function, object, args...);
            }
            else
            {
                return std::invoke(function, object, args...);
            }
        };
    }

    std::string name() const override
    {
        return _name;
    }

    method_signature signature() const override
    {
        return method_signature(_name, argument_types());
    }

    std::any invoke(const std::vector<std::any>& args) const override
    {
        validate_arg_count(args);
        return invoke_impl(args, std::index_sequence_for<ArgumentTypes...>());
    }

    std::type_index object_type() const override
    {
        return std::type_index(typeid(ObjectType));
    }

    std::type_index return_type() const override
    {
        return std::type_index(typeid(ReturnType));
    }

    std::vector<std::type_index> argument_types() const override
    {
        return {std::type_index(typeid(ArgumentTypes))...};
    }
};

template <typename ObjectType, typename PropertyType> class property_impl : public property
{
    std::string _name;
    std::function<PropertyType(ObjectType*)> _getter;
    std::function<void(ObjectType*, PropertyType)> _setter;

    static ObjectType* object_from_any(const std::any& object)
    {
        auto typed_object = vstd::any_cast<std::shared_ptr<ObjectType>>(object);
        if (!typed_object)
        {
            throw std::invalid_argument("vstd::meta: property object argument is null");
        }
        return typed_object.get();
    }

  public:
    template <typename Getter, typename Setter>
        requires MetaGetter<Getter, ObjectType, PropertyType> && MetaSetter<Setter, ObjectType, PropertyType>
    property_impl(std::string name, Getter getter, Setter setter) : _name(std::move(name))
    {
        _getter = [getter](ObjectType* object) -> PropertyType { return std::invoke(getter, object); };
        _setter = [setter](ObjectType* object, PropertyType value) { std::invoke(setter, object, value); };
    }

    std::string name() const override
    {
        return _name;
    }

    std::any get(const std::any& object) const override
    {
        return _getter(object_from_any(object));
    }

    void set(const std::any& object, const std::any& value) override
    {
        _setter(object_from_any(object), vstd::any_cast<PropertyType>(value));
    }

    std::type_index object_type() const override
    {
        return std::type_index(typeid(ObjectType));
    }

    std::type_index value_type() const override
    {
        return std::type_index(typeid(PropertyType));
    }
};

template <typename ObjectType, typename PropertyType> class dynamic_property_impl : public property
{
    std::string _name;
    PropertyType _value;

  public:
    explicit dynamic_property_impl(std::string name) : _name(std::move(name)) {}

    std::string name() const override
    {
        return _name;
    }

    std::any get(const std::any&) const override
    {
        return std::any(_value);
    }

    void set(const std::any&, const std::any& value) override
    {
        _value = vstd::any_cast<PropertyType>(value);
    }

    std::type_index object_type() const override
    {
        return std::type_index(typeid(ObjectType));
    }

    std::type_index value_type() const override
    {
        return std::type_index(typeid(PropertyType));
    }
};
} // namespace detail

class meta
{
  public:
    class empty
    {
      public:
        static std::shared_ptr<vstd::meta> static_meta()
        {
            return nullptr;
        }
    };

    static std::unordered_map<std::string, std::shared_ptr<meta>>* index()
    {
        static std::unordered_map<std::string, std::shared_ptr<meta>> _index;
        return &_index;
    };

  private:
    std::string _name;
    property_map _props;
    method_map _methods;
    std::shared_ptr<meta> _super;

    void _add() {}

    template <typename... Args> void _add(std::shared_ptr<property> prop, Args... props)
    {
        _props[prop->name()] = std::move(prop);
        _add(props...);
    };

    template <typename... Args> void _add(std::shared_ptr<method> meth, Args... props)
    {
        _methods[meth->signature()] = std::move(meth);
        _add(props...);
    };

    template <typename... Args> void _add(vstd::meta::empty, Args... props)
    {
        _add(props...);
    };

    std::shared_ptr<property> _find_static_property(const std::string& name) const
    {
        if (auto prop = _props.find(name); prop != _props.end())
        {
            return prop->second;
        }
        if (_super)
        {
            return _super->_find_static_property(name);
        }
        return nullptr;
    }

    std::shared_ptr<method> _find_static_method(const method_signature& signature) const
    {
        if (auto method = _methods.find(signature); method != _methods.end())
        {
            return method->second;
        }
        if (_super)
        {
            return _super->_find_static_method(signature);
        }
        return nullptr;
    }

    template <typename ObjectType>
    std::shared_ptr<property> _get_property_object(const std::shared_ptr<ObjectType>& ob, const std::string& name) const
    {
        auto prop = find_property(name, ob);
        if (!prop)
        {
            throw std::out_of_range("vstd::meta: property not found: " + name);
        }
        return prop;
    }

    template <typename ObjectType, typename ReturnType, typename... ArgumentTypes>
    std::shared_ptr<method> _get_method_object(const std::shared_ptr<ObjectType>& ob, const std::string& name) const
    {
        auto signature = method_signature::from<ArgumentTypes...>(name);
        auto meth = find_method(signature, ob);
        if (!meth)
        {
            throw std::out_of_range("vstd::meta: method not found: " + name);
        }
        if (meth->return_type() != std::type_index(typeid(ReturnType)))
        {
            throw std::invalid_argument("vstd::meta: method return type mismatch: " + name);
        }
        return meth;
    }

    template <typename ObjectType, typename ReturnType, typename... ArgumentTypes, typename Function>
        requires detail::MetaCallable<Function, ObjectType, ReturnType, ArgumentTypes...>
    void _set_method_object(const std::shared_ptr<ObjectType>& ob, const std::string& name, Function function) const
    {
        auto meth =
            std::make_shared<vstd::detail::method_impl<ObjectType, ReturnType, ArgumentTypes...>>(name, function, true);
        ob->dynamic_methods()[meth->signature()] = std::move(meth);
    }

    template <typename ObjectType, typename... ArgumentTypes>
    static std::vector<std::any> _invoke_args(const std::shared_ptr<ObjectType>& object, ArgumentTypes... args)
    {
        return {std::any(object), std::any(args)...};
    }

    template <typename ObjectType> property_map _collect_properties(const std::shared_ptr<ObjectType>& ob) const
    {
        property_map collected;
        for (const auto& [name, property] : ob->dynamic_props())
        {
            collected.emplace(name, property);
        }
        _collect_static_properties(collected);
        return collected;
    }

    void _collect_static_properties(property_map& collected) const
    {
        for (const auto& [name, property] : _props)
        {
            collected.emplace(name, property);
        }
        if (_super)
        {
            _super->_collect_static_properties(collected);
        }
    }

    template <typename ObjectType> method_map _collect_methods(const std::shared_ptr<ObjectType>& ob) const
    {
        method_map collected;
        for (const auto& [signature, method] : ob->dynamic_methods())
        {
            collected.emplace(signature, method);
        }
        _collect_static_methods(collected);
        return collected;
    }

    void _collect_static_methods(method_map& collected) const
    {
        for (const auto& [signature, method] : _methods)
        {
            collected.emplace(signature, method);
        }
        if (_super)
        {
            _super->_collect_static_methods(collected);
        }
    }

  public:
    template <typename... Args>
    meta(std::string name, std::shared_ptr<meta> super, Args... props)
        : _name(std::move(name)), _super(std::move(super))
    {
        _add(props...);
    }

    std::shared_ptr<meta> super() const
    {
        return _super;
    }

    std::string name() const
    {
        return _name;
    }

    bool inherits(const std::string& clas) const
    {
        return name() == clas || (_super && _super->inherits(clas));
    }

    template <typename ObjectType>
    std::shared_ptr<property> find_property(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    {
        if (auto prop = ob->dynamic_props().find(name); prop != ob->dynamic_props().end())
        {
            return prop->second;
        }
        return _find_static_property(name);
    }

    template <typename ObjectType>
    std::shared_ptr<method> find_method(const method_signature& signature, const std::shared_ptr<ObjectType>& ob) const
    {
        if (auto meth = ob->dynamic_methods().find(signature); meth != ob->dynamic_methods().end())
        {
            return meth->second;
        }
        return _find_static_method(signature);
    }

    template <typename ObjectType, typename... ArgumentTypes>
    std::shared_ptr<method> find_method(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    {
        return find_method(method_signature::from<ArgumentTypes...>(name), ob);
    }

    template <typename ObjectType, typename PropertyType>
    void set_dynamic_property(const std::string& prop, const std::shared_ptr<ObjectType>& t,
                              const PropertyType& p) const
    {
        auto& dynamic_props = t->dynamic_props();
        auto prop_it = dynamic_props.find(prop);
        if (prop_it == dynamic_props.end())
        {
            prop_it =
                dynamic_props
                    .emplace(prop, std::make_shared<detail::dynamic_property_impl<ObjectType, PropertyType>>(prop))
                    .first;
        }
        if (prop_it->second->value_type() != std::type_index(typeid(PropertyType)))
        {
            throw std::invalid_argument("vstd::meta: dynamic property type mismatch: " + prop);
        }
        prop_it->second->set(t, p);
    }

    template <typename ObjectType, typename PropertyType>
    void set_property(const std::string& prop, const std::shared_ptr<ObjectType>& t, const PropertyType& p) const
    {
        _get_property_object(t, prop)->set(t, p);
    }

    template <typename ReturnType, typename ObjectType, typename... ArgumentTypes>
        requires std::same_as<ReturnType, void>
    void invoke_method(const std::string& name, const std::shared_ptr<ObjectType>& t, ArgumentTypes... args) const
    {
        _get_method_object<ObjectType, ReturnType, ArgumentTypes...>(t, name)->invoke(_invoke_args(t, args...));
    }

    template <typename ReturnType, typename ObjectType, typename... ArgumentTypes>
        requires(!std::same_as<ReturnType, void>)
    ReturnType invoke_method(const std::string& name, const std::shared_ptr<ObjectType>& t, ArgumentTypes... args) const
    {
        return vstd::any_cast<ReturnType>(
            _get_method_object<ObjectType, ReturnType, ArgumentTypes...>(t, name)->invoke(_invoke_args(t, args...)));
    }

    template <typename ObjectType, typename PropertyType>
        requires(!std::same_as<PropertyType, std::any>)
    PropertyType get_property(const std::string& prop, const std::shared_ptr<ObjectType>& t) const
    {
        return vstd::any_cast<PropertyType>(_get_property_object(t, prop)->get(t));
    }

    template <typename ObjectType, typename PropertyType>
        requires std::same_as<PropertyType, std::any>
    PropertyType get_property(const std::string& prop, const std::shared_ptr<ObjectType>& t) const
    {
        return _get_property_object(t, prop)->get(t);
    }

    template <typename ObjectType, typename ReturnType, typename... ArgumentTypes, typename Function>
        requires detail::MetaCallable<Function, ObjectType, ReturnType, ArgumentTypes...>
    void set_method(const std::string& method, const std::shared_ptr<ObjectType>& t, Function function) const
    {
        _set_method_object<ObjectType, ReturnType, ArgumentTypes...>(t, method, function);
    }

    template <typename ObjectType>
    std::type_index get_property_type(const std::shared_ptr<ObjectType>& ob, const std::string& name) const
    {
        auto prop = find_property(name, ob);
        return prop ? prop->value_type() : std::type_index(typeid(void));
    }

    template <typename ObjectType>
    std::shared_ptr<std::set<std::shared_ptr<vstd::property>>> properties(const std::shared_ptr<ObjectType>& ob) const
    {
        auto props = std::make_shared<std::set<std::shared_ptr<vstd::property>>>();
        for (const auto& [name, property] : _collect_properties(ob))
        {
            props->insert(property);
        }
        return props;
    }

    template <typename ObjectType, typename PropertyCallback>
    void for_properties(const std::shared_ptr<ObjectType>& ob, PropertyCallback propertyCallback) const
    {
        for (const auto& [name, property] : _collect_properties(ob))
        {
            if (propertyCallback(property))
            {
                return;
            }
        }
    }

    template <typename ObjectType, typename PropertyCallback>
    void for_all_properties(const std::shared_ptr<ObjectType>& ob, PropertyCallback propertyCallback) const
    {
        for_properties(ob,
                       [&](auto prop)
                       {
                           propertyCallback(prop);
                           return false;
                       });
    }

    template <typename ObjectType>
    bool has_property(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    {
        return static_cast<bool>(find_property(name, ob));
    }

    template <typename ObjectType>
    std::shared_ptr<std::set<std::shared_ptr<vstd::method>>> methods(const std::shared_ptr<ObjectType>& ob) const
    {
        auto meths = std::make_shared<std::set<std::shared_ptr<vstd::method>>>();
        for (const auto& [signature, method] : _collect_methods(ob))
        {
            meths->insert(method);
        }
        return meths;
    }

    template <typename ObjectType, typename MethodCallback>
    void for_methods(const std::shared_ptr<ObjectType>& ob, MethodCallback methodCallback) const
    {
        for (const auto& [signature, method] : _collect_methods(ob))
        {
            if (methodCallback(method))
            {
                return;
            }
        }
    }

    template <typename ObjectType, typename MethodCallback>
    void for_all_methods(const std::shared_ptr<ObjectType>& ob, MethodCallback methodCallback) const
    {
        for_methods(ob,
                    [&](auto method)
                    {
                        methodCallback(method);
                        return false;
                    });
    }

    template <typename ObjectType>
    bool has_method(const method_signature& signature, const std::shared_ptr<ObjectType>& ob) const
    {
        return static_cast<bool>(find_method(signature, ob));
    }

    template <typename ObjectType, typename... ArgumentTypes>
    bool has_method(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    {
        return has_method(method_signature::from<ArgumentTypes...>(name), ob);
    }
};

namespace detail
{
template <typename Derived, typename Super> void register_meta_base_conversion()
{
    if constexpr (!std::same_as<Super, vstd::meta::empty>)
    {
        using DerivedPtr = std::shared_ptr<Derived>;
        using SuperPtr = std::shared_ptr<Super>;

        auto& any_registry = vstd::detail::registry<>();
        std::vector<std::pair<std::type_index, std::function<std::any(std::any)>>> super_conversions;
        for (const auto& [key, converter] : any_registry)
        {
            if (key.second == std::type_index(typeid(SuperPtr)))
            {
                super_conversions.emplace_back(key.first, converter);
            }
        }

        vstd::register_any_type<SuperPtr, DerivedPtr>();
        for (const auto& [target, converter] : super_conversions)
        {
            any_registry[std::make_pair(target, std::type_index(typeid(DerivedPtr)))] = [converter](std::any value)
            { return converter(std::any(vstd::cast<SuperPtr>(std::any_cast<DerivedPtr>(value)))); };
        }
    }
}
} // namespace detail
} // namespace vstd

#define V_META(CLASS, SUPER, ...)                                                                                      \
  private:                                                                                                             \
    friend class vstd::meta;                                                                                           \
    vstd::property_map _dynamic_props;                                                                                 \
    virtual vstd::property_map& dynamic_props()                                                                        \
    {                                                                                                                  \
        return _dynamic_props;                                                                                         \
    }                                                                                                                  \
    virtual const vstd::property_map& dynamic_props() const                                                            \
    {                                                                                                                  \
        return _dynamic_props;                                                                                         \
    }                                                                                                                  \
    vstd::method_map _dynamic_methods;                                                                                 \
    virtual vstd::method_map& dynamic_methods()                                                                        \
    {                                                                                                                  \
        return _dynamic_methods;                                                                                       \
    }                                                                                                                  \
    virtual const vstd::method_map& dynamic_methods() const                                                            \
    {                                                                                                                  \
        return _dynamic_methods;                                                                                       \
    }                                                                                                                  \
                                                                                                                       \
  public:                                                                                                              \
    static std::shared_ptr<vstd::meta> static_meta()                                                                   \
    {                                                                                                                  \
        static std::shared_ptr<vstd::meta> _static_meta =                                                              \
            std::make_shared<vstd::meta>(V_STRING(CLASS), SUPER::static_meta(), __VA_ARGS__);                          \
        static const bool _registered_base_conversion = []()                                                           \
        {                                                                                                              \
            vstd::detail::register_meta_base_conversion<CLASS, SUPER>();                                               \
            return true;                                                                                               \
        }();                                                                                                           \
        (void)_registered_base_conversion;                                                                             \
        auto meta_index = vstd::meta::index();                                                                         \
        if (meta_index->find(_static_meta->name()) == meta_index->end())                                               \
        {                                                                                                              \
            meta_index->insert(std::make_pair(_static_meta->name(), _static_meta));                                    \
        }                                                                                                              \
        return _static_meta;                                                                                           \
    }                                                                                                                  \
    virtual std::shared_ptr<vstd::meta> meta()                                                                         \
    {                                                                                                                  \
        return static_meta();                                                                                          \
    }                                                                                                                  \
    virtual std::shared_ptr<const vstd::meta> meta() const                                                             \
    {                                                                                                                  \
        return static_meta();                                                                                          \
    }                                                                                                                  \
                                                                                                                       \
  private:

#define VSTD_META_NARG(...) VSTD_META_NARG_I(__VA_ARGS__, VSTD_META_RSEQ_N())
#define VSTD_META_NARG_I(...) VSTD_META_ARG_N(__VA_ARGS__)
#define VSTD_META_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,     \
                        _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
                        _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, \
                        _59, _60, _61, _62, _63, N, ...)                                                               \
    N
#define VSTD_META_RSEQ_N()                                                                                             \
    63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36,    \
        35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8,  \
        7, 6, 5, 4, 3, 2, 1, 0

#define VSTD_META_FUNC_NAME(name, n) name##n
#define VSTD_META_FUNC(name, n) VSTD_META_FUNC_NAME(name, n)
#define VSTD_META_DISPATCH(func, ...) VSTD_META_FUNC(func, VSTD_META_NARG(__VA_ARGS__))(__VA_ARGS__)

#define V_METHOD(...) VSTD_META_DISPATCH(V_METHOD, __VA_ARGS__)

#define V_PROPERTY(CLASS, TYPE, NAME, GETTER, SETTER)                                                                  \
    std::make_shared<vstd::detail::property_impl<CLASS, TYPE>>(V_STRING(NAME),                                         \
                                                               static_cast<TYPE (CLASS::*)()>(&CLASS::GETTER),         \
                                                               static_cast<void (CLASS::*)(TYPE)>(&CLASS::SETTER)),    \
        V_METHOD(CLASS, GETTER, TYPE), V_METHOD(CLASS, SETTER, void, TYPE)

#define V_METHOD2(CLASS, NAME)                                                                                         \
    std::make_shared<vstd::detail::method_impl<CLASS, void>>(V_STRING(NAME),                                           \
                                                             static_cast<void (CLASS::*)()>(&CLASS::NAME))
#define V_METHOD3(CLASS, NAME, RET_TYPE)                                                                               \
    std::make_shared<vstd::detail::method_impl<CLASS, RET_TYPE>>(V_STRING(NAME),                                       \
                                                                 static_cast<RET_TYPE (CLASS::*)()>(&CLASS::NAME))
#define V_METHOD4(CLASS, NAME, RET_TYPE, ...)                                                                          \
    std::make_shared<vstd::detail::method_impl<CLASS, RET_TYPE, __VA_ARGS__>>(                                         \
        V_STRING(NAME), static_cast<RET_TYPE (CLASS::*)(__VA_ARGS__)>(&CLASS::NAME))
#define V_METHOD5(CLASS, NAME, RET_TYPE, ...)                                                                          \
    std::make_shared<vstd::detail::method_impl<CLASS, RET_TYPE, __VA_ARGS__>>(                                         \
        V_STRING(NAME), static_cast<RET_TYPE (CLASS::*)(__VA_ARGS__)>(&CLASS::NAME))
#define V_METHOD6(CLASS, NAME, RET_TYPE, ...)                                                                          \
    std::make_shared<vstd::detail::method_impl<CLASS, RET_TYPE, __VA_ARGS__>>(                                         \
        V_STRING(NAME), static_cast<RET_TYPE (CLASS::*)(__VA_ARGS__)>(&CLASS::NAME))
