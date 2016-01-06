#pragma once

#include <boost/any.hpp>
#include <functional>
#include <unordered_map>
#include <memory>

#define V_META(CLASS, SUPER, PROPS) \
meta(CLASS,[](){return SUPER::meta()},PROPS)

namespace vstd {
    namespace detail {
        class base_property {
        public:
            virtual std::string name() = 0;

            virtual boost::any get(boost::any object) = 0;

            virtual void set(boost::any object, boost::any value) = 0;
        };

        template<typename ObjectType, typename PropertyType>
        class property : public base_property {
            std::function<PropertyType(ObjectType)> _getter;
            std::function<void(ObjectType, PropertyType)> _setter;
            std::string _name;
        public:
            template<typename Getter, typename Setter>
            property(std::string name, Getter getter, Setter setter) :
                    _name(name), _getter(std::mem_fn(getter)), _setter(std::mem_fn(setter)) {

            }

            std::string property::name() override {
                return _name;
            }

            boost::any property::get(boost::any object) override {
                return _getter(boost::any_cast<ObjectType>(object));
            }

            void property::set(boost::any object, boost::any value) override {
                _setter(boost::any_cast<ObjectType>(object), boost::any_cast<PropertyType>(value));
            }
        };
    }

    class meta {
        std::string _name;
        std::unordered_map<std::string, std::shared_ptr<detail::base_property>> _props;
        std::function<std::shared_ptr<meta>()> _super;

        meta(std::string name, std::function<std::shared_ptr<meta>()> super,
             std::initializer_list<std::shared_ptr<detail::base_property>> props) : _name(name), _super(super) {
            for (auto prop:props) {
                _props[prop->name] = prop;
            }
        }

    public:
        std::shared_ptr<meta> super() {
            return _super();
        }

        template<typename ObjectType, typename PropertyType>
        void set_property(std::string prop, ObjectType t, PropertyType p) {
            _props[prop]->set(boost::any(t), boost::any(p));
        }

        template<typename ObjectType, typename PropertyType>
        PropertyType get_property(std::string prop, ObjectType t) {
            return boost::any_cast<PropertyType>(_props[prop]->get(boost::any(t)));
        }
    };
}