# C++ language and library guidelines

Build and consume `vstd` as C++23 with extensions disabled. Use the
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
those local copies as lvalues. Use `std::ref` when an explicit reference is required. Delegate type-list element lookup
to `std::tuple_element` and avoid reserved identifiers in template parameters.

When a member pointer returns a reference, `functional::call` requires a raw-pointer or `std::reference_wrapper`
receiver. Returning a reference into a copied receiver would dangle as the call exits. The caller must keep the borrowed
receiver alive while using the result.

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

## Validation

Follow `AGENTS.md` and run the CMake `format` and `format-check` targets before committing C++ changes. CTest runs
focused string and callable/type-list regressions alongside the existing reflection, future, neural, random, and
header self-sufficiency tests. The upstream workflow builds Debug and Release with GCC and Clang. Consumer integration
must also rebuild and test the game on Linux and Windows, including its performance guards and coverage gate.
