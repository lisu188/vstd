#pragma once

namespace vstd {
    template<typename T, typename... U>
    class lazy {
    public:
        std::shared_ptr<T> get(U... parent) {
            if (ptr) {
                return ptr;
            }
            return ptr = std::make_shared<T>(parent...);
        }

    private :
        std::shared_ptr<T> ptr;
    };

    template<typename T, typename F=std::function<std::shared_ptr<T>()>>
    class lazy2 {
    public:
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
