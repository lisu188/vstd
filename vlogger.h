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