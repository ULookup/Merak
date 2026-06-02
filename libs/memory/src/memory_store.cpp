#include <merak/memory_store.hpp>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <expected>

namespace merak {

MemoryStore::MemoryStore(const MemoryConfig& config,
                         std::shared_ptr<EmbeddingProvider> embedder)
    : config_(config)
    , embedder_(std::move(embedder))
{
}

MemoryStore::~MemoryStore() {
}

void MemoryStore::append_message(const Message& msg) {
    std::lock_guard lock(working_memory_mutex_);
    working_memory_.push_back(msg);
}

std::vector<Message> MemoryStore::recent_history(int max_turns) const {
    std::lock_guard lock(working_memory_mutex_);
    int msg_count = max_turns * 2;
    int total = (int)working_memory_.size();
    int start = std::max(0, total - msg_count);

    std::vector<Message> result;
    for (int i = start; i < total; i++) {
        result.push_back(working_memory_[i]);
    }
    return result;
}

int MemoryStore::message_count() const {
    std::lock_guard lock(working_memory_mutex_);
    return (int)working_memory_.size();
}

std::expected<void, AgentError> MemoryStore::init_db() {
    if (!config_.enabled) return {};
    return create_tables();
}

std::expected<void, AgentError> MemoryStore::create_tables() {
    try {
        auto conn = std::make_unique<pqxx::connection>(config_.db_connection);

        pqxx::work txn(*conn);
        txn.exec("CREATE EXTENSION IF NOT EXISTS vector");
        txn.exec("CREATE EXTENSION IF NOT EXISTS \"uuid-ossp\"");

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS memory_entries (
                id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
                content TEXT NOT NULL,
                type VARCHAR(32) NOT NULL DEFAULT 'semantic',
                embedding VECTOR(1536),
                confidence DOUBLE PRECISION DEFAULT 1.0,
                created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
                last_accessed TIMESTAMP WITH TIME ZONE DEFAULT NOW()
            )
        )");

        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_memory_embedding
            ON memory_entries USING ivfflat (embedding vector_cosine_ops)
            WITH (lists = 100)
        )");

        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_memory_type
            ON memory_entries (type)
        )");
        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_memory_confidence
            ON memory_entries (confidence)
        )");

        txn.commit();
        spdlog::info("MemoryStore: tables created successfully");
        return {};
    } catch (const pqxx::failure& e) {
        return std::unexpected(AgentError(
            ErrorType::MEMORY_ERROR,
            std::string("Database init failed: ") + e.what()
        ));
    }
}

std::future<std::expected<void, AgentError>> MemoryStore::store(
    std::string content,
    std::string type
) {
    return std::async(std::launch::async, [this, content = std::move(content), type]()
        -> std::expected<void, AgentError>
    {
        if (!config_.enabled) return {};

        if (!embedder_) {
            return std::unexpected(AgentError(
                ErrorType::MEMORY_ERROR,
                "No EmbeddingProvider configured"
            ));
        }

        auto emb_future = embedder_->embed(content);
        auto embedding = emb_future.get();

        try {
            auto conn = std::make_unique<pqxx::connection>(config_.db_connection);
            pqxx::work txn(*conn);

            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < embedding.size(); i++) {
                if (i > 0) oss << ",";
                oss << embedding[i];
            }
            oss << "]";
            std::string emb_str = oss.str();

            txn.exec_params(
                "INSERT INTO memory_entries (content, type, embedding) VALUES ($1, $2, $3)",
                content, type, emb_str
            );

            txn.commit();
            spdlog::debug("MemoryStore: stored entry, type={}, dim={}", type, embedding.size());
            return {};
        } catch (const pqxx::failure& e) {
            return std::unexpected(AgentError(
                ErrorType::MEMORY_ERROR,
                std::string("Store failed: ") + e.what()
            ));
        }
    });
}

