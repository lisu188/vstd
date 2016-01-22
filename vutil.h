#pragma once

#include <list>
#include <set>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <boost/pool/pool_alloc.hpp>
#include <boost/any.hpp>
#include <queue>
#include "vtraits.h"
#include "vhash.h"
#include "vcast.h"

namespace vstd {
    template<typename Container, typename Value>
    bool ctn(Container &container, Value value) {
        return container.find(value) != container.end();
    }

    template<typename T, typename U>
    bool castable(std::shared_ptr<U> ptr) {
        return std::dynamic_pointer_cast<T>(ptr).operator bool();
    }

    template<typename A, typename B>
    auto type_pair() {
        return std::make_pair(boost::typeindex::type_id<A>(), boost::typeindex::type_id<B>());
    }

    template<typename F, typename... Args>
    std::function<typename function_traits<F>::return_type()> bind(F f, Args... args) {
        return std::bind(f, args...);
    }

    template<typename Range, typename Value=typename range_traits<Range>::value_type>
    std::set<Value> collect(Range r) {

        return cast<std::set<Value>>(r);
    }

    template<typename T>
    typename T::value_type pop(T &t) {
        auto x = t.front();
        t.pop();
        return x;
    }

    template<typename T>
    typename T::value_type pop_p(T &t) {
        auto x = t.top();
        t.pop();
        return x;
    }

    template<typename F,
            typename ret=typename vstd::function_traits<F>::return_type,
            typename arg=typename vstd::function_traits<F>::template arg<0>::type>
    std::function<ret(arg)> make_function(F f,
                                          typename vstd::disable_if<std::is_void<arg>::value>::type * = 0) {
        return std::function<ret(arg)>(f);
    }

    template<typename F,
            typename ret=typename vstd::function_traits<F>::return_type,
            typename arg=typename vstd::function_traits<F>::template arg<0>::type>
    std::function<ret()> make_function(F f,
                                       typename vstd::enable_if<std::is_void<arg>::value>::type * = 0) {
        return std::function<ret()>(f);
    }

    //TODO: use somewhere std::priority_queue<T,std::vector<T>,std::function<bool ( T,T ) >>
    template<typename T, typename Queue=std::queue<T>>
    class blocking_queue {
    private:
        std::recursive_mutex d_mutex;
        std::condition_variable_any d_condition;
        Queue d_queue;
    public:
        typedef T value_type;

        void push(T value) {
            std::unique_lock<std::recursive_mutex> lock(d_mutex);
            d_queue.push(value);
            d_condition.notify_one();
        }

        void pop(std::function<void(T)> callback) {
            T value;
            {
                std::unique_lock<std::recursive_mutex> lock(d_mutex);
                d_condition.wait(lock, [this]() {
                    return !d_queue.empty();
                });
                value = vstd::pop(d_queue);
            }
            callback(value);
        }
    };

    std::mt19937_64 &rng() {
        static std::mt19937_64 rng;
        return rng;
    }

    std::uniform_real_distribution<double> &unif() {
        static std::uniform_real_distribution<double> unif(-0.5, 0.5);
        return unif;
    }

    double rand() {
        return unif()(rng());
    };

    template<typename T>
    static T *allocate(size_t size) {
        static boost::pool_allocator<T> _pool;
        return _pool.allocate(size);
    }

    template<typename T>
    static void deallocate(T *t, size_t size) {
        static boost::pool_allocator<T> _pool;
        _pool.deallocate(t, size);
    }

    template<typename T>
    typename T::value_type *as_array(T vec) {
        auto ret = vstd::allocate<typename T::value_type>(vec.size());
        std::copy(std::begin(vec), std::end(vec), ret);
        return ret;
    }
}
