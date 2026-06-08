#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace merak::dsl {

class DslCache {
    std::unordered_map<std::string, std::string> cache_;

public:
    std::optional<std::string> get(const std::string& key) {
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        return std::nullopt;
    }

    void set(const std::string& key, const std::string& rendered) {
        cache_[key] = rendered;
    }

    void clear() { cache_.clear(); }
};

} // namespace merak::dsl