std::future<std::expected<std::vector<MemorySnippet>, AgentError>> MemoryStore::search(
    const std::string& query,
    int top_k
) {
    return std::async(std::launch::async, [this, query, top_k]()
        -> std::expected<std::vector<MemorySnippet>, AgentError>
    {
        if (!config_.enabled) {
            std::vector<MemorySnippet> empty;
            return empty;
        }

        if (!embedder_) {
            return std::unexpected(AgentError(
                ErrorType::MEMORY_ERROR,
                "No EmbeddingProvider configured"
            ));
        }

        auto emb_future = embedder_->embed(query);
        auto query_emb = emb_future.get();

        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < query_emb.size(); i++) {
            if (i > 0) oss << ",";
            oss << query_emb[i];
        }
        oss << "]";
        std::string emb_str = oss.str();

        try {
            auto conn = std::make_unique<pqxx::connection>(config_.db_connection);
            pqxx::work txn(*conn);

            auto result = txn.exec_params(
                R"(
                    SELECT id, content, type,
                           1 - (embedding <=> $1) AS relevance
                    FROM memory_entries
                    WHERE confidence > 0.1
                    ORDER BY embedding <=> $1
                    LIMIT $2
                )",
                emb_str, top_k
            );

            std::vector<MemorySnippet> snippets;
            for (auto const& row : result) {
                MemorySnippet snip;
                snip.id = row["id"].c_str();
                snip.content = row["content"].c_str();
                snip.type = row["type"].c_str();
                snip.relevance = row["relevance"].as<double>();

                txn.exec_params(
                    "UPDATE memory_entries SET last_accessed = NOW() WHERE id = $1",
                    snip.id
                );

                snippets.push_back(std::move(snip));
            }

            txn.commit();
            spdlog::debug("MemoryStore: search '{}' returned {} results",
                query.substr(0, 50), snippets.size());
            return snippets;
        } catch (const pqxx::failure& e) {
            return std::unexpected(AgentError(
                ErrorType::MEMORY_ERROR,
                std::string("Search failed: ") + e.what()
            ));
        }
    });
}

std::expected<void, AgentError> MemoryStore::remove(const std::string& id) {
    if (!config_.enabled) return {};
    try {
        auto conn = std::make_unique<pqxx::connection>(config_.db_connection);
        pqxx::work txn(*conn);
        txn.exec_params("DELETE FROM memory_entries WHERE id = $1", id);
        txn.commit();
        return {};
    } catch (const pqxx::failure& e) {
        return std::unexpected(AgentError(
            ErrorType::MEMORY_ERROR,
            std::string("Remove failed: ") + e.what()
        ));
    }
}

std::expected<int, AgentError> MemoryStore::decay_confidence() {
    if (!config_.enabled) return 0;
    try {
        auto conn = std::make_unique<pqxx::connection>(config_.db_connection);
        pqxx::work txn(*conn);
        auto result = txn.exec_params(
            R"(
                UPDATE memory_entries
                SET confidence = GREATEST(0, confidence - $1)
                WHERE last_accessed < NOW() - INTERVAL '$2 days'
                RETURNING id
            )",
            config_.confidence_decay, config_.decay_interval_days
        );
        txn.commit();
        int count = (int)result.size();
        spdlog::info("MemoryStore: decayed {} entries", count);
        return count;
    } catch (const pqxx::failure& e) {
        return std::unexpected(AgentError(
            ErrorType::MEMORY_ERROR,
            std::string("Decay failed: ") + e.what()
        ));
    }
}

std::expected<int, AgentError> MemoryStore::purge_expired(double threshold) {
    if (!config_.enabled) return 0;
    try {
        auto conn = std::make_unique<pqxx::connection>(config_.db_connection);
        pqxx::work txn(*conn);
        auto result = txn.exec_params(
            "DELETE FROM memory_entries WHERE confidence < $1 RETURNING id",
            threshold
        );
        txn.commit();
        int count = (int)result.size();
        spdlog::info("MemoryStore: purged {} expired entries", count);
        return count;
    } catch (const pqxx::failure& e) {
        return std::unexpected(AgentError(
            ErrorType::MEMORY_ERROR,
            std::string("Purge failed: ") + e.what()
        ));
    }
}

} // namespace merak
