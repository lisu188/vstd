# Copyright (c) 2026 Andrzej Lis
# SPDX-License-Identifier: MIT
if(NOT "cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(FATAL_ERROR "C++26 is required without fallback. Use GCC 15+ or clang-cl 20.1.8+ with CMake 4.0.2+.")
endif()

include(CheckCXXSourceCompiles)

function(vstd_require_cxx26_feature name source)
    check_cxx_source_compiles("${source}" VSTD_CXX26_${name})
    if(NOT VSTD_CXX26_${name})
        message(FATAL_ERROR
                "vstd requires C++26 ${name} support with the selected compiler and standard library. "
                "Use GCC 15 or Clang 20 (clang-cl 20.1.8 on Windows) with a compatible C++ standard library; "
                "the build does not fall back to an older language standard.")
    endif()
endfunction()

vstd_require_cxx26_feature(PACK_INDEXING [[
    #include <type_traits>
    template <typename... Types> using selected = Types...[1];
    static_assert(std::is_same_v<selected<int, const long&>, const long&>);
    int main() { return 0; }
]])

vstd_require_cxx26_feature(DELETED_FUNCTION_REASONS [[
    struct borrowed {
        borrowed() = default;
        borrowed(borrowed&&) = delete("cannot borrow a temporary");
    };
    int main() { borrowed value; }
]])

vstd_require_cxx26_feature(COROUTINES [[
    #include <coroutine>
    struct task {
        struct promise_type {
            task get_return_object() { return {}; }
            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() noexcept {}
        };
    };
    task run() { co_return; }
    int main() { run(); }
]])

vstd_require_cxx26_feature(EXPECTED [[
    #include <expected>
    int main() { std::expected<int, int> value{4}; return value.value() - 4; }
]])

include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/..")
vstd_require_cxx26_feature(MOVE_ONLY_FUNCTION [[
    #include "vmove_only_function.h"
    #include <memory>
    int main() {
        vstd::move_only_function<int()> callback = [value = std::make_unique<int>(4)] { return *value; };
        return callback() - 4;
    }
]])
cmake_pop_check_state()
