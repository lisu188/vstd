#pragma once

#include <boost/any.hpp>
#include <functional>
#include <unordered_map>
#include <memory>

#define V_NONE vstd::empty_meta

#define V_STRING(X) #X

#define V_META(CLASS, SUPER, ...) \
public: \
static std::shared_ptr<vstd::meta> static_meta(){ \
    static std::shared_ptr<vstd::meta> _meta=std::make_shared<vstd::meta>(V_STRING(CLASS),SUPER::static_meta(),__VA_ARGS__); \
    return _meta; \
} \
virtual std::shared_ptr<vstd::meta> meta() const{ \
    return static_meta(); \
} \
private: \

#define V_PROPERTY(CLASS,TYPE,NAME,GETTER,SETTER) std::make_shared<vstd::detail::property<CLASS,TYPE>>(V_STRING(NAME),&CLASS::GETTER,&CLASS::SETTER)

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

            std::string name() override {
                return _name;
            }

            boost::any get(boost::any object) override {
                return _getter(boost::any_cast<ObjectType>(object));
            }

            void set(boost::any object, boost::any value) override {
                _setter(boost::any_cast<ObjectType>(object), boost::any_cast<PropertyType>(value));
            }
        };
    }

    class meta {
        std::string _name;
        std::unordered_map<std::string, std::shared_ptr<detail::base_property>> _props;
        std::shared_ptr<meta> _super;

        void add(){

        }

        template<typename... Args>
        void add(std::shared_ptr<detail::base_property> prop,Args... props){
            _props[prop->name()] = prop;
            add(props...);
        };
    public:
        template<typename... Args>
        meta(std::string name,std::shared_ptr<meta> super,Args... props) : _name(name), _super(super) {
            add(props...);
        }

        std::shared_ptr<meta> super() {
            return _super;
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

    class empty_meta{
    public:
        static std::shared_ptr<vstd::meta> static_meta(){
            return nullptr;
        }
    };
}