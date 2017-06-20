#pragma once

namespace vstd {
    template<typename T>
    class lazy {
    public:
        template<typename F>
        std::shared_ptr<T> get(F f) {
            if (ptr) {
                return ptr;
            }
            return ptr = f();
        }

    private :
        std::shared_ptr<T> ptr;
    };
}
