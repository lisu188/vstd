# vstd

`vstd` is a header-only C++ utility toolkit that bundles reusable building blocks for
asynchronous execution, type utilities, reflection, logging, conversion helpers, and
small algorithmic/data-structure conveniences.

## Design overview

- **Header-first distribution**: most functionality lives in `v*.h` headers and can be
  consumed directly.
- **Composable primitives**: modules are intentionally small and often build on each
  other (for example, futures + threading + traits).
- **Boost-aware integration**: selected modules expose convenience wrappers for common
  Boost components (Python interop, range adaptors, string helpers, allocators).

## Module index (detailed)

### Aggregation and core includes

- **`vstd.h`**  
  Umbrella header that includes the major `vstd` modules so consumers can opt into
  most of the library through a single include.

- **`vdefines.h`**  
  Compile-time/interop macros used across the project (including Python-safety/logging
  macro helpers).

### Type system, traits, and generic conversion

- **`vtraits.h`**  
  Type traits and function-introspection helpers (`enable_if`/`disable_if` aliases,
  callable signature utilities, container/range/type checks).

- **`vtuple.h`**  
  Lightweight tuple metaprogramming helpers (`tuple_element`, `tuple_size`) for
  compile-time type-list style work.

- **`vcast.h`**  
  Generic `cast` utilities with overloads/SFINAE support for values, pointers,
  smart pointers, ranges/containers, and pair-like conversions.

- **`vany.h`**  
  Dynamic type-erasure utilities built on top of `std::any`, with a conversion
  registry to map between stored/target runtime types.

- **`vconverter.h`**  
  Boost.Python conversion bridges for binding C++ callable/object types through
  Python-facing adapters.

### Functional and call-composition helpers

- **`vfunctional.h`**  
  Functional helpers for safe invocation and higher-order call handling, including
  utilities that normalize void/non-void callable behavior.

- **`vbind.h`**  
  Partial application / prebinding utilities to construct deferred call wrappers and
  bind arguments incrementally.

- **`vchain.h`**  
  Serialized async-invocation chain that ensures callback execution order by
  appending work to an internal future pipeline.

- **`vadaptors.h`**  
  Range adaptor helpers (Boost.Range) for transforming collections of future-like
  values into chained/later-executed continuations.

### Concurrency and asynchronous runtime

- **`vthreadpool.h`**  
  Thread pool implementation with worker loop and stop-token-aware task processing.

- **`vthread.h`**  
  Threading/scheduling facade exposing configurable handlers for call-now,
  call-async, delayed execution, conditional waiting, and related dispatch patterns.

- **`vfuture.h`**  
  Future/promise-like async abstraction with continuation methods (sync/async/later),
  normalization logic for callable signatures, and composition support.

- **`veventloop.h`**  
  Event-loop abstraction (SDL-oriented) for frame callbacks, event callbacks, delayed
  tasks, and condition-based deferred execution.

### Logging, assertions, and diagnostics

- **`vlogger.h`**  
  Configurable logger with sink selection (stdout/stderr/file/disabled) and
  level-based logging methods.

- **`vassert.h`**  
  Assertion/fail-fast helpers (`fail_if`, `fail`, null checks) that route failures
  through logger-backed fatal diagnostics.

- **`vhex.h`**  
  Hex formatting helpers for values, raw pointers, and smart pointers (uppercase
  string output).

### String, hashing, and utility data helpers

- **`vstring.h`**  
  String utilities and conversions (`stringable` support, formatting/splitting and
  Boost-based string/file-path convenience routines).

- **`vhash.h`**  
  Hashing/metahash helpers and compile-time numeric utility templates used by generic
  components.

- **`vutil.h`**  
  General-purpose utilities and small containers/helpers (timing, synchronization,
  queues/collections, random helpers, allocator-oriented conveniences).

- **`vcache.h`**  
  TTL-style cache templates providing key/value memoization with refresh callbacks or
  function-based value generation.

- **`vlazy.h`**  
  Lazy initialization wrapper that materializes and stores a shared object only on
  first access, with explicit reset support.

### Reflection, metaobject, and advanced modules

- **`vmeta.h`**  
  Runtime reflection/metaobject system driven by macros (`V_META`, property
  registration, getter/setter/method metadata, dynamic property access).

- **`vneuro.h`**  
  Neural-network-oriented experimental module containing training/test data and
  matrix-based learning internals.

## Typical include patterns

- Include only the module headers you need for tighter compile boundaries.
- Use `#include "vstd.h"` when convenience is preferred over minimal include surface.

## Build and formatting

Project configuration and checks are CMake-driven.

```bash
cmake -S . -B build
cmake --build build --target format-check
```

If formatting drift is reported:

```bash
cmake --build build --target format
```
