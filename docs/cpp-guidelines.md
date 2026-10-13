# C++ language and library guidelines

Build and consume `vstd` as C++26 with extensions disabled and standard fallback prohibited. CMake 4.0.2 is the
minimum; CI provisions 4.3.1. Supported desktop toolchains are GCC 15, Clang 20 with a compatible standard library,
and LLVM 20.1.8 clang-cl with the Visual Studio 2022 standard library. Configuration probes the required language
and library facilities. Use the
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
for interfaces, resource ownership, and object lifetimes. Apply improvements incrementally and retain public
utility names and existing consumer contracts. Check new facilities against the
[libstdc++ implementation status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html) and
[Microsoft C++ conformance table](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance).

Prefer standard string predicates such as `contains`, `starts_with`, and `ends_with` over manual searches when only
membership is needed. Use `.empty()` to express emptiness. Keep byte classification safe by converting a `char` to
`unsigned char` before passing it to `<cctype>` functions.

Use constrained `std::invoke` for general callable invocation, including mutable lambdas, function pointers, and
member pointers. `functional::call` retains its existing by-value callable and argument parameters; invocation uses
those local copies as lvalues. Use `std::ref` when an explicit reference is required. Callable argument selection
uses C++26 pack indexing, retaining the nullary `void` first-argument sentinel. The public `tuple_element` helper
still delegates to `std::tuple_element`; avoid reserved identifiers in template parameters.

Use `std::move_only_function` for owned one-shot work. Keep `std::function` for callbacks that need to be copied,
including observer snapshots. Do not assume availability of `std::inplace_vector`, `std::function_ref`, or the
saturation library on the selected Windows standard library. Deleted borrowed-reference overloads state why
temporaries cannot be used; the lifetime and ownership contracts remain unchanged.

When a member pointer returns a reference, `functional::call` requires a raw-pointer or `std::reference_wrapper`
receiver. Reference results are materialized as values when the callable or any argument may own copied state. To retain
a reference result, borrow the callable with `std::ref` or use a function/member pointer, and borrow every argument with
`std::ref` (a member receiver may also be a raw pointer). References to local `std::reference_wrapper` handles are copied
as handles. Noncopyable lvalue reference results require explicit borrowing; rvalue reference results can move into the
returned value. The caller must keep all borrowed state alive while using a retained reference. In particular, a member
function returning a reference and taking an additional value argument produces a value result unless that argument is
explicitly wrapped with `std::ref`.

`partial::bind` owns its callable and bound arguments. Invocations expand nested binders directly from that persistent
closure, pass ordinary bound arguments as lvalues, and forward call-time arguments. A returned reference into bound state
remains valid only while the owning binder lives; a returned reference into a call-time argument follows that argument's
lifetime. Nested mutable binders retain state between calls.
When a nested expansion produces a value, a reference returned by the outer callable is materialized before the expanded
temporary is destroyed. Noncopyable lvalue reference results cannot escape such an invocation. Materialized results use
direct construction so explicitly declared copy constructors remain supported.

A public polymorphic interface that supports destruction through its base pointer needs a virtual destructor
(Core Guidelines C.35 and C.80). `stringable` now has a defaulted virtual destructor. Rebuild all consumers together
when updating this header-only library because the change affects its virtual table.

## String compatibility contracts

- `replace` replaces non-overlapping matches from left to right and resumes after the inserted text. It does not
  recursively replace inserted text or revisit matches formed across a replacement boundary. An empty search string
  leaves the input unchanged. This makes identity replacements and escaping a quote as `\"` terminate and avoids
  growing the call stack with the number of matches.
- `to_int` retains the existing decimal conversion contract: the first byte must be a digit, signs and leading
  whitespace are rejected, and the complete input must parse for success. A partial parse such as `42x` returns
  `{42, false}`; overflow and invalid input return `{0, false}`. Keep `std::stoi` here to preserve conversion behavior.
