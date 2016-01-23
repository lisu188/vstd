#pragma once

#include "vcast.h"
#include "vutil.h"
#include <unordered_map>

namespace vstd {
    namespace detail {
        template<typename T=void>
        //left returned
        //right taken
        std::unordered_map<std::pair<boost::typeindex::type_index, boost::typeindex::type_index>, std::function<boost::any(
                boost::any)>> &registry() {
            static std::unordered_map<std::pair<boost::typeindex::type_index, boost::typeindex::type_index>, std::function<boost::any(
                    boost::any)>> reg;
            return reg;
        };
    }

    template<typename T>
    T any_cast(boost::any val) {
        auto key = std::pair<boost::typeindex::type_index, boost::typeindex::type_index>( boost::typeindex::type_id<T>(),
                                                                                       val.type() );
        if (vstd::ctn(detail::registry(), key)) {
            return boost::any_cast<T>(detail::registry()[key](val));
        }
        return boost::any_cast<T>(val);
    }

    template<typename T, typename U>
    void register_any_type() {
        detail::registry()[vstd::type_pair<T, U>()] = [](
                boost::any conv) {
            return boost::any(vstd::cast<T>(boost::any_cast<U>(conv)));
        };
    };
}