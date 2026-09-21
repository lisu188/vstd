from pathlib import Path
import json
import os
import re
import subprocess

path = Path('vmeta.h')
text = path.read_text()
marker = 'template <typename Actual, typename Expected>\nconcept MetaReturnCompatible'
assert text.count(marker) == 1
text = text.replace(marker, 'template <typename T>\nconcept MetaPropertyValue = !std::is_reference_v<T> && std::copy_constructible<T>;\n\n' + marker)
for name, invocable in [('MetaGetter', 'std::invocable<Getter, ObjectType*>'), ('MetaSetter', 'std::invocable<Setter, ObjectType*, PropertyType>')]:
    old = 'concept ' + name + ' = ' + invocable
    assert text.count(old) == 1
    text = text.replace(old, 'concept ' + name + ' = MetaPropertyValue<PropertyType> && ' + invocable)
for name in ('property_impl', 'dynamic_property_impl'):
    pattern = r'template\s*<typename ObjectType,\s*typename PropertyType>\s*class ' + name
    text, count = re.subn(pattern, 'template <typename ObjectType, typename PropertyType>\n    requires MetaPropertyValue<PropertyType>\nclass ' + name, text)
    assert count == 1
pattern = r'(template <typename ObjectType, typename PropertyType>)\s*(void set_dynamic_property)'
text, count = re.subn(pattern, r'\1\n        requires detail::MetaPropertyValue<PropertyType>\n    \2', text)
assert count == 1
path.write_text(text)
path = Path('tests/vmeta_regressions.cpp')
text = path.read_text()
match = re.search(r'template\s*<typename R>\s*concept CanInvokeResult', text)
assert match
checks = '''template <typename T> concept CanDeclareProperty = requires
{
    typename vstd::detail::property_impl<Subject, T>;
    typename vstd::detail::dynamic_property_impl<Subject, T>;
};
template <typename T> concept CanSetDynamicProperty = requires(vstd::meta& meta, std::shared_ptr<Subject> object, std::remove_cvref_t<T>& value)
{
    meta.template set_dynamic_property<Subject, T>("value", object, value);
};
static_assert(CanDeclareProperty<std::string>);
static_assert(!CanDeclareProperty<std::string&>);
static_assert(!CanDeclareProperty<const std::string&>);
static_assert(!CanDeclareProperty<std::string&&>);
static_assert(!CanSetDynamicProperty<std::string&>);
static_assert(!CanSetDynamicProperty<const std::string&>);
static_assert(!CanSetDynamicProperty<std::string&&>);

'''
text = text[:match.start()] + checks + text[match.start():]
path.write_text(text)
path = Path('docs/vmeta.md')
text = path.read_text()
marker = '## Values and references\n'
assert text.count(marker) == 1
text = text.replace(marker, marker + '\nRegistered property types must be non-reference, copy-constructible values. Declare a string property as `std::string`, even when its C++ getter returns `const std::string&`. Reference-typed property descriptors and dynamic property declarations are rejected at compile time so a value-returning getter cannot accidentally create a dangling reference wrapper.\n')
path.write_text(text)
subprocess.run(['cmake', '-S', '.', '-B', 'build', '-DCMAKE_BUILD_TYPE=Release'], check=True)
subprocess.run(['cmake', '--build', 'build', '--target', 'format'], check=True)
subprocess.run(['cmake', '--build', 'build', '--target', 'format-check'], check=True)
subprocess.run(['cmake', '--build', 'build', '-j2'], check=True)
subprocess.run(['ctest', '--test-dir', 'build', '--output-on-failure'], check=True)
subprocess.run(['git', 'diff', '--check'], check=True)
paths = ['vmeta.h', 'tests/vmeta_regressions.cpp', 'docs/vmeta.md']
subprocess.run(['git', 'diff', '--', *paths], check=True)
entries = []
for name in paths:
    payload = json.dumps({'content': Path(name).read_text(), 'encoding': 'utf-8'})
    result = subprocess.run(['gh', 'api', '--method', 'POST', 'repos/' + os.environ['GITHUB_REPOSITORY'] + '/git/blobs', '--input', '-'], input=payload, text=True, capture_output=True, check=True)
    entries.append({'path': name, 'mode': '100644', 'type': 'blob', 'sha': json.loads(result.stdout)['sha']})
print('FORMATTED_TREE=' + json.dumps(entries))
