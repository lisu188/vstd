#pragma once

#include <boost/algorithm/string.hpp>
#include "defines.h"

namespace vstd {
    template<typename T,typename U>
    force_inline T to_hex ( U object ) {
        std::stringstream stream;
        stream << std::hex << object;
        std::string s=boost::to_upper_copy<std::string> ( stream.str() );
        return T ( s.c_str() );
    }

    template<typename T,typename U>
    force_inline T to_hex ( U * object ) {
        return to_hex<T> ( static_cast<std::size_t >( object ) );
    }

    template<typename T,typename U>
    force_inline T to_hex ( std::shared_ptr<U> object ) {
        return to_hex<T,U*> ( object.get() );
    }

    template<typename T,typename U>
    force_inline T to_hex_hash ( U object ) {
        return to_hex<T,std::size_t > ( hash_combine ( object ) );
    }
}
