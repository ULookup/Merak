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

std::vector<float> OpenAIEmbeddingProvider::get_or_cache(
    const std::string& cache_key, const std::string& text)
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_map_.find(cache_key);
    if (it != cache_map_.end()) {
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        return it->second->embedding;
    }
    auto embedding = embed_single(text);
    cache_list_.push_front({cache_key, embedding});
    cache_map_[cache_key] = cache_list_.begin();
    if (static_cast<int>(cache_list_.size()) > config_.cache_size) {
        auto last = cache_list_.end();
        --last;
        cache_map_.erase(last->key);
        cache_list_.pop_back();
    }
    return embedding;
}

std::vector<float> OpenAIEmbeddingProvider::embed_single(const std::string& text) {
    std::string url = config_.api_url;
    while (!url.empty() && url.back() == '/') url.pop_back();

    std::string host;
    if (url.find("https://") == 0) host = url.substr(8);
    else if (url.find("http://") == 0) host = url.substr(7);
    else host = url;

    httplib::Client cli(host);
    cli.set_connection_timeout(config_.timeout_ms / 1000, 0);
    cli.set_read_timeout(config_.timeout_ms / 1000, 0);

    nlohmann::json body;
    body["model"] = config_.model;
    body["input"] = text;

    httplib::Headers headers = {{"Authorization", "Bearer " + config_.api_key}};

    // Retry once on failure
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
        return std::vector<float>(1536, 0.0f);
    }

    auto json = nlohmann::json::parse(res->body);
    auto& arr = json["data"][0]["embedding"];
    std::vector<float> result;
    result.reserve(arr.size());
    for (auto& val : arr) result.push_back(val.get<float>());
    return result;
}

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

        for (size_t i = 0; i < texts.size(); i += config_.batch_size) {
            size_t batch_end = std::min(i + config_.batch_size, texts.size());

            // Check cache first
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

            // Batch API call
            std::string url = config_.api_url;
            while (!url.empty() && url.back() == '/') url.pop_back();
            std::string host;
            if (url.find("https://") == 0) host = url.substr(8);
            else if (url.find("http://") == 0) host = url.substr(7);
            else host = url;

            httplib::Client cli(host);
            cli.set_connection_timeout(config_.timeout_ms / 1000, 0);
            cli.set_read_timeout(config_.timeout_ms / 1000, 0);

            nlohmann::json body;
            body["model"] = config_.model;
            body["input"] = uncached_texts;

            httplib::Headers headers = {{"Authorization", "Bearer " + config_.api_key}};

            // Retry once on failure
            httplib::Result res;
            for (int attempt = 0; attempt < 2; ++attempt) {
                res = cli.Post("/v1/embeddings", headers, body.dump(), "application/json");
                if (res && res->status == 200) break;
                if (attempt == 0) {
                    spdlog::warn("OpenAIEmbeddingProvider: retrying batch after API error");
                }
            }

            if (res && res->status == 200) {
                auto json = nlohmann::json::parse(res->body);
                for (size_t k = 0; k < uncached_texts.size(); ++k) {
                    std::vector<float> emb;
                    auto& arr = json["data"][k]["embedding"];
                    emb.reserve(arr.size());
                    for (auto& v : arr) emb.push_back(v.get<float>());
                    results[uncached_idx[k]] = emb;

                    std::string key = config_.model + ":" + uncached_texts[k];
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    cache_list_.push_front({key, emb});
                    cache_map_[key] = cache_list_.begin();
                    if (static_cast<int>(cache_list_.size()) > config_.cache_size) {
                        auto last = cache_list_.end(); --last;
                        cache_map_.erase(last->key);
                        cache_list_.pop_back();
                    }
                }
            } else {
                for (size_t k = 0; k < uncached_texts.size(); ++k) {
                    results[uncached_idx[k]] = std::vector<float>(1536, 0.0f);
                }
            }
        }
        return results;
    });
}

} // namespace merak
