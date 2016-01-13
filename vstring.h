#pragma once

#include <string>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include "vdefines.h"

namespace vstd {
    template<typename T=void>
    std::string replace(std::string str, std::string from, std::string to) {
        size_t start_pos = str.find(from);
        if (start_pos == std::string::npos) {
            return str;
        }
        str.replace(start_pos, from.length(), to);
        return replace(str, from, to);
    }

    template<typename T=void>
    force_inline std::string ltrim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    template<typename T=void>
    force_inline std::string rtrim(std::string s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    template<typename T=void>
    force_inline  std::string trim(std::string s) {
        return ltrim(rtrim(s));
    }

    template<typename T=void>
    force_inline bool is_empty(std::string string) {
        return trim(string).length() == 0;
    }

    template<typename T>
    force_inline std::string str(T c) {
        return std::string(c);
    }

    template<typename T=void>
    force_inline std::pair<int, bool> to_int(std::string s) {
        try {
            return std::make_pair(std::stoi(s), true);
        } catch (...) {
            return std::make_pair(0, false);
        }
    };

    template<typename T=void>
    force_inline  std::string join(std::list<std::string> list, std::string sep) {
        std::stringstream stream;
        int i = 0;
        for (std::string str:list) {
            stream << str;
            if (i++ != list.size() - 1) {
                stream << sep;
            }
        }
        return stream.str();
    }

    template<typename T=void>
    force_inline std::vector<std::string> split(std::string s, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    template<typename T, typename U>
    force_inline  bool string_equals(T a, U b) {
        return str(a) == str(b);
    }
}