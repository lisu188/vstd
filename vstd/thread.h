#pragma once

#include "defines.h"
#include "traits.h"
#include "util.h"
#include "threadpool.h"

namespace vstd {
    extern std::function<void(std::function<void()>)> get_call_later_handler();
    extern std::function<void(std::function<void()>)> get_call_async_handler();
    extern std::function<void(std::function<void()>)> get_call_later_block_handler();
    extern std::function<void(std::function<bool()>)> get_wait_until_handler();

    template<typename Function, typename... Arguments>
    force_inline void call_later(Function target, Arguments... args) {
        get_call_later_handler()(vstd::bind(target, args...));
    }

    template<typename Function, typename... Arguments>
    force_inline void call_async(Function target, Arguments... args) {
        get_call_async_handler()(vstd::bind(target, args...));
    }

    template<typename Function, typename... Arguments>
    force_inline void call_later_block(Function target, Arguments... args) {
        get_call_later_block_handler()(vstd::bind(target, args...));
    }


    template<typename Predicate>
    force_inline void wait_until(Predicate pred) {
        get_wait_until_handler()(pred);
    }

    template<typename Predicate, typename Function, typename... Arguments>
    force_inline void call_when(Predicate pred, Function func, Arguments... params) {
        auto bind = vstd::bind(func, params...);
        call_later([pred, bind]() {
            wait_until(pred);
            call_later(bind);
        });
    }
}
