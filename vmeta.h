#pragma once

#include <boost/any.hpp>
#include <functional>
#include <unordered_map>
#include <memory>

#define V_VOID boost::typeindex::type_id<void>()
#define V_NONE vstd::empty_meta

#define V_STRING(X) #X

#define V_META(CLASS, SUPER, ...) \
private: \
friend class vstd::meta; \
std::unordered_map<std::string, std::shared_ptr<vstd::property>> _dynamic_props; \
public: \
static std::shared_ptr<vstd::meta> static_meta(){ \
    static std::shared_ptr<vstd::meta> _meta=std::make_shared<vstd::meta>(V_STRING(CLASS),SUPER::static_meta(),__VA_ARGS__); \
    return _meta; \
} \
virtual std::shared_ptr<vstd::meta> meta() const{ \
    return static_meta(); \
} \
private: \

#define V_PROPERTY(CLASS, TYPE, NAME, GETTER, SETTER) std::make_shared<vstd::detail::property_impl<CLASS,TYPE>>(V_STRING(NAME),&CLASS::GETTER,&CLASS::SETTER)

namespace vstd {
    class property {
    public:
        virtual std::string name() = 0;

        virtual boost::any get(boost::any object) = 0;

        virtual void set(boost::any object, boost::any value) = 0;

        virtual boost::typeindex::type_index object_type() = 0;

        virtual boost::typeindex::type_index value_type() = 0;
    };

    namespace detail {
        template<typename ObjectType, typename PropertyType>
        class property_impl : public property {
            std::function<PropertyType(ObjectType)> _getter;
            std::function<void(ObjectType, PropertyType)> _setter;
            std::string _name;
        public:
            template<typename Getter, typename Setter>
            property_impl(std::string name, Getter getter, Setter setter) :
                    _name(name), _getter(std::mem_fn(getter)), _setter(std::mem_fn(setter)) {

            }

            std::string name() override {
                return _name;
            }

            boost::any get(boost::any object) override {
                return _getter(boost::any_cast<ObjectType>(object));
            }

            void set(boost::any object, boost::any value) override {
                _setter(boost::any_cast<ObjectType>(object), boost::any_cast<PropertyType>(value));
            }

            boost::typeindex::type_index object_type() override {
                return boost::typeindex::type_id<ObjectType>();
            }

            boost::typeindex::type_index value_type() override {
                return boost::typeindex::type_id<PropertyType>();
            }
        };

        template<typename ObjectType, typename PropertyType>
        class dynamic_property_impl : public property {
            std::string _name;
            PropertyType _value;
        public:
            dynamic_property_impl(std::string name) : _name(name) {

            }

            std::string name() override {
                return _name;
            }

            boost::any get(boost::any object) override {
                return boost::any(_value);
            }

            void set(boost::any object, boost::any value) override {
                _value = boost::any_cast<PropertyType>(value);
            }

            boost::typeindex::type_index object_type() override {
                return boost::typeindex::type_id<ObjectType>();
            }

            boost::typeindex::type_index value_type() override {
                return boost::typeindex::type_id<PropertyType>();
            }
        };
    }

    class meta {
        std::string _name;
        std::unordered_map<std::string, std::shared_ptr<property>> _props;
        std::shared_ptr<meta> _super;


        void _add() {

        }

        template<typename... Args>
        void _add(std::shared_ptr<property> prop, Args... props) {
            _props[prop->name()] = prop;
            add(props...);
        };

        template<typename ObjectType, typename PropertyType>
        std::shared_ptr<property> _get_property_object(ObjectType ob, std::string name) {
            if (vstd::ctn(_props, name)) {
                return _props[name];
            } else if (vstd::ctn(ob._dynamic_props, name)) {
                return ob._dynamic_props[name];
            }
            ob._dynamic_props[name] = std::make_shared<detail::dynamic_property_impl<ObjectType, PropertyType>>(name);
            return _get_property_object<PropertyType>(ob, name);
        }

    public:
        template<typename... Args>
        meta(std::string name, std::shared_ptr<meta> super, Args... props) : _name(name), _super(super) {
            _add(props...);
        }

        std::shared_ptr<meta> super() {
            return _super;
        }

        template<typename ObjectType, typename PropertyType>
        void set_property(std::string prop, ObjectType t, PropertyType p) {
            _get_property_object<PropertyType>(t, prop)->set(boost::any(t), boost::any(p));
        }

        template<typename ObjectType, typename PropertyType>
        PropertyType get_property(std::string prop, ObjectType t) {
            return boost::any_cast<PropertyType>(_get_property_object<PropertyType>(prop, t)->get(boost::any(t)));
        }

        template<typename ObjectType>
        boost::typeindex::type_index get_property_type(ObjectType ob, std::string name) {
            if (vstd::ctn(_props, name)) {
                return _props[name]->value_type();
            } else if (vstd::ctn(ob->_dynamic_props, name)) {
                return ob->_dynamic_props[name]->value_type();
            }
            return boost::typeindex::type_id<void>();
        }
    };

    class empty_meta {
    public:
        static std::shared_ptr<vstd::meta> static_meta() {
            return nullptr;
        }
    };
}