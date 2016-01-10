#pragma once

#include "vdefines.h"
#include "vlogger.h"

namespace vstd {
    template<typename T, typename... Args>
    force_inline T fail_if(T arg, Args... args) {
        if (arg) {
            vstd::logger::fatal(args...);
        }
        return arg;
    }

    template<typename... Args>
    force_inline void fail(Args... args) {
        fail_if(true, args...);
    }

    template<typename T, typename... Args>
    force_inline T not_null(T t, Args... args) {
        if (t) {
            return t;
        }
        fail(args...);
    }
}
