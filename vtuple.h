#pragma once

namespace vstd {

    template<std::size_t __i, typename _Head, typename... _Tail>
    struct tuple_element : tuple_element<__i - 1, _Tail... > {
    };

    template<typename _Head, typename... _Tail>
    struct tuple_element<0, _Head, _Tail... > {
        typedef _Head type;
    };
}