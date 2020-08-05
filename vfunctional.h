/*
 * MIT License
 *
 * Copyright (c) 2019 Andrzej Lis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
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

        template<typename Return, typename Ctn, typename Func>
        Return map(Ctn ctn, Func f) {
            Return ret;
            for (typename Ctn::value_type val:ctn) {
                ret.insert(f(val));
            }
            return ret;
        }

        template<typename Ctn, typename Func>
        void foreach(Ctn ctn, Func f) {
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