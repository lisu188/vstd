#pragma once

#include <mutex>

#include "future.h"

namespace vstd{
template<typename T>
class chain {
    std::shared_ptr<vstd::future<void, void>> _future = vstd::async([]() { });
    std::recursive_mutex _lock;
    std::function<void(T)> _cb;
public:
    chain(std::function<void(T)> cb):_cb(cb){}
    void invoke_async(T t){
        std::unique_lock<std::recursive_mutex> lock(_lock);
        _future = _future->thenAsync(vstd::bind(_cb,t));
    }
    void invoke_later(T t){
        std::unique_lock<std::recursive_mutex> lock(_lock);
        _future = _future->thenLater(vstd::bind(_cb,t));
    }
    std::shared_ptr<vstd::future<void, void>> terminate(){
        std::unique_lock<std::recursive_mutex> lock(_lock);
        return _future;
    };
};}