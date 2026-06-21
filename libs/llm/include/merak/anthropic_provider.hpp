#pragma once
#include <merak/llm_provider.hpp>
#include <merak/config.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <curl/curl.h>

namespace merak {

class AnthropicProvider : public LlmProvider {
public:
    explicit AnthropicProvider(const LLMConfig& config);
    ~AnthropicProvider() override;

    std::future<AgentResponse> chat(
        const ChatRequest& request,
        std::function<void(StreamChunk)> on_chunk,
        std::shared_ptr<CancellationToken> cancellation = {}
    ) override;

    std::string name() const override { return "anthropic"; }
    bool supports_caching() const override { return true; }
    bool test_connection() override;
    const CacheStats& cache_stats() const { return stats_; }

    nlohmann::json build_request_body(const ChatRequest& request) const;

private:
    LLMConfig config_;
    CacheStats stats_;
};

} // namespace merak
