#include <merak/token_counter.hpp>
#include <merak/context_assembler.hpp>
#include <merak/cache_aware_context.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

int main() {
    // TokenCounter tests
    {
        TokenCounter counter("gpt-4o");
        assert(counter.count("hello world") > 0);
        assert(counter.count("") == 0);

        Message msg;
        msg.role = "user";
        msg.content = "test message";
        int tokens = counter.count(msg);
        assert(tokens > 0);

        std::vector<Message> msgs = {msg, msg, msg};
        int fit = counter.fit_in_budget(msgs, tokens * 2);
        assert(fit == 2);
    }

    // CacheAwareContext tests
    {
        std::vector<Message> msgs;
        Message sys;
        sys.role = "system";
        sys.content = "You are helpful";
        msgs.push_back(sys);

        Message user;
        user.role = "user";
        user.content = "hello";
        msgs.push_back(user);

        auto split = CacheAwareContext::split(msgs);
        assert(split.static_prefix.size() == 1);
        assert(split.static_prefix[0].role == "system");
        assert(split.dynamic_suffix.size() == 1);
        assert(split.dynamic_suffix[0].role == "user");
    }

    std::cout << "All context tests passed!" << std::endl;
    return 0;
}
