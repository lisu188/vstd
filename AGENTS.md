# AGENTS.md

## Formatting policy (repository-wide)

- All C++ source/header files (`*.cpp`, `*.cc`, `*.cxx`, `*.h`, `*.hpp`) must be formatted with `clang-format`.
- The canonical style is defined in the repository `.clang-format` file.
- Before committing, run:
  - `cmake -S . -B build`
  - `cmake --build build --target format-check`
- If formatting drift is reported, fix it with:
  - `cmake --build build --target format`

## Command safety

- Keep command output concise and focused on the task.
- Do not use slow recursive shell patterns; prefer targeted `rg` queries.

## Git branch naming

- The default branch name has changed from `master` to `main`.
