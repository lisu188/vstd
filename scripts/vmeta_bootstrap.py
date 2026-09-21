from pathlib import Path

path = Path('vmeta.h')
text = path.read_text()
marker = '#define V_METHOD(CLASS, NAME, ...)'
assert text.count(marker) == 1
text = text[:text.index(marker)] + r'''#define VSTD_META_MEMBER(CLASS, NAME, ...) \
    []<typename MetaClass = CLASS>() \
    { \
        if constexpr (requires { &MetaClass::NAME; }) \
        { return &MetaClass::NAME; } \
        else \
        { return vstd::detail::member_selector<MetaClass __VA_OPT__(,) __VA_ARGS__>::select(&MetaClass::NAME); } \
    }()
#define V_METHOD(CLASS, NAME, ...) \
    std::make_shared<vstd::detail::method_impl<CLASS __VA_OPT__(,) __VA_ARGS__>>( \
        V_STRING(NAME), VSTD_META_MEMBER(CLASS, NAME __VA_OPT__(,) __VA_ARGS__))
#define V_PROPERTY(CLASS, TYPE, NAME, GETTER, SETTER) \
    std::make_shared<vstd::detail::property_impl<CLASS, TYPE>>( \
        V_STRING(NAME), VSTD_META_MEMBER(CLASS, GETTER, TYPE), VSTD_META_MEMBER(CLASS, SETTER, void, TYPE)), \
        V_METHOD(CLASS, GETTER, TYPE), V_METHOD(CLASS, SETTER, void, TYPE)
'''
path.write_text(text)
path = Path('tests/vmeta_regressions.cpp')
text = path.read_text()
marker = 'struct Empty'
assert text.count(marker) == 1
text = text.replace(marker, '''struct AccessorSubject
{
    V_META(AccessorSubject, vstd::meta::empty,
           V_PROPERTY(AccessorSubject, std::string, label, getLabel, setLabel),
           V_METHOD(AccessorSubject, qualified, int))
  public:
    std::string label;
    const std::string& getLabel() const { return label; }
    void setLabel(const std::string& value) { label = value; }
    int qualified() { return 1; }
    int qualified() const { return 2; }
};

''' + marker)
marker = 'void values()\n{'
assert text.count(marker) == 1
text = text.replace(marker, marker + '''
    auto accessors = std::make_shared<AccessorSubject>();
    accessors->meta()->set_property("label", accessors, std::string("compatible"));
    CHECK(accessors->meta()->get_property<AccessorSubject, std::string>("label", accessors) == "compatible");
    CHECK(accessors->meta()->invoke_method<std::string>("getLabel", accessors) == "compatible");
    accessors->meta()->invoke_method<void>("setLabel", accessors, std::string("updated"));
    CHECK(accessors->label == "updated");
    CHECK(accessors->meta()->invoke_method<int>("qualified", accessors) == 1);
''')
path.write_text(text)
path = Path('CMakeLists.txt')
if path.exists():
    path.write_text(path.read_text().replace('${HEADER_SELF_SUFFICIENCY_DIR}/${HEADER_FILE}.cpp', '${HEADER_SELF_SUFFICIENCY_DIR}/${HEADER_SYMBOL}.cpp'))
