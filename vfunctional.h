#pragma once

#include "vdefines.h"
#include "vtraits.h"

namespace vstd {
    namespace functional {
        template<typename F, typename... Args>

        typename disable_if<is_void<typename function_traits<F>::return_type>::value,
                typename function_traits<F>::return_type>::type call(F f, Args... args) {
            return f(args...);
        }

        template<typename F, typename... Args>

        typename enable_if<is_void<typename function_traits<F>::return_type>::value,
                typename function_traits<F>::return_type>::type call(F f, Args... args) {
            f(args...);
        }

        template<typename Ctn, typename Func>
        std::set<typename function_traits<Func>::return_type> map(Ctn ctn, Func f) {
            typedef typename function_traits<Func>::return_type Return;
            std::set<Return> ret;
            for (typename Ctn::value_type val:ctn) {
                ret.insert(call<Return>(f, val));
            }
            return ret;
        }

        template<typename Ctn, typename Func>
        void map(Ctn ctn, Func f) {
            for (auto val:ctn) {
                f(val);
            }
        }

        template<typename T, typename Ctn, typename Func>
        T sum(Ctn ctn, Func f) {
            T s = 0;
            for (auto val:ctn) {
                s += f(val);
            }
            return s;
        }


        template<typename T, typename Ctn>
        T sum(Ctn ctn) {
            T s = 0;
            for (auto val:ctn) {
                s += val;
            }
            return s;
        }
    }
}