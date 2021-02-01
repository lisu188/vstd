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
#include "vstd.h"
#include "vtuple.h"

namespace vstd {
    template<bool B, typename T = void> using enable_if = std::enable_if<B, T>;
    template<bool B, typename T = void> using disable_if = std::enable_if<!B, T>;

    using std::is_enum;
    using std::is_same;
    using std::is_base_of;
    using std::is_void;

    namespace detail {
        template<typename T>
        struct return_type
                : public return_type<decltype(&T::operator())> {
        };

        template<typename ClassType, typename ReturnType, typename... Args>
        struct return_type<ReturnType (ClassType::*)(Args...) const> {
            typedef ReturnType type;
        };

        template<size_t i>
        void args(typename vstd::enable_if<i == 0>::type * = 0) {}

        template<size_t i, typename... Args>
        typename vstd::tuple_element<i, Args...>

        ::type
        args(typename vstd::disable_if<sizeof... (Args) == 0>::type * = 0) {
            //return std::declval<typename std::tuple_element<i, std::tuple<Args...>>::type>() ;
        }
    }

    template<typename T>
    struct function_traits
            : public function_traits<decltype(&T::operator())> {
    };

    template<typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType (ClassType::*)(Args...) const> {

        constexpr static size_t arity = sizeof... (Args);


        typedef ReturnType return_type;

        template<size_t i>
        struct arg {
            typedef decltype(detail::args<i, Args...>()) type;
        };

        typedef typename arg<0>::type first_arg;
    };

    template<typename T, typename... Args>
    class has_insert {
        template<typename C,
                typename = decltype(std::declval<C>().insert(std::declval<Args>()...))>
        static std::true_type test(int);

        template<typename C>
        static std::false_type test(...);

    public:
        static constexpr bool value = decltype(test<T>(0))
        ::value;
    };

    template<typename Range>
    struct range_traits {
        typedef typename vstd::function_traits<decltype(&Range::begin)>::return_type iterator;
        typedef typename vstd::function_traits<decltype(&iterator::operator*)>::return_type value_type;
    };

    template<typename T>
    struct clear_type {
        typedef typename std::remove_reference<typename std::remove_cv<T>::type>::type type;
    };

    template<typename T, typename U>
    struct is_same_clear : public std::is_same<typename clear_type<T>::type, typename clear_type<U>::type> {

    };

    template<class T, class R = void>
    struct enable_if_type {
        typedef R type;
    };

    template<class T, class Enable = void>
    struct is_shared_ptr : std::false_type {
    };

    template<class T>
    struct is_shared_ptr<T, typename enable_if_type<typename T::element_type>::type> : std::true_type {
    };

    template<class T, class Enable = void>
    struct is_set : std::false_type {
    };

    template<class T>
    struct is_set<T, typename std::enable_if<std::is_same<typename T::key_type, typename T::value_type>::value>::type>
            : std::true_type {
    };

    template<class T, class E1 = void, class E2 = void, class E3 = void>
    struct is_map : std::false_type {
    };

    template<class T>
    struct is_map<T, typename enable_if_type<typename T::key_type>::type, typename enable_if_type<typename T::value_type>::type, typename disable_if<is_set<T>::value>::type>
            : std::true_type {
    };

    template<class T, class E1 = void, class E2 = void>
    struct is_pair : std::false_type {
    };

    template<class T>
    struct is_pair<T, typename enable_if_type<typename T::first_type>::type, typename enable_if_type<typename T::second_type>::type>
            : std::true_type {
    };

    template<class T, class E1 = void, class E2=void>
    struct is_range : std::false_type {
    };

    template<class T>
    struct is_range<T, typename enable_if_type<typename T::value_type>::type,
            typename disable_if<is_same_clear<T, std::string>::value>::type> : std::true_type {
    };

    template<class T, class E1 = void, class E2=void, class E3=void>
    struct is_container : std::false_type {
    };

    template<class T>
    struct is_container<T, typename enable_if_type<typename T::value_type>::type,
            typename enable_if<has_insert<T, typename T::value_type>::value>::type,
            typename disable_if<is_same_clear<T, std::string>::value>::type> : std::true_type {
    };

    template<class T, class E1 = void>
    struct is_pure_range : std::false_type {
    };

    template<class T>
    struct is_pure_range<T, typename std::enable_if<
            is_range<T>::value && !is_container<T>::value>::type> : std::true_type {
    };
}


