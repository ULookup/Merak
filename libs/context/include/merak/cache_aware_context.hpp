#pragma once
#include <merak/message.hpp>
#include <vector>

namespace merak {

class CacheAwareContext {
public:
    struct Split {
        std::vector<Message> static_prefix;
        std::vector<Message> dynamic_suffix;
    };

    static Split split(const std::vector<Message>& full_context);
    static bool will_cache_hit(const Split& prev, const Split& curr);
    static void append(std::vector<Message>& messages, const Message& new_msg);
    static std::string info(const Split& s);
};

} // namespace merak
