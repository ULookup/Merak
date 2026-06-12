#include <merak/cache_aware_context.hpp>
#include <sstream>

namespace merak {

CacheAwareContext::Split CacheAwareContext::split(
    const std::vector<Message>& full_context
) {
    Split result;
    bool found_first_non_static = false;

    for (auto& msg : full_context) {
        if (!found_first_non_static && msg.role == "system") {
            result.static_prefix.push_back(msg);
        } else {
            found_first_non_static = true;
            result.dynamic_suffix.push_back(msg);
        }
    }

    return result;
}

bool CacheAwareContext::will_cache_hit(
    const Split& prev,
    const Split& curr
) {
    if (prev.static_prefix.size() != curr.static_prefix.size()) return false;

    for (size_t i = 0; i < prev.static_prefix.size(); i++) {
        if (prev.static_prefix[i].content != curr.static_prefix[i].content)
            return false;
    }
    return true;
}

std::string CacheAwareContext::info(const Split& s) {
    std::ostringstream oss;
    oss << "Static prefix: " << s.static_prefix.size() << " messages, "
        << "Dynamic suffix: " << s.dynamic_suffix.size() << " messages";
    return oss.str();
}

} // namespace merak
