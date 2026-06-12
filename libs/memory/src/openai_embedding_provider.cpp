#include <merak/openai_embedding_provider.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <future>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

namespace merak {

OpenAIEmbeddingProvider::OpenAIEmbeddingProvider(const Config& config)
    : config_(config)
{
    if (config_.api_key.empty()) {
        spdlog::warn("OpenAIEmbeddingProvider: api_key is empty, embedding will fail");
    }
}

// ——— helpers ———

// Extract host[:port] from an API base URL, stripping scheme, path, query, fragment
static std::string parse_host(const std::string& api_url) {
    std::string url = api_url;
    size_t start = 0;
    if (url.find("https://") == 0) start = 8;
    else if (url.find("http://") == 0) start = 7;

    size_t end = url.find_first_of("/?#", start);
    if (end == std::string::npos) end = url.size();
    return url.substr(start, end - start);
}

static httplib::Client make_client(const std::string& host, int timeout_ms) {
    httplib::Client cli(host);
    cli.set_connection_timeout(timeout_ms / 1000, 0);
    cli.set_read_timeout(timeout_ms / 1000, 0);
    return cli;
}

static std::vector<float> parse_embedding(const nlohmann::json& data_entry) {
    auto& arr = data_entry["embedding"];
    std::vector<float> result;
    result.reserve(arr.size());
    for (auto& val : arr) result.push_back(val.get<float>());
    return result;
}

// ——— cache ———

std::vector<float> OpenAIEmbeddingProvider::get_or_cache(
    const std::string& cache_key, const std::string& text)
{
    // Check cache under lock, release before I/O
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_map_.find(cache_key);
        if (it != cache_map_.end()) {
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
            return it->second->embedding;
        }
    }

    // Do I/O without holding the lock
    auto embedding = embed_single(text);

    // Re-acquire lock, double-check another thread didn't insert first.
    // Only cache non-empty results (empty = API failure).
    if (!embedding.empty()) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_map_.find(cache_key);
        if (it != cache_map_.end()) {
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
            return it->second->embedding;
        }
        cache_list_.push_front({cache_key, embedding});
        cache_map_[cache_key] = cache_list_.begin();
        if (static_cast<int>(cache_list_.size()) > config_.cache_size) {
            auto last = cache_list_.end();
            --last;
            cache_map_.erase(last->key);
            cache_list_.pop_back();
        }
    }
    return embedding;
}

// ——— single embedding ———

std::vector<float> OpenAIEmbeddingProvider::embed_single(const std::string& text) {
    if (text.empty()) return {};

    std::string host = parse_host(config_.api_url);
    auto cli = make_client(host, config_.timeout_ms);

    nlohmann::json body;
    body["model"] = config_.model;
    body["input"] = text;

    httplib::Headers headers = {{"Authorization", "Bearer " + config_.api_key}};

    httplib::Result res;
    for (int attempt = 0; attempt < 2; ++attempt) {
        res = cli.Post("/v1/embeddings", headers, body.dump(), "application/json");
        if (res && res->status == 200) break;
        if (attempt == 0) {
            spdlog::warn("OpenAIEmbeddingProvider: retrying after API error");
        }
    }

    if (!res || res->status != 200) {
        spdlog::error("OpenAIEmbeddingProvider: API error after retry");
        return {};
    }

    try {
        auto json = nlohmann::json::parse(res->body);
        return parse_embedding(json["data"][0]);
    } catch (const nlohmann::json::exception& e) {
        spdlog::error("OpenAIEmbeddingProvider: JSON parse error: {}", e.what());
        return {};
    }
}

// ——— public API ———

std::future<std::vector<float>> OpenAIEmbeddingProvider::embed(const std::string& text) {
    return std::async(std::launch::async, [this, text]() -> std::vector<float> {
        std::string cache_key = config_.model + ":" + text;
        return get_or_cache(cache_key, text);
    });
}

std::future<std::vector<std::vector<float>>> OpenAIEmbeddingProvider::embed_batch(
    const std::vector<std::string>& texts)
{
    return std::async(std::launch::async, [this, texts]() -> std::vector<std::vector<float>> {
        std::vector<std::vector<float>> results(texts.size());

        std::string host = parse_host(config_.api_url);
        auto cli = make_client(host, config_.timeout_ms);

        for (size_t i = 0; i < texts.size(); i += config_.batch_size) {
            size_t batch_end = std::min(i + config_.batch_size, texts.size());

            // Check cache first, release lock before I/O
            std::vector<size_t> uncached_idx;
            std::vector<std::string> uncached_texts;
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                for (size_t j = i; j < batch_end; ++j) {
                    std::string key = config_.model + ":" + texts[j];
                    auto it = cache_map_.find(key);
                    if (it != cache_map_.end()) {
                        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
                        results[j] = it->second->embedding;
                    } else {
                        uncached_idx.push_back(j);
                        uncached_texts.push_back(texts[j]);
                    }
                }
            }

            if (uncached_texts.empty()) continue;

            // Batch API call (no lock held)
            nlohmann::json body;
            body["model"] = config_.model;
            body["input"] = uncached_texts;

            httplib::Headers headers = {{"Authorization", "Bearer " + config_.api_key}};

            httplib::Result res;
            for (int attempt = 0; attempt < 2; ++attempt) {
                res = cli.Post("/v1/embeddings", headers, body.dump(), "application/json");
                if (res && res->status == 200) break;
                if (attempt == 0) {
                    spdlog::warn("OpenAIEmbeddingProvider: retrying batch after API error");
                }
            }

            if (res && res->status == 200) {
                try {
                    auto json = nlohmann::json::parse(res->body);

                    // Insert all embeddings under a single lock
                    {
                        std::lock_guard<std::mutex> lock(cache_mutex_);
                        for (size_t k = 0; k < uncached_texts.size(); ++k) {
                            auto emb = parse_embedding(json["data"][k]);
                            results[uncached_idx[k]] = emb;

                            if (emb.empty()) continue; // skip caching failures

                            std::string key = config_.model + ":" + uncached_texts[k];
                            // Double-check: another thread may have inserted while we were doing I/O
                            if (cache_map_.find(key) == cache_map_.end()) {
                                cache_list_.push_front({key, emb});
                                cache_map_[key] = cache_list_.begin();
                                if (static_cast<int>(cache_list_.size()) > config_.cache_size) {
                                    auto last = cache_list_.end(); --last;
                                    cache_map_.erase(last->key);
                                    cache_list_.pop_back();
                                }
                            }
                        }
                    }
                } catch (const nlohmann::json::exception& e) {
                    spdlog::error("OpenAIEmbeddingProvider: batch JSON parse error: {}", e.what());
                    for (size_t k = 0; k < uncached_texts.size(); ++k) {
                        results[uncached_idx[k]] = {};
                    }
                }
            } else {
                spdlog::error("OpenAIEmbeddingProvider: batch API error after retry");
                for (size_t k = 0; k < uncached_texts.size(); ++k) {
                    results[uncached_idx[k]] = {};
                }
            }
        }
        return results;
    });
}

} // namespace merak
