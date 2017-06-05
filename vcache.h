#pragma once

namespace vstd {
    template<typename key, typename val, val(*get_next)(), int(*get_ttl)()>
    class cache {
        std::unordered_map<key, std::pair<val, int>> _values;
    public:
        val get(key k, int time) {
            auto it = _values.find(k);
            if (it == _values.end()) {
                val ret = get_next();
                _values.insert(std::make_pair(k, std::make_pair(ret, time + get_ttl())));
                return ret;
            }
            if ((*it).second.second < time) {
                _values.erase(it);
                return get(k, time);
            }
            return (*it).second.first;
        }
    };
}