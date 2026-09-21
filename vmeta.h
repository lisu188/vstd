/*
 * MIT License
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include "vany.h"
#include <any>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
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
inline constexpr int meta_api_version = 2;
namespace detail
{
template <typename T> struct meta_value_type { using type = std::remove_cvref_t<T>; };
template <typename T> struct meta_value_type<std::reference_wrapper<T>> { using type = std::remove_cv_t<T>; };
template <typename T> using meta_value_t = typename meta_value_type<std::remove_cvref_t<T>>::type;
}
struct method_signature
{
    std::string name;
    std::vector<std::type_index> argument_types;
    method_signature() = default;
    method_signature(std::string method_name, std::vector<std::type_index> method_argument_types)
        : name(std::move(method_name)), argument_types(std::move(method_argument_types)) {}
    template <typename... ArgumentTypes> static method_signature from(const std::string& method_name)
    { return method_signature(method_name, {std::type_index(typeid(detail::meta_value_t<ArgumentTypes>))...}); }
    bool operator==(const method_signature& other) const
    { return name == other.name && argument_types == other.argument_types; }
};
struct method_signature_hash
{
    std::size_t operator()(const method_signature& signature) const
    {
        std::size_t seed = std::hash<std::string>()(signature.name);
        for (const auto& type : signature.argument_types)
        { seed ^= type.hash_code() + 0x9e3779b9 + (seed << 6) + (seed >> 2); }
        return seed;
    }
};
class method
{
  public:
    virtual ~method() = default;
    virtual std::shared_ptr<method> clone() const
    {
        throw std::logic_error("vstd::meta: this method does not support cloning");
    }
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
    virtual std::shared_ptr<property> clone() const
    {
        throw std::logic_error("vstd::meta: this property does not support cloning");
    }
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
template <typename Map> class dynamic_map : public Map
{
  public:
    dynamic_map() = default;
    dynamic_map(const dynamic_map& other)
    {
        for (const auto& [key, value] : other)
        {
            this->emplace(key, value->clone());
        }
    }
    dynamic_map(dynamic_map&&) noexcept = default;
    dynamic_map& operator=(const dynamic_map& other)
    {
        dynamic_map copy(other);
        this->swap(copy);
        return *this;
    }
    dynamic_map& operator=(dynamic_map&&) noexcept = default;
};

template <typename T> class meta_argument
{
    using value_type = std::remove_cvref_t<T>;
    std::optional<value_type> _owned;
    const value_type* _value = nullptr;
    value_type* _mutable = nullptr;

  public:
    explicit meta_argument(const std::any& value)
    {
        if constexpr (std::is_lvalue_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>)
        {
            auto reference = std::any_cast<std::reference_wrapper<value_type>>(&value);
            if (!reference)
            {
                throw std::invalid_argument("vstd::meta: mutable reference arguments require std::ref");
            }
            _mutable = &reference->get();
        }
        else if constexpr (std::same_as<value_type, std::any>)
        {
            if (auto reference = std::any_cast<std::reference_wrapper<std::any>>(&value))
            { _value = &reference->get(); }
            else if (auto reference = std::any_cast<std::reference_wrapper<const std::any>>(&value))
            { _value = &reference->get(); }
            else { _value = &value; }
        }
        else if (auto exact = std::any_cast<value_type>(&value))
        {
            _value = exact;
        }
        else if (auto reference = std::any_cast<std::reference_wrapper<value_type>>(&value))
        {
            _value = &reference->get();
        }
        else if (auto reference = std::any_cast<std::reference_wrapper<const value_type>>(&value))
        {
            _value = &reference->get();
        }
        else
        {
            _owned.emplace(vstd::any_cast<value_type>(value));
        }
    }

    decltype(auto) get()
    {
        if constexpr (std::is_lvalue_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>)
        {
            return *_mutable;
        }
        else if constexpr (std::is_lvalue_reference_v<T>)
        {
            return static_cast<const value_type&>(_owned ? *_owned : *_value);
        }
        else
        {
            return value_type(_owned ? std::move(*_owned) : *_value);
        }
    }
};
}
namespace detail
{
template <typename Actual, typename Expected>
concept MetaReturnCompatible = (std::same_as<Expected, void> && std::same_as<Actual, void>) ||
                               (!std::same_as<Expected, void> && std::convertible_to<Actual, Expected>);
template <typename Function, typename ObjectType, typename ReturnType, typename... ArgumentTypes>
concept MetaCallable = std::copy_constructible<Function> &&
                       (std::copy_constructible<std::remove_cvref_t<ArgumentTypes>> && ...) &&
                       (std::same_as<ReturnType, void> || std::copy_constructible<std::remove_cvref_t<ReturnType>>) &&
                       std::invocable<Function&, ObjectType*, ArgumentTypes...> &&
                       MetaReturnCompatible<std::invoke_result_t<Function&, ObjectType*, ArgumentTypes...>, ReturnType>;
template <typename Method, typename ObjectType, typename ReturnType, typename... ArgumentTypes>
concept MetaMemberMethod = std::is_member_function_pointer_v<std::decay_t<Method>> &&
                           MetaCallable<Method, ObjectType, ReturnType, ArgumentTypes...>;
template <typename Getter, typename ObjectType, typename PropertyType>
concept MetaGetter = std::invocable<Getter, ObjectType*> &&
                     MetaReturnCompatible<std::invoke_result_t<Getter, ObjectType*>, PropertyType>;
template <typename Setter, typename ObjectType, typename PropertyType>
concept MetaSetter = std::invocable<Setter, ObjectType*, PropertyType> &&
                     std::same_as<std::invoke_result_t<Setter, ObjectType*, PropertyType>, void>;
template <typename ObjectType, typename ReturnType = void, typename... ArgumentTypes> class method_impl : public method
{
    std::string _name;
    using result_type = std::remove_cvref_t<ReturnType>;
    std::function<result_type(ObjectType*, ArgumentTypes...)> _func;
    static void validate_arg_count(const std::vector<std::any>& args)
    {
        constexpr std::size_t expected_args = sizeof...(ArgumentTypes) + 1;
        if (args.size() != expected_args)
        { throw std::invalid_argument("vstd::meta: method argument count mismatch"); }
    }
    static std::shared_ptr<ObjectType> object_from_args(const std::vector<std::any>& args)
    {
        auto object = vstd::any_cast<std::shared_ptr<ObjectType>>(args[0]);
        if (!object) { throw std::invalid_argument("vstd::meta: method object argument is null"); }
        return object;
    }
    template <std::size_t... Indexes>
    std::any invoke_impl(const std::vector<std::any>& args, std::index_sequence<Indexes...>) const
    {
        auto object = object_from_args(args);
        std::tuple<meta_argument<ArgumentTypes>...> arguments{meta_argument<ArgumentTypes>(args[Indexes + 1])...};
        if constexpr (std::same_as<ReturnType, void>)
        {
            std::invoke(_func, object.get(), std::get<Indexes>(arguments).get()...);
            return std::any();
        }
        else
        {
            return std::any(std::invoke(_func, object.get(), std::get<Indexes>(arguments).get()...));
        }
    }
  public:
    template <typename Method>
        requires MetaMemberMethod<Method, ObjectType, ReturnType, ArgumentTypes...>
    method_impl(std::string name, Method method) : _name(std::move(name))
    {
        _func = [method](ObjectType* object, ArgumentTypes... args) mutable -> result_type
        {
            if constexpr (std::same_as<ReturnType, void>) { std::invoke(method, object, std::forward<ArgumentTypes>(args)...); }
            else { return std::invoke(method, object, std::forward<ArgumentTypes>(args)...); }
        };
    }
    template <typename Function>
        requires MetaCallable<Function, ObjectType, ReturnType, ArgumentTypes...>
    method_impl(std::string name, Function function, bool) : _name(std::move(name))
    {
        _func = [function = std::move(function)](ObjectType* object, ArgumentTypes... args) mutable -> result_type
        {
            if constexpr (std::same_as<ReturnType, void>) { std::invoke(function, object, std::forward<ArgumentTypes>(args)...); }
            else { return std::invoke(function, object, std::forward<ArgumentTypes>(args)...); }
        };
    }
    std::shared_ptr<method> clone() const override { return std::make_shared<method_impl>(*this); }
    std::string name() const override { return _name; }
    method_signature signature() const override { return method_signature(_name, argument_types()); }
    std::any invoke(const std::vector<std::any>& args) const override
    {
        validate_arg_count(args);
        return invoke_impl(args, std::index_sequence_for<ArgumentTypes...>());
    }
    std::type_index object_type() const override { return std::type_index(typeid(ObjectType)); }
    std::type_index return_type() const override { return std::type_index(typeid(ReturnType)); }
    std::vector<std::type_index> argument_types() const override { return {std::type_index(typeid(detail::meta_value_t<ArgumentTypes>))...}; }
};
template <typename ObjectType, typename PropertyType> class property_impl : public property
{
    std::string _name;
    std::function<PropertyType(ObjectType*)> _getter;
    std::function<void(ObjectType*, PropertyType)> _setter;
    static std::shared_ptr<ObjectType> object_from_any(const std::any& object)
    {
        auto typed_object = vstd::any_cast<std::shared_ptr<ObjectType>>(object);
        if (!typed_object) { throw std::invalid_argument("vstd::meta: property object argument is null"); }
        return typed_object;
    }
  public:
    template <typename Getter, typename Setter>
        requires MetaGetter<Getter, ObjectType, PropertyType> && MetaSetter<Setter, ObjectType, PropertyType>
    property_impl(std::string name, Getter getter, Setter setter) : _name(std::move(name))
    {
        _getter = [getter](ObjectType* object) -> PropertyType { return std::invoke(getter, object); };
        _setter = [setter](ObjectType* object, PropertyType value) { std::invoke(setter, object, value); };
    }
    std::string name() const override { return _name; }
    std::any get(const std::any& object) const override { return _getter(object_from_any(object).get()); }
    void set(const std::any& object, const std::any& value) override
    { _setter(object_from_any(object).get(), vstd::any_cast<PropertyType>(value)); }
    std::type_index object_type() const override { return std::type_index(typeid(ObjectType)); }
    std::type_index value_type() const override { return std::type_index(typeid(PropertyType)); }
};
template <typename ObjectType, typename PropertyType> class dynamic_property_impl : public property
{
    std::string _name;
    PropertyType _value{};
  public:
    explicit dynamic_property_impl(std::string name) : _name(std::move(name)) {}
    dynamic_property_impl(std::string name, const PropertyType& value) : _name(std::move(name)), _value(value) {}
    std::shared_ptr<property> clone() const override { return std::make_shared<dynamic_property_impl>(*this); }
    std::string name() const override { return _name; }
    std::any get(const std::any&) const override { return std::any(_value); }
    void set(const std::any&, const std::any& value) override { _value = vstd::any_cast<PropertyType>(value); }
    std::type_index object_type() const override { return std::type_index(typeid(ObjectType)); }
    std::type_index value_type() const override { return std::type_index(typeid(PropertyType)); }
};
}
class meta
{
  public:
    class empty
    {
      public: static std::shared_ptr<vstd::meta> static_meta() { return nullptr; }
    };
    using index_type = std::unordered_map<std::string, std::shared_ptr<meta>>;
    static std::shared_ptr<const index_type> index()
    {
        std::lock_guard lock(index_mutex());
        return std::make_shared<const index_type>(mutable_index());
    }
    static void register_type(const std::shared_ptr<meta>& value)
    {
        if (!value) { throw std::invalid_argument("vstd::meta: cannot register null metadata"); }
        std::lock_guard lock(index_mutex());
        auto& index = mutable_index();
        if (auto found = index.find(value->name()); found != index.end() && found->second != value)
        {
            throw std::invalid_argument("vstd::meta: duplicate type name: " + value->name());
        }
        index.emplace(value->name(), value);
    }
  private:
    static std::mutex& index_mutex() { static std::mutex mutex; return mutex; }
    static index_type& mutable_index() { static index_type index; return index; }
    template <typename ObjectType> static void validate_object(const std::shared_ptr<ObjectType>& object)
    {
        if (!object) { throw std::invalid_argument("vstd::meta: object argument is null"); }
    }
    std::string _name;
    property_map _props;
    method_map _methods;
    std::shared_ptr<meta> _super;
    void _add() {}
    template <typename... Args> void _add(std::shared_ptr<property> prop, Args... props)
    {
        if (!prop || !_props.emplace(prop->name(), prop).second)
        { throw std::invalid_argument("vstd::meta: duplicate or null property registration"); }
        _add(props...);
    };
    template <typename... Args> void _add(std::shared_ptr<method> meth, Args... props)
    {
        if (!meth || !_methods.emplace(meth->signature(), meth).second)
        { throw std::invalid_argument("vstd::meta: duplicate method signature (cv/ref overloads are not distinct)"); }
        _add(props...);
    };
    template <typename... Args> void _add(vstd::meta::empty, Args... props) { _add(props...); };
    std::shared_ptr<property> _find_static_property(const std::string& name) const
    {
        if (auto prop = _props.find(name); prop != _props.end()) { return prop->second; }
        if (_super) { return _super->_find_static_property(name); }
        return nullptr;
    }
    std::shared_ptr<method> _find_static_method(const method_signature& signature) const
    {
        if (auto method = _methods.find(signature); method != _methods.end()) { return method->second; }
        if (_super) { return _super->_find_static_method(signature); }
        return nullptr;
    }
    template <typename ObjectType>
    std::shared_ptr<property> _get_property_object(const std::shared_ptr<ObjectType>& ob, const std::string& name) const
    {
        auto prop = find_property(name, ob);
        if (!prop) { throw std::out_of_range("vstd::meta: property not found: " + name); }
        return prop;
    }
    template <typename ObjectType, typename ReturnType, typename... ArgumentTypes>
    std::shared_ptr<method> _get_method_object(const std::shared_ptr<ObjectType>& ob, const std::string& name) const
    {
        auto signature = method_signature::from<ArgumentTypes...>(name);
        auto meth = find_method(signature, ob);
        if (!meth) { throw std::out_of_range("vstd::meta: method not found: " + name); }
        if (meth->return_type() != std::type_index(typeid(ReturnType)))
        { throw std::invalid_argument("vstd::meta: method return type mismatch: " + name); }
        return meth;
    }
    template <typename ObjectType, typename ReturnType, typename... ArgumentTypes, typename Function>
        requires detail::MetaCallable<Function, ObjectType, ReturnType, ArgumentTypes...>
    void _set_method_object(const std::shared_ptr<ObjectType>& ob, const std::string& name, Function function) const
    {
        auto meth = std::make_shared<vstd::detail::method_impl<ObjectType, ReturnType, ArgumentTypes...>>(name, std::move(function), true);
        validate_object(ob);
        ob->dynamic_methods()[meth->signature()] = std::move(meth);
    }
    template <typename ObjectType, typename... ArgumentTypes>
    static std::vector<std::any> _invoke_args(const std::shared_ptr<ObjectType>& object, const ArgumentTypes&... args)
    {
        std::vector<std::any> result;
        result.reserve(sizeof...(args) + 1);
        result.emplace_back(object);
        (result.emplace_back(args), ...);
        return result;
    }
    template <typename ObjectType> property_map _collect_properties(const std::shared_ptr<ObjectType>& ob) const
    {
        validate_object(ob);
        property_map collected;
        for (const auto& [name, property] : ob->dynamic_props()) { collected.emplace(name, property); }
        _collect_static_properties(collected);
        return collected;
    }
    void _collect_static_properties(property_map& collected) const
    {
        for (const auto& [name, property] : _props) { collected.emplace(name, property); }
        if (_super) { _super->_collect_static_properties(collected); }
    }
    template <typename ObjectType> method_map _collect_methods(const std::shared_ptr<ObjectType>& ob) const
    {
        validate_object(ob);
        method_map collected;
        for (const auto& [signature, method] : ob->dynamic_methods()) { collected.emplace(signature, method); }
        _collect_static_methods(collected);
        return collected;
    }
    void _collect_static_methods(method_map& collected) const
    {
        for (const auto& [signature, method] : _methods) { collected.emplace(signature, method); }
        if (_super) { _super->_collect_static_methods(collected); }
    }
  public:
    template <typename... Args>
    meta(std::string name, std::shared_ptr<meta> super, Args... props)
        : _name(std::move(name)), _super(std::move(super)) { _add(props...); }
    std::shared_ptr<meta> super() const { return _super; }
    std::string name() const { return _name; }
    bool inherits(const std::string& clas) const { return name() == clas || (_super && _super->inherits(clas)); }
    template <typename ObjectType>
    std::shared_ptr<property> find_property(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    {
        validate_object(ob);
        if (auto prop = ob->dynamic_props().find(name); prop != ob->dynamic_props().end()) { return prop->second; }
        return _find_static_property(name);
    }
    template <typename ObjectType>
    std::shared_ptr<method> find_method(const method_signature& signature, const std::shared_ptr<ObjectType>& ob) const
    {
        validate_object(ob);
        if (auto meth = ob->dynamic_methods().find(signature); meth != ob->dynamic_methods().end()) { return meth->second; }
        return _find_static_method(signature);
    }
    template <typename ObjectType, typename... ArgumentTypes>
    std::shared_ptr<method> find_method(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    { return find_method(method_signature::from<ArgumentTypes...>(name), ob); }
    template <typename ObjectType, typename PropertyType>
    void set_dynamic_property(const std::string& prop, const std::shared_ptr<ObjectType>& t, const PropertyType& p) const
    {
        validate_object(t);
        auto& dynamic_props = t->dynamic_props();
        auto prop_it = dynamic_props.find(prop);
        if (prop_it == dynamic_props.end())
        {
            auto initialized = std::make_shared<detail::dynamic_property_impl<ObjectType, PropertyType>>(prop, p);
            dynamic_props.emplace(prop, std::move(initialized));
            return;
        }
        if (prop_it->second->value_type() != std::type_index(typeid(PropertyType)))
        { throw std::invalid_argument("vstd::meta: dynamic property type mismatch: " + prop); }
        prop_it->second->set(t, p);
    }
    template <typename ObjectType, typename PropertyType>
    void set_property(const std::string& prop, const std::shared_ptr<ObjectType>& t, const PropertyType& p) const
    { _get_property_object(t, prop)->set(t, p); }
    template <typename ReturnType, typename ObjectType, typename... ArgumentTypes>
        requires std::same_as<ReturnType, void>
    void invoke_method(const std::string& name, const std::shared_ptr<ObjectType>& t, ArgumentTypes... args) const
    { _get_method_object<ObjectType, ReturnType, ArgumentTypes...>(t, name)->invoke(_invoke_args(t, args...)); }
    template <typename ReturnType, typename ObjectType, typename... ArgumentTypes>
        requires(!std::same_as<ReturnType, void> && !std::is_reference_v<ReturnType>)
    ReturnType invoke_method(const std::string& name, const std::shared_ptr<ObjectType>& t, ArgumentTypes... args) const
    {
        return vstd::any_cast<ReturnType>(
            _get_method_object<ObjectType, ReturnType, ArgumentTypes...>(t, name)->invoke(_invoke_args(t, args...)));
    }
    template <typename ObjectType, typename PropertyType>
        requires(!std::same_as<PropertyType, std::any> && !std::is_reference_v<PropertyType>)
    PropertyType get_property(const std::string& prop, const std::shared_ptr<ObjectType>& t) const
    { return vstd::any_cast<PropertyType>(_get_property_object(t, prop)->get(t)); }
    template <typename ObjectType, typename PropertyType>
        requires std::same_as<PropertyType, std::any>
    PropertyType get_property(const std::string& prop, const std::shared_ptr<ObjectType>& t) const
    { return _get_property_object(t, prop)->get(t); }
    template <typename ObjectType, typename ReturnType, typename... ArgumentTypes, typename Function>
        requires detail::MetaCallable<Function, ObjectType, ReturnType, ArgumentTypes...>
    void set_method(const std::string& method, const std::shared_ptr<ObjectType>& t, Function function) const
    { _set_method_object<ObjectType, ReturnType, ArgumentTypes...>(t, method, std::move(function)); }
    template <typename ObjectType>
    std::type_index get_property_type(const std::shared_ptr<ObjectType>& ob, const std::string& name) const
    { auto prop = find_property(name, ob); return prop ? prop->value_type() : std::type_index(typeid(void)); }
    template <typename ObjectType>
    std::shared_ptr<std::set<std::shared_ptr<vstd::property>>> properties(const std::shared_ptr<ObjectType>& ob) const
    {
        auto props = std::make_shared<std::set<std::shared_ptr<vstd::property>>>();
        for (const auto& [name, property] : _collect_properties(ob)) { props->insert(property); }
        return props;
    }
    template <typename ObjectType, typename PropertyCallback>
    void for_properties(const std::shared_ptr<ObjectType>& ob, PropertyCallback propertyCallback) const
    {
        for (const auto& [name, property] : _collect_properties(ob)) { if (propertyCallback(property)) { return; } }
    }
    template <typename ObjectType, typename PropertyCallback>
    void for_all_properties(const std::shared_ptr<ObjectType>& ob, PropertyCallback propertyCallback) const
    { for_properties(ob, [&](auto prop) { propertyCallback(prop); return false; }); }
    template <typename ObjectType>
    bool has_property(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    { return static_cast<bool>(find_property(name, ob)); }
    template <typename ObjectType>
    std::shared_ptr<std::set<std::shared_ptr<vstd::method>>> methods(const std::shared_ptr<ObjectType>& ob) const
    {
        auto meths = std::make_shared<std::set<std::shared_ptr<vstd::method>>>();
        for (const auto& [signature, method] : _collect_methods(ob)) { meths->insert(method); }
        return meths;
    }
    template <typename ObjectType, typename MethodCallback>
    void for_methods(const std::shared_ptr<ObjectType>& ob, MethodCallback methodCallback) const
    {
        for (const auto& [signature, method] : _collect_methods(ob)) { if (methodCallback(method)) { return; } }
    }
    template <typename ObjectType, typename MethodCallback>
    void for_all_methods(const std::shared_ptr<ObjectType>& ob, MethodCallback methodCallback) const
    { for_methods(ob, [&](auto method) { methodCallback(method); return false; }); }
    template <typename ObjectType>
    bool has_method(const method_signature& signature, const std::shared_ptr<ObjectType>& ob) const
    { return static_cast<bool>(find_method(signature, ob)); }
    template <typename ObjectType, typename... ArgumentTypes>
    bool has_method(const std::string& name, const std::shared_ptr<ObjectType>& ob) const
    { return has_method(method_signature::from<ArgumentTypes...>(name), ob); }
};
namespace detail
{
template <typename Derived, typename Super> void register_meta_base_conversion()
{
    if constexpr (!std::same_as<Super, vstd::meta::empty>)
    {
        using DerivedPtr = std::shared_ptr<Derived>;
        using SuperPtr = std::shared_ptr<Super>;
        vstd::register_any_type<SuperPtr, DerivedPtr>();
        vstd::register_any_type<DerivedPtr, SuperPtr>();
    }
}
}
}
#define V_META_NAMED(CLASS, SUPER, META_NAME, ...) \
  private: \
    friend class vstd::meta; \
    vstd::detail::dynamic_map<vstd::property_map> _dynamic_props; \
    virtual vstd::property_map& dynamic_props() { return _dynamic_props; } \
    virtual const vstd::property_map& dynamic_props() const { return _dynamic_props; } \
    vstd::detail::dynamic_map<vstd::method_map> _dynamic_methods; \
    virtual vstd::method_map& dynamic_methods() { return _dynamic_methods; } \
    virtual const vstd::method_map& dynamic_methods() const { return _dynamic_methods; } \
  public: \
    static std::shared_ptr<vstd::meta> static_meta() \
    { \
        static std::shared_ptr<vstd::meta> _static_meta = [] \
        { \
            auto result = std::make_shared<vstd::meta>(META_NAME, SUPER::static_meta() __VA_OPT__(,) __VA_ARGS__); \
            vstd::detail::register_meta_base_conversion<CLASS, SUPER>(); \
            vstd::meta::register_type(result); \
            return result; \
        }(); \
        return _static_meta; \
    } \
    virtual std::shared_ptr<vstd::meta> meta() { return static_meta(); } \
    virtual std::shared_ptr<const vstd::meta> meta() const { return static_meta(); } \
  private:
#define V_META(CLASS, SUPER, ...) V_META_NAMED(CLASS, SUPER, V_STRING(CLASS) __VA_OPT__(,) __VA_ARGS__)

namespace vstd::detail
{
template <typename Object, typename Result = void, typename... Args> struct member_selector
{
    using mutable_method = Result (Object::*)(Args...);
    using const_method = Result (Object::*)(Args...) const;
    static constexpr mutable_method select(mutable_method method) { return method; }
    template <typename = void> static constexpr const_method select(const_method method) { return method; }
};
}
#define V_METHOD(CLASS, NAME, ...) \
    std::make_shared<vstd::detail::method_impl<CLASS __VA_OPT__(,) __VA_ARGS__>>( \
        V_STRING(NAME), vstd::detail::member_selector<CLASS __VA_OPT__(,) __VA_ARGS__>::select(&CLASS::NAME))
#define V_PROPERTY(CLASS, TYPE, NAME, GETTER, SETTER) \
    std::make_shared<vstd::detail::property_impl<CLASS, TYPE>>( \
        V_STRING(NAME), vstd::detail::member_selector<CLASS, TYPE>::select(&CLASS::GETTER), \
        vstd::detail::member_selector<CLASS, void, TYPE>::select(&CLASS::SETTER)), \
        V_METHOD(CLASS, GETTER, TYPE), V_METHOD(CLASS, SETTER, void, TYPE)
