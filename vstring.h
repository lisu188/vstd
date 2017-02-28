#pragma once

#include <stdio.h>
#include <string.h>
#include <list>
#include <string>
#include <sstream>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <boost/filesystem.hpp>
#include "vdefines.h"

namespace vstd {
    class stringable {
    public:
        virtual std::string to_string() = 0;
    };

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
    std::string ltrim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    template<typename T=void>
    std::string rtrim(std::string s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    template<typename T=void>
    std::string trim(std::string s) {
        return ltrim(rtrim(s));
    }

    template<typename T=void>
    bool is_empty(std::string string) {
        return trim(string).length() == 0;
    }

    template<typename T>
    std::string str(T c) {
        std::stringstream st;
        st << c;
        return st.str();
    }

    template<typename T=void>
    std::string str(std::shared_ptr<stringable> c) {
        return c->to_string();
    }

    template<typename T=void>
    std::pair<int, bool> to_int(std::string s) {
        try {
            return std::make_pair(std::stoi(s), true);
        } catch (...) {
            return std::make_pair(0, false);
        }
    };

    template<typename T=void>
    std::string join(std::list<std::string> list, std::string sep) {
        std::stringstream stream;
        unsigned int i = 0;
        for (std::string str:list) {
            stream << str;
            if (i++ != list.size() - 1) {
                stream << sep;
            }
        }
        return stream.str();
    }

    template<typename T=void>
    std::vector<std::string> split(std::string s, char delim) {
        std::vector<std::string> elems;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    template<typename T, typename U>
    bool string_equals(T a, U b) {
        return str(a) == str(b);
    }

    template<typename T=void>
    bool ends_with(std::string full_string, std::string ending) {
        if (full_string.length() >= ending.length()) {
            return (0 == full_string.compare(full_string.length() - ending.length(), ending.length(), ending));
        } else {
            return false;
        }
    }

    template<typename T=void>
    wchar_t *to_wchar(const char *text) {
        size_t size = strlen(text) + 1;
        wchar_t *wa = new wchar_t[size];
        mbstowcs(wa, text, size);
        return wa;
    }

    template<typename T=void>
    wchar_t **to_wchar(int size, const char **text) {
        wchar_t **wa = new wchar_t *[size];
        for (int i = 0; i < size; i++) {
            wa[i] = to_wchar(text[i]);
        }
        return wa;
    }

    template<typename T=void>
    std::string stem(const std::string &script) {
        return boost::filesystem::path(script).stem().string();
    }
}