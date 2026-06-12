#pragma once
#include <merak/message.hpp>
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <optional>
#include <expected>
#include <mutex>

namespace merak {

struct MemoryEntry {
    std::string id;
    std::string content;
    std::string type;
    std::vector<float> embedding;
    double confidence = 1.0;
    std::string timestamp;
    std::string last_accessed;
};

struct MemorySnippet {
    std::string id;
    std::string content;
    std::string type;
    double relevance = 0.0;
};

class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;
    virtual std::future<std::vector<float>> embed(const std::string& text) = 0;
    virtual int dimension() const = 0;
    virtual std::future<std::vector<std::vector<float>>> embed_batch(
        const std::vector<std::string>& texts) = 0;
};

class MemoryStore {
public:
    MemoryStore(const MemoryConfig& config,
                std::shared_ptr<EmbeddingProvider> embedder);
    ~MemoryStore();

    void append_message(const Message& msg);
    std::vector<Message> recent_history(int max_turns = 20) const;
    int message_count() const;

    std::expected<void, AgentError> init_db();
    std::future<std::expected<void, AgentError>> store(
        std::string content,
        std::string type = "semantic"
    );
    std::future<std::expected<std::vector<MemorySnippet>, AgentError>> search(
        const std::string& query,
        int top_k = 5
    );
    std::expected<void, AgentError> remove(const std::string& id);
    std::expected<int, AgentError> decay_confidence();
    std::expected<int, AgentError> purge_expired(double threshold = 0.1);

private:
    MemoryConfig config_;
    std::shared_ptr<EmbeddingProvider> embedder_;
    std::vector<Message> working_memory_;
    mutable std::mutex working_memory_mutex_;

    std::expected<void, AgentError> create_tables();
};

} // namespace merak
