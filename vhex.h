#pragma once

#include <boost/algorithm/string.hpp>
#include "vdefines.h"

namespace vstd {
    template<typename U>
    std::string to_hex(U object) {
        std::stringstream stream;
        stream << std::hex << object;
        return boost::to_upper_copy<std::string>(stream.str());
    }

    template<typename U>
    std::string to_hex(U *object) {
        return to_hex(static_cast<std::size_t > ( object ));
    }

    template<typename U>
    std::string to_hex(std::shared_ptr<U> object) {
        return to_hex<U *>(object.get());
    }

    template<typename U>
    std::string to_hex_hash(U object) {
        return to_hex<std::size_t>(hash_combine(object));
    }
}
