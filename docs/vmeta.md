# Runtime reflection contract (API version 2)

`vmeta.h` and `vany.h` can be included directly. No prerequisite `vhash.h` include is required.

## Values and references

Registered property types must be non-reference, copy-constructible values. Declare a string property as `std::string`, even when its C++ getter returns `const std::string&`. Reference-typed property descriptors and dynamic property declarations are rejected at compile time so a value-returning getter cannot accidentally create a dangling reference wrapper.

Reflection results own their values. A registered C++ method returning `T&` or `const T&` is exposed as a copied `T`; request `invoke_method<T>`, not `invoke_method<T&>`. Reference result requests and reference casts from temporary `std::any` objects are rejected at compile time.

Value and const-reference parameters accept owned arguments. Exact const-reference arguments can borrow the invocation payload. Converted arguments are retained by the invocation until the function returns. Mutable reference parameters require `std::ref(value)`; passing an ordinary value throws rather than silently mutating a copy. `std::cref` is supported for read-only references. Rvalue-reference parameters receive an invocation-owned value, not ownership of the caller's object. Argument and result values must be copy-constructible because the transport is `std::any`.

`std::any` is a pass-through erased payload for method arguments, results, and properties, not an automatically nested `any`. Exact value casts bypass converters. Borrowing `vstd::any_cast<T&>` is available only from an existing, exactly typed lvalue `std::any`; conversions return owned values.

## Registration and overloads

`V_METHOD` supports const/non-const member functions, void shorthand, and a variadic argument list. Dynamic methods accept copyable mutable callables and member pointers. Empty `V_META(Type, Base)` declarations are valid.

Runtime signatures use the method name and normalized value types. Distinct value-type overloads are supported. Overloads differing only by reference or cv qualification are deliberately not separately dispatchable: duplicate registration in one metadata definition throws `std::invalid_argument`. Inherited and per-object overrides remain supported.

Names must be unique. `V_META_NAMED(Type, Base, "namespace::Type", ...)` supplies an explicit stable name without changing the C++ class name. Existing unique `V_META` names remain unchanged for serialization compatibility. Duplicate names fail explicitly rather than resolving to the first unrelated type.

## Object identity, storage, and threading

Automatic shared-pointer conversions are registered in both directions for reflected inheritance edges. Conversion lookup can compose registered edges, including multi-level inheritance, independently of registration order. A direct conversion takes precedence over a composed path. User-supplied conversion callbacks execute after the registry lock is released.

Conversion registration and metadata name-index access are synchronized. `meta::index()` returns an immutable shared snapshot, not a writable global map. The internal conversion registry is no longer exposed as an unordered_map.

Static descriptors are shared. Copying a reflected object clones its dynamic property values and dynamic method descriptors; mutable lambda state is copied rather than unintentionally shared. Captures and values that themselves contain shared pointers retain their normal C++ sharing semantics. Custom dynamic descriptors must implement `clone()` to opt into copying.

New dynamic properties are initialized before publication. Failed creation leaves no new visible property. This does not promise a strong exception guarantee for arbitrary user setters or assignment to an existing property.

All object-facing reflection operations reject null object pointers. Reflection does not serialize arbitrary object mutations: concurrent access to an individual object's dynamic maps, mutable callable state, or user data still requires caller synchronization.

## Pair hashing migration

The non-standard `std::hash` specializations for standard-library pair types have been removed. Supply `vstd::pair_hash` explicitly as the hasher of pair-keyed unordered containers. The conversion registry has its own local hasher.

## Validation

`vmeta_regressions` directly includes `vmeta.h` and instantiates its public API, including concurrency and compile-time rejection checks. `vmeta_allocations` guards zero allocations for a cached integer method with a reused argument vector. Existing `vmeta_tests` assertions remain active in Release builds. Run CMake/CTest in Debug and Release and run the regression executable separately with address/undefined and thread sanitizers.
