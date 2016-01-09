#pragma once

#include <string>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

namespace vstd {
    template<typename T=std::string>
    T replace(T str, T from, T to) {
        size_t start_pos = str.find(from);
        if (start_pos == std::string::npos) {
            return false;
        }
        str.replace(start_pos, from.length(), to);
        return str;
    }

    template<typename T=std::string>
    T ltrim(T s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    template<typename T=std::string>
    T rtrim(T s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    template<typename T=std::string>
    T trim(T s) {
        return ltrim(rtrim(s));
    }

    bool is_empty(std::string string) {
        return trim(string).length() == 0;
    }

    template<typename T>
    std::string str(T c) {
        return std::string(c);
    }

    template<typename T>
    std::pair<int, bool> to_int(T s) {
        try {
            return std::make_pair(std::stoi(s), true);
        } catch (...) {
            return std::make_pair(0, false);
        }
    };
}