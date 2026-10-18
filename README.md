# vstd

`vstd` is a header-only C++ utility toolkit that bundles reusable building blocks for
asynchronous execution, type utilities, reflection, logging, conversion helpers, and
small algorithmic/data-structure conveniences.

The build requires CMake 4.0.2 or newer and C++26 with compiler extensions disabled and no standard fallback.
CI provisions CMake 4.3.1 and tests GCC 15 and Clang 20 on Ubuntu 26.04, plus LLVM 20.1.8 clang-cl with
the Visual Studio 2022 SDK, runtime, and standard library on Windows 2022. Configuration checks pack indexing,
deleted-function reasons, coroutines, `std::expected`, and `std::move_only_function` before loading dependencies.
Consumers using CMake can link `vstd::headers` to inherit the `cxx_std_26` requirement. See the
[C++ guidelines](docs/cpp-guidelines.md) for supported standard-library idioms,
compatibility contracts, and validation requirements.

The optional SDL event-loop and Boost.Python converter regression targets run when
their development packages are installed. CI configures with
`-DVSTD_REQUIRE_INTEGRATION_TESTS=ON` so missing dependencies fail configuration.
The SDL tests initialize only the events subsystem and run without a display.

`as_array` returns copy-constructed elements in pool-allocated storage. Destroy
nontrivial elements with `std::destroy_n(pointer, count)` before returning the raw
storage with `vstd::deallocate(pointer, count)`. A failed copy destroys partial
elements and releases their storage; an empty input returns `nullptr`.

An external `thread_pool::stop()` waits for the currently active generation's queued work and workers to finish.
Stopping from any worker generation of that pool requests shutdown asynchronously,
allowing that task and dependent peer tasks to finish. Restarting creates an independent queue.
Custom worker templates receive a shared queue state exposing `pop_task`; a worker
must not retain shared ownership of the pool itself.

## CMake package consumption

The initial CMake package metadata version is `0.1.0`. Configure and install with
`cmake -S . -B build` and `cmake --install build --prefix /your/prefix`, then use:

```cmake
find_package(vstd 0.1 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE vstd::headers)
```

Set `CMAKE_PREFIX_PATH` to the installed prefix. The target supplies the C++26
requirement, public include directory, `Boost::headers`, `Boost::filesystem` and
`Threads::Threads`; consumers do not need separate dependency discovery or link
flags for the core headers. Include them as `<vstd.h>`, `<vthreadpool.h>`, etc.
The same package can be loaded from a configured build tree with `vstd_DIR`.
Installed include and package paths are relative to the prefix and remain usable
after moving the installation. CI builds and runs separate consumers against both
forms on GCC, Clang and Windows clang-cl in Debug and Release configurations.

The optional `veventloop.h` and `vconverter.h` integrations still require callers
to provide SDL2 or Boost.Python/Python development dependencies respectively.
These are not imposed on the core `vstd::headers` target.

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

- **`vtask.h`**
  Lazy move-only coroutine tasks, shared task handles, task groups, cooperative cancellation, scheduler-aware
  continuations, and ordered joins. Owned one-shot work uses `std::move_only_function`; reusable observers retain
  their copyable callback contracts. Legacy `vfuture.h` remains available to existing consumers.

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
  function-based value generation. The existing `cache::get(key, int time)` and
  `cache2::get(key, int time, callback)` APIs use caller-defined matching tick/TTL units.
  Values remain valid at the inclusive TTL boundary and refresh on the first later tick;
  zero TTL reuses a value only at the insertion tick. Negative TTL returns the loaded
  value without storing it. A backward tick (including a timer reset/wrap) refreshes
  that key. Elapsed-time comparison avoids signed deadline/subtraction overflow across
  the full `int` range. TTL is sampled once after each successful value load; provider
  exceptions propagate without caching a failed load. These caches remain unsynchronized.

- **`vlazy.h`**  
  Lazy initialization wrapper that materializes and stores a shared object only on
  first access, with explicit reset support.

### Reflection, metaobject, and advanced modules

- **`vmeta.h`**  
  Runtime reflection/metaobject system driven by macros (`V_META`, property
  registration, getter/setter/method metadata, dynamic property access).

- **`vneuro.h`**  
  Deterministic feed-forward neural network with contiguous RAII storage, Xavier
  initialization, sigmoid activations, momentum SGD, bounded training, prediction,
  RMSE reporting, and numerical gradient checking. Legacy training entry points remain
  available as compatibility wrappers; see `docs/vneuro.md`.

## Typical include patterns

- Include only the module headers you need for tighter compile boundaries.
- Use `#include "vstd.h"` when convenience is preferred over minimal include surface.

## Build and formatting

Project configuration and checks are CMake-driven.

```bash
cmake -S . -B build-cxx26-gcc15 -DCMAKE_CXX_COMPILER=g++-15
cmake --build build-cxx26-gcc15 --target format-check
cmake --build build-cxx26-gcc15 --parallel
ctest --test-dir build-cxx26-gcc15 --output-on-failure
```

If formatting drift is reported:

```bash
cmake --build build-cxx26-gcc15 --target format
```

Use a separate build directory when changing compilers. `configure.sh` defaults to `g++-15` and `build-cxx26`;
override `CXX` and `VSTD_BUILD_DIR` together for another supported compiler. Setup installs system dependencies
when requested and configures the existing source without rewriting it. Install CMake 4.0.2 or newer beforehand.
Run `python tests/toolchain_setup_tests.py` to check source preservation without configuring or building.
