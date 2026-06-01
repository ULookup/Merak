#pragma once
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <merak/error.hpp>
#include <merak/config.hpp>
#include <string>
#include <vector>
#include <functional>
#include <future>
#include <memory>
#include <optional>

namespace merak {

struct StreamChunk {
    std::string text;
    bool is_final = false;
    bool is_tool_call = false;
};

struct ChatRequest {
    std::string model;
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    int max_output_tokens = 4096;
    bool enable_cache = true;
};

class LlmProvider {
public:
    virtual ~LlmProvider() = default;

    virtual std::future<AgentResponse> chat(
        const ChatRequest& request,
        std::function<void(StreamChunk)> on_chunk
    ) = 0;

    virtual std::string name() const = 0;
    virtual bool supports_caching() const { return false; }
};

struct CacheStats {
    int total_requests = 0;
    int cache_hits = 0;
    int input_tokens_saved = 0;
    double hit_rate() const {
        return total_requests > 0 ? (double)cache_hits / total_requests : 0.0;
    }
};

} // namespace merak