- `ends_with` remains case-sensitive, accepts an empty suffix, and handles embedded null bytes as string data.
- `camel` retains its existing spacing, including the trailing space for a multiword input.
- `tuple_size` retains its existing `void` sentinel behavior, including an empty type list.

## Coroutine ownership and scheduling

Include `vtask.h` for `task<T>`, `task_handle<T>`, and `task_group`. A task is lazy and move-only; its body starts
inline on the spawning thread and runs until the first suspension. Supported results are `void` and copyable owned
values. A handle is copyable, supports multiple awaiters, and returns an independent copy on each successful read.
References are not task results. Give coroutine functions their owned arguments by value: a coroutine lambda does
not extend the lifetime of its closure.

```cpp
vstd::task<int> compute(std::shared_ptr<vstd::event_loop<>> mainLoop,
                        std::shared_ptr<vstd::thread_pool<2>> workers,
                        std::unique_ptr<int> input)
{
    co_await vstd::post(mainLoop);
    co_return co_await vstd::work(workers, [input = std::move(input)] { return *input + 1; });
}

vstd::task_group session;
auto handle = session.spawn(compute(mainLoop, workers, std::make_unique<int>(41)),
                            vstd::loopScheduler(mainLoop));
```

`post(loop)` switches to the main loop; `work(pool)` switches to a worker. Both change the scheduler captured by
subsequent awaits. `work(pool, callable)` instead executes owned one-shot work on a worker and resumes its caller
through the previously captured scheduler. The callable may own move-only captures. `sleepFor(loop, duration)`,
`until(loop, predicate)`, and `nextFrame(loop)` register with the existing timer, condition, and frame phases and
resume through the caller's captured scheduler. Posting during a queue snapshot waits for a later snapshot.
Cancellation removes suspended timer, condition, and frame registrations. Reusable observers remain copyable
`std::function` callbacks and use the event loop's copied observer snapshots.

A scheduler must either accept ownership of its callback or reject it. Rejected or discarded continuations fail
with `scheduler_rejected`, including execution-completion awaits. Error unwinding can run inline after rejection;
code requiring thread affinity must not assume a rejected dispatch reached its target thread.

Cancellation is cooperative. `requestStop()` settles an outstanding result with `task_cancelled` once and requests
its stop token. Work cancelled before it starts skips its body; work already running completes or observes
`co_await thisTaskStopToken()`. Result settlement and execution completion are separate states: `ready()` and `get()`
describe the result, while `finished()`, `waitFinished()`, and `join()` describe completion of execution and destruction
of the coroutine frame. A cancelled result may be ready while worker code is still running.

`whenAll(vector<task_handle<T>>)` preserves input order. The first child failure requests cancellation of all remaining
children, and the join waits for every child's execution to finish before propagating the error. A failure to schedule
a join continuation follows the same cancel-and-drain contract.

Each runtime owner should keep a task group, reject new work when shutdown begins, request cancellation, and retain
the resources used by tasks until `finished()` is true. The group destructor only requests cancellation; it does not
block. `join()` returns `false` inside any task owned by that group, and `ownsCurrentTask()` supports an explicit
deferred-teardown path. Never block a loop thread on `get()` or `join()` while its continuations need that loop to run.
Synchronous entry adapters must provide a deliberate wait boundary and release interpreter locks as appropriate.

Legacy `future` and threading facade behavior remains available to external consumers. New coroutine clients should
use explicit awaitables and session ownership rather than installing nested event-loop polling in native flows.

## Validation

Follow `AGENTS.md` and run the CMake `format` and `format-check` targets before committing C++ changes. CTest runs
focused string and callable/type-list regressions alongside the existing reflection, future, neural, random, and
header self-sufficiency and coroutine tests. The upstream workflow builds Debug and Release with GCC, Clang, and
Windows clang-cl, and retains Linux address/undefined/thread sanitizer checks. The setup source-preservation
regression runs without a native build. Consumer integration
must also rebuild and test the game on Linux and Windows, including its performance guards and coverage gate.
