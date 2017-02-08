#pragma once

#include "vdefines.h"
#include "vtraits.h"
#include "vutil.h"
#include "vthreadpool.h"

namespace vstd {
    extern std::function<void(std::function<void()>)> get_call_later_handler();

    extern std::function<void(std::function<void()>)> get_call_async_handler();

    extern std::function<void(std::function<void()>)> get_call_later_block_handler();

    extern std::function<void(std::function<bool()>)> get_wait_until_handler();

    extern std::function<void(int, std::function<void()>)> get_call_delayed_async_handler();

    extern std::function<void(int, std::function<void()>)> get_call_delayed_later_handler();

    template<typename Function, typename... Arguments>
    void call_later(Function target, Arguments... args) {
        get_call_later_handler()(vstd::bind(target, args...));
    }

    template<typename Function, typename... Arguments>
    void call_async(Function target, Arguments... args) {
        get_call_async_handler()(vstd::bind(target, args...));
    }

    template<typename Function, typename... Arguments>
    void call_later_block(Function target, Arguments... args) {
        get_call_later_block_handler()(vstd::bind(target, args...));
    }


    template<typename Predicate>
    void wait_until(Predicate pred) {
        get_wait_until_handler()(pred);
    }

    template<typename Predicate, typename Function, typename... Arguments>
    void call_when(Predicate pred, Function func, Arguments... params) {
        auto bind = vstd::bind(func, params...);
        call_later([pred, bind]() {
            wait_until(pred);
            call_later(bind);
        });
    }

    template<typename Function, typename... Arguments>
    void call_delayed_async(int millis, Function target, Arguments... args) {
        get_call_delayed_async_handler()(millis, vstd::bind(target, args...));
    }

    template<typename Function, typename... Arguments>
    void call_delayed_later(int millis, Function target, Arguments... args) {
        get_call_delayed_later_handler()(millis, vstd::bind(target, args...));
    }
}
