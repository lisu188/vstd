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

#include <string>
#include <sstream>
#include <iostream>
#include "vdefines.h"
#include "vstring.h"

namespace vstd {
    class logger {
        template<typename T, typename U, typename ...Args>
        static void log(T t, U u, Args... args) {
            std::stringstream stream;
            stream << vstd::str(t) << " " << u;
            log(stream.str(), args...);
        }

        template<typename T>
        static void log(T t) {
            std::cout << t << std::endl;
        }

    public:
        template<typename ...Args>
        static void fatal(Args... args) {
            log("FATAL:", args...);
            std::terminate();
        }

        template<typename ...Args>
        static void error(Args... args) {
            log("ERROR:", args...);
        }

        template<typename ...Args>
        static void warning(Args... args) {
            log("WARNING:", args...);
        }

        template<typename ...Args>
        static void info(Args... args) {
            log("INFO:", args...);
        }

        template<typename ...Args>
        static void debug(Args... args) {
            log("DEBUG:", args...);
        }
    };
}