#pragma once

#include <boost/range/adaptors.hpp>
#include "traits.h"
#include "future.h"

namespace vstd {
    namespace adaptors {
        template<typename F>
        auto add_later ( F f ) {
            return boost::adaptors::transformed ( [f] ( auto future ) {
                return future->thenLater ( f );
            } );
        }
    }
}
