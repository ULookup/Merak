#pragma once
#include <merak/memory_store.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <mutex>
#include <future>

namespace merak {

class OpenAIEmbeddingProvider : public EmbeddingProvider {
public:
    struct Config {
        std::string api_url = "https://api.openai.com/v1";
        std::string api_key;
        std::string model = "text-embedding-3-small";
        int cache_size = 512;
        int batch_size = 20;
        int timeout_ms = 10000;
    };

    explicit OpenAIEmbeddingProvider(const Config& config);
    ~OpenAIEmbeddingProvider() override = default;

    std::future<std::vector<float>> embed(const std::string& text) override;
    std::future<std::vector<std::vector<float>>> embed_batch(
        const std::vector<std::string>& texts) override;
    int dimension() const override { return 1536; }

private:
    Config config_;
    struct CacheEntry {
        std::string key;
        std::vector<float> embedding;
    };
    std::list<CacheEntry> cache_list_;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> cache_map_;
    mutable std::mutex cache_mutex_;

    std::vector<float> embed_single(const std::string& text);
    std::vector<float> get_or_cache(const std::string& cache_key, const std::string& text);
};

} // namespace merak
