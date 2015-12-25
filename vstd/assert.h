#pragma once

#include "defines.h"

namespace vstd {
    template<typename T,  typename U,typename V=int>
    force_inline V fail_if ( T arg, U msg) {
        if ( arg ) {
            qFatal ( msg.toStdString().c_str() );
        }
        return U();
    }

    template<typename T,typename U=int>
    force_inline U fail ( T msg ) {
        return fail_if<bool, T> ( true, msg );
    }

    template<typename T,typename U>
    force_inline T not_null ( T t, U msg=U()) {
        if ( t ) {
            return t;
        }
        return fail<T> ( msg );
    }
}
